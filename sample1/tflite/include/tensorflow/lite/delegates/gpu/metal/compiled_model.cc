/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/delegates/gpu/metal/compiled_model.h"

#include <algorithm>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/substitute.h"
#include "tensorflow/lite/delegates/gpu/common/model.h"
#include "tensorflow/lite/delegates/gpu/common/shape.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/types.h"
#include "tensorflow/lite/delegates/gpu/common/util.h"

namespace tflite {
namespace gpu {
namespace metal {
namespace {

// Allows to get result about the graph compilation to validate graph. This
// information helps to find a cause of performance degradation, like misfusing.
struct OptimizationInfo {
  // Initial operations count before compilation.
  int operations_count;
  // GPU tasks count after fusion and splitting complex operations into few GPU
  // subtasks.
  int gpu_tasks_count;
  // Some operations are not used due to dependencies of the graph.
  std::vector<int> unused_operations;
  // Used inputs.
  std::vector<ValueId> input_buffer_ids;
  // Unused inputs. Requested outputs do not require this inputs to be used.
  std::vector<ValueId> unused_input_buffer_ids;
  // The outputs are deducted by the graph but not requested by user.
  std::vector<ValueId> extra_output_buffer_ids;
  // Outputs that are requested but can't be calculated by the graph.
  std::vector<ValueId> missing_output_buffer_ids;
};

using FusionSequence = std::vector<NodeDescriptor>;

bool Contains(const std::vector<ValueId>& container, ValueId value) {
  return std::find(container.begin(), container.end(), value) !=
         container.end();
}

template <class T>
bool Contains(const std::vector<T>& container, ValueId value) {
  for (const auto& buffer : container) {
    if (buffer.id == value) {
      return true;
    }
  }
  return false;
}

// Checks if all elements of the narrow vector exist in the wide vector. Vectors
// are expected to be unsorted.
bool Contains(const std::vector<ValueId>& wide,
              const std::vector<ValueId>& narrow) {
  if (narrow.empty() || narrow.size() > wide.size()) {
    return false;
  }
  std::set<ValueId> wide_sorted;
  wide_sorted.insert(wide.begin(), wide.end());
  for (auto element : narrow) {
    if (std::find(wide.begin(), wide.end(), element) == wide.end()) {
      return false;
    }
  }
  return true;
}

uint32_t BufferUseCount(ValueId id,
                        const std::list<NodeDescriptor>& descriptors,
                        std::list<FusionSequence>* chains) {
  uint32_t use_count = 0;
  // Buffer may be read by both processed and not processed operations.
  for (auto& desc : descriptors) {
    if (Contains(desc.src_tensors_ids, id)) {
      use_count++;
    }
  }

  for (auto& chain : *chains) {
    for (auto& desc : chain) {
      if (Contains(desc.src_tensors_ids, id)) {
        use_count++;
      }
    }
  }
  return use_count;
}

// Examines if the second operation can be linked to the first one. Linking may
// be skipped in the situation when conflict may happen: if first operation's
// output is used by more than 1 other operation.
bool CanFuseOperations(const NodeDescriptor& first,
                       const NodeDescriptor& second,
                       const std::vector<ValueId>& output_ids,
                       const std::list<NodeDescriptor>& descriptors,
                       std::list<FusionSequence>* chains) {
  return second.task->is_linkable &&
         !Contains(output_ids, first.dst_tensors_ids[0]) &&
         BufferUseCount(first.dst_tensors_ids[0], descriptors, chains) == 1;
}

// Takes an unsorted list of task descriptors, builds a list of chains. Each
// chain is a list of task descriptors that can be fused into a single GPU task.
// Building is started from the input IDs and building statistic is filled.
void BuildFusableChains(const std::vector<ValueId>& input_ids,
                        const std::vector<ValueId>& output_ids,
                        std::list<NodeDescriptor>* descriptors,
                        std::list<FusionSequence>* chains,
                        std::vector<int>* unused_ids) {
  // Proxy tasks for inputs - only output is valid on this elements.
  for (auto input_id : input_ids) {
    auto desc = std::make_shared<ComputeTaskDescriptor>();
    desc->is_linkable = true;
    desc->AddDstTensor("", {});
    NodeDescriptor node;
    node.task = desc;
    node.dst_tensors_ids = {input_id};
    chains->push_back({node});
  }

  if (descriptors->empty()) return;
  // Get all possible operations - grow-up chains.
  bool added;
  do {
    // At least one element must be added to any chain at this step.
    added = false;
    for (auto it = descriptors->begin(); it != descriptors->end();) {
      const NodeDescriptor& task_descriptor = *it;

      // Gather all outputs of all chains to check with.
      std::vector<ValueId> ready_buffer_ids;
      ready_buffer_ids.reserve(chains->size());
      for (const auto& chain : *chains) {
        ready_buffer_ids.push_back(chain.back().dst_tensors_ids[0]);
      }

      // Check if all inputs of this operation are ready.
      if (Contains(ready_buffer_ids, task_descriptor.src_tensors_ids)) {
        // Now find a chain to fuse with.
        bool fused = false;
        for (auto& chain : *chains) {
          // We can fuse only single output for now.
          bool can_link = false;
          if (task_descriptor.task->is_associative_op) {
            can_link = Contains(task_descriptor.src_tensors_ids,
                                chain.back().dst_tensors_ids[0]);
          } else {
            can_link = task_descriptor.src_tensors_ids[0] ==
                       chain.back().dst_tensors_ids[0];
          }
          if (can_link && CanFuseOperations(chain.back(), task_descriptor,
                                            output_ids, *descriptors, chains)) {
            chain.push_back(task_descriptor);
            fused = true;
            break;
          }
        }
        if (!fused) {
          chains->push_back({task_descriptor});
        }

        // Remove operation from original list and start from the beginning.
        descriptors->erase(it);
        added = true;
        break;
      } else {
        ++it;
      }
    }
  } while (!descriptors->empty() && added);

  unused_ids->reserve(descriptors->size());
  for (const auto& desc : *descriptors) {
    unused_ids->push_back(desc.id);
  }
}

// Accepts unsorted list of chains and returns sorted list with the order of GPU
// task execution.
std::list<FusionSequence> SortChains(
    const std::vector<ValueId>& graph_input_ids,
    std::list<FusionSequence>* chains) {
  std::list<FusionSequence> sorted_chains;
  while (!chains->empty()) {
    // Collect ready buffers.
    std::vector<ValueId> ready_buffer_ids;
    ready_buffer_ids.reserve(graph_input_ids.size() + sorted_chains.size());
    ready_buffer_ids.insert(ready_buffer_ids.begin(), graph_input_ids.begin(),
                            graph_input_ids.end());
    for (auto& chain : sorted_chains) {
      ready_buffer_ids.push_back(chain.back().dst_tensors_ids[0]);
    }

    for (auto it = chains->begin(); it != chains->end();) {
      const FusionSequence& chain = *it;

      // If the input is also is the output in the same chain - eliminate
      // because it used internally inside sthis chain only.
      std::vector<ValueId> elements_output_buffer_ids;
      elements_output_buffer_ids.reserve(chain.size());
      for (const auto& element : chain) {
        elements_output_buffer_ids.push_back(element.dst_tensors_ids[0]);
      }

      // Collect all inputs also for linked operations.
      std::vector<ValueId> elements_input_buffer_ids;
      for (const auto& element : chain) {
        for (const auto& id : element.src_tensors_ids) {
          if (!Contains(elements_output_buffer_ids, id)) {
            elements_input_buffer_ids.push_back(id);
          }
        }
      }

      if (Contains(ready_buffer_ids, elements_input_buffer_ids)) {
        // All input buffers for all elements of this chain are ready.
        sorted_chains.push_back(chain);
        it = chains->erase(it);
      } else {
        ++it;
      }
    }
  }
  return sorted_chains;
}

// If a graph structure contains unused outputs then it can lead to unused
// operations and unused input buffers. It's not an error but some sort of
// warning.
std::vector<ValueId> GetUsedInputBufferIds(
    const std::list<FusionSequence>& sorted_chains) {
  // Match requested outputs with all outputs and intermediate buffers.
  std::vector<ValueId> output_and_intermediate_ids;
  output_and_intermediate_ids.reserve(sorted_chains.size());
  std::set<ValueId> input_and_intermediate_ids;
  for (auto it = sorted_chains.begin(); it != sorted_chains.end(); ++it) {
    output_and_intermediate_ids.push_back(it->back().dst_tensors_ids[0]);
    for (const auto& id : it->front().src_tensors_ids) {
      input_and_intermediate_ids.insert(id);
    }
  }
  std::vector<ValueId> input_ids;
  for (ValueId id : input_and_intermediate_ids) {
    if (!Contains(output_and_intermediate_ids, id)) {
      input_ids.push_back(id);
    }
  }
  return input_ids;
}

// If a buffer is requested as output from the graph but the graph structure
// can't provide this buffer by output (can't deduct), that means the graph
// structure is incorrect.
std::vector<ValueId> GetMissingOutputBufferIds(
    const std::vector<ValueId>& output_ids,
    const std::list<FusionSequence>& sorted_chains) {
  // Match requested outputs with all output and intermediate buffers.
  std::vector<ValueId> output_and_intermediate_ids;
  output_and_intermediate_ids.reserve(sorted_chains.size());
  for (auto it = sorted_chains.begin(); it != sorted_chains.end(); ++it) {
    output_and_intermediate_ids.push_back(it->back().dst_tensors_ids[0]);
  }
  std::vector<ValueId> missing_output_ids;
  for (ValueId id : output_ids) {
    if (!Contains(output_and_intermediate_ids, id)) {
      missing_output_ids.push_back(id);
    }
  }
  return missing_output_ids;
}

// Graph may contain leafs with outputs that are not requested. It wastes GPU
// computations.
std::vector<ValueId> DeductOutputBufferIds(
    const std::vector<ValueId>& output_ids,
    const std::list<FusionSequence>& sorted_chains) {
  std::vector<ValueId> extra_output_ids;
  // Detect all unused output buffers - all outputs.
  for (auto it1 = sorted_chains.begin(); it1 != sorted_chains.end(); ++it1) {
    bool found_as_input = false;
    for (auto it2 = sorted_chains.begin(); it2 != sorted_chains.end(); ++it2) {
      if (it1 != it2) {
        std::vector<ValueId> input_ids;
        for (const auto& element : *it2) {
          for (const auto& id : element.src_tensors_ids) {
            input_ids.push_back(id);
          }
        }
        if (Contains(input_ids, it1->back().dst_tensors_ids[0])) {
          found_as_input = true;
          break;
        }
      }
    }
    if (!found_as_input) {
      if (!Contains(output_ids, it1->back().dst_tensors_ids[0])) {
        extra_output_ids.push_back(it1->back().dst_tensors_ids[0]);
      }
    }
  }
  return extra_output_ids;
}

// Delete all unused task descriptors that have non-requested outputs.
// TODO(chirkov): delete not the whole chain but only the last element, then
// others.
std::vector<int> DeleteUnusedTasks(const std::vector<ValueId>& output_ids,
                                   std::list<FusionSequence>* chains) {
  std::vector<int> unused_operations;
  for (auto it1 = chains->rbegin(); it1 != chains->rend();) {
    // Don't delete if output is requested.
    if (Contains(output_ids, it1->back().dst_tensors_ids[0])) {
      ++it1;
      continue;
    }

    // Don't delete if some operation uses the output.
    bool output_used = false;
    for (auto it2 = chains->rbegin(); it2 != chains->rend(); ++it2) {
      std::vector<ValueId> input_ids;
      for (const auto& element : *it2) {
        for (const auto& id : element.src_tensors_ids) {
          input_ids.push_back(id);
        }
      }
      if (Contains(input_ids, it1->back().dst_tensors_ids[0])) {
        output_used = true;
        break;
      }
    }
    if (output_used) {
      ++it1;
      continue;
    }
    // Delete if not used.
    unused_operations.push_back(it1->back().id);
    it1 = decltype(it1){chains->erase(std::next(it1).base())};
  }
  return unused_operations;
}

// Returns unused input buffer IDs.
void RemoveInputProxies(std::list<FusionSequence>* chains) {
  // Remove input proxy and sort items.
  for (auto it = chains->begin(); it != chains->end();) {
    auto& chain = *it;
    // Remove input proxy-operations.
    if (chain.front().src_tensors_ids.empty()) {
      chain.erase(chain.begin());
    }
    if (chain.empty()) {
      // Input proxy operation has been deleted and the chain is empty due to
      // unused input buffer.
      it = chains->erase(it);
    } else {
      ++it;
    }
  }
}

NodeDescriptor NonLinkableStub(int operation_id, ValueId input_id,
                               ValueId output_id) {
  auto desc = std::make_shared<ComputeTaskDescriptor>();
  desc->is_linkable = false;
  desc->shader_source = R"(
    #include <metal_stdlib>
    using namespace metal;
    $0
    kernel void ComputeFunction(
                                $1
                                uint3 gid[[thread_position_in_grid]]) {
      if (int(gid.x) >= size.x || int(gid.y) >= size.y) {
        return;
      }
      const int linear_index = (gid.z * size.y + gid.y) * size.x + gid.x;
      FLT4 value = src_tensor[linear_index];
      $2
      dst_tensor[linear_index] = value;
    }
  )";

  desc->AddSrcTensor("src_tensor", {});
  desc->AddDstTensor("dst_tensor", {});

  desc->uniform_buffers = {
      {"constant int2& size",
       [](const std::vector<BHWC>& src_shapes,
          const std::vector<BHWC>& dst_shapes) {
         return GetByteBuffer(
             std::vector<int>{src_shapes[0].w, src_shapes[0].h});
       }},
  };

  desc->resize_function = [](const std::vector<BHWC>& src_shapes,
                             const std::vector<BHWC>& dst_shapes) {
    uint3 groups_size{16, 16, 1};
    uint3 groups_count{DivideRoundUp(dst_shapes[0].w, groups_size.x),
                       DivideRoundUp(dst_shapes[0].h, groups_size.y),
                       DivideRoundUp(dst_shapes[0].c, 4)};
    return std::make_pair(groups_size, groups_count);
  };

  NodeDescriptor node_desc;
  node_desc.task = desc;
  node_desc.id = operation_id;
  node_desc.src_tensors_ids = {input_id};
  node_desc.dst_tensors_ids = {output_id};
  return node_desc;
}

NodeDescriptor FuseChain(const FusionSequence& chain) {
  NodeDescriptor node_desc;
  auto fused_descriptor = std::make_shared<ComputeTaskDescriptor>();
  FusionSequence sequence;
  if (chain.front().task->is_linkable) {
    // The first task is linkable so it contains only linkable code. Insert
    // unlinkable meta-task with remaining shader code.
    sequence.push_back(NonLinkableStub(-1, chain.front().src_tensors_ids[0],
                                       chain.front().src_tensors_ids[0]));
  }
  sequence.insert(sequence.end(), chain.begin(), chain.end());

  // Count buffers to calculate proper indices then.
  int num_outputs = 1;
  int num_inputs = 0;
  int num_immutables = 0;
  bool invalid_id = true;
  ValueId fused_id;
  for (const auto& desc : sequence) {
    for (const auto& id : desc.src_tensors_ids) {
      if (invalid_id || id != fused_id) {
        num_inputs++;
      }
    }
    fused_id = desc.dst_tensors_ids[0];
    invalid_id = false;
    num_immutables += desc.task->immutable_buffers.size();
  }

  int output_index = 0;
  int input_index = num_outputs;
  int immutable_index = num_outputs + num_inputs;
  int uniform_index = num_outputs + num_inputs + num_immutables;

  int function_index = 0;
  std::string function_code;
  std::string buffer_declarations;
  std::string call_code;
  invalid_id = true;
  for (const auto& desc : sequence) {
    if (desc.task->is_linkable) {
      function_code +=
          absl::Substitute(desc.task->shader_source, function_index) + "\n";
    } else {
      // Declare output buffer only for the first unlinkable task.
      buffer_declarations +=
          desc.task->dst_tensors_names[0] + "[[buffer(0)]],\n";
      output_index++;
    }

    std::string call_arguments;
    for (int i = 0; i < desc.task->src_tensors_names.size(); ++i) {
      if (invalid_id || desc.src_tensors_ids[i] != fused_id) {
        std::string index = std::to_string(input_index);
        std::string name = (desc.task->is_linkable ? (" buffer" + index) : "");
        buffer_declarations += desc.task->src_tensors_names[i] + name +
                               "[[buffer(" + index + ")]],\n";
        call_arguments += ", buffer" + index;
        input_index++;
        fused_descriptor->AddSrcTensor("", {});
        node_desc.src_tensors_ids.push_back(desc.src_tensors_ids[i]);
      }
    }
    // We have an output id that is the input for the next task.
    fused_id = desc.dst_tensors_ids[0];
    invalid_id = false;

    for (const auto& buffer : desc.task->immutable_buffers) {
      std::string index = std::to_string(immutable_index);
      std::string name = (desc.task->is_linkable ? (" buffer" + index) : "");
      buffer_declarations +=
          buffer.declaration + name + "[[buffer(" + index + ")]],\n";
      call_arguments += ", buffer" + index;
      immutable_index++;
      fused_descriptor->immutable_buffers.push_back(buffer);
    }

    for (const auto& buffer : desc.task->uniform_buffers) {
      std::string index = std::to_string(uniform_index);
      std::string name = (desc.task->is_linkable ? (" buffer" + index) : "");
      buffer_declarations +=
          buffer.declaration + name + "[[buffer(" + index + ")]],\n";
      call_arguments += ", buffer" + index;
      uniform_index++;
      fused_descriptor->uniform_buffers.push_back({"", buffer.data_function});
    }

    if (desc.task->is_linkable) {
      call_code +=
          absl::Substitute("value = linkable$0(value, linear_index, gid$1);\n",
                           function_index, call_arguments);
      function_index++;
    }
  }
  fused_descriptor->args = std::move(sequence.front().task->args);

  auto& non_linkable = sequence.front();
  fused_descriptor->shader_source =
      absl::Substitute(non_linkable.task->shader_source, function_code + "$0",
                       buffer_declarations + "$1", call_code);
  fused_descriptor->AddDstTensor("", {});
  fused_descriptor->resize_function = non_linkable.task->resize_function;
  node_desc.dst_tensors_ids = {fused_id};
  node_desc.task = fused_descriptor;
  // The id of fused descriptor is the id of the first descriptor in the list.
  node_desc.id = chain.front().id;
  for (const auto& desc : sequence) {
    node_desc.description += desc.description + "_";
  }

  return node_desc;
}

}  // namespace

absl::Status ValidateOptimizeModel(const std::vector<ValueId>& input_buffers,
                                   const std::vector<ValueId>& output_buffers,
                                   const CompiledModel& input_model,
                                   CompiledModel* output_model) {
  std::list<NodeDescriptor> input;
  input.insert(input.end(), input_model.nodes.begin(), input_model.nodes.end());
  OptimizationInfo info;
  info.operations_count = static_cast<int>(input.size());

  // A chain is a sequence of fusable operations. All internal outputs are
  // consumed with the next element of the chain. The last element of each chain
  // contains outputs which are ready to be used as inputs. if a chain can't be
  // extended with linkable element then new chain is created.
  std::list<FusionSequence> unsorted_chains;
  BuildFusableChains(input_buffers, output_buffers, &input, &unsorted_chains,
                     &info.unused_operations);

  RemoveInputProxies(&unsorted_chains);
  std::list<FusionSequence> sorted_chains =
      SortChains(input_buffers, &unsorted_chains);

  info.extra_output_buffer_ids =
      DeductOutputBufferIds(output_buffers, sorted_chains);
  info.unused_operations = DeleteUnusedTasks(output_buffers, &sorted_chains);
  info.input_buffer_ids = GetUsedInputBufferIds(sorted_chains);
  // find provided input buffers that has not being used
  for (ValueId id : input_buffers) {
    if (!Contains(info.input_buffer_ids, id)) {
      info.unused_input_buffer_ids.push_back(id);
    }
  }
  info.missing_output_buffer_ids =
      GetMissingOutputBufferIds(output_buffers, sorted_chains);
  info.gpu_tasks_count = static_cast<int>(sorted_chains.size());
  if (sorted_chains.empty()) {
    const std::string message =
        "No valid operations in the graph.\nInput operations count " +
        std::to_string(info.operations_count) + "\nUnused operations " +
        std::to_string(info.unused_operations.size()) + "\nUnused inputs " +
        std::to_string(info.unused_input_buffer_ids.size()) +
        "\nMissing output buffers " +
        std::to_string(info.missing_output_buffer_ids.size());
    return absl::InternalError(message);
  }
  for (const auto& chain : sorted_chains)
    output_model->nodes.push_back(FuseChain(chain));
  output_model->tensor_shapes = input_model.tensor_shapes;
  return absl::OkStatus();
}

}  // namespace metal
}  // namespace gpu
}  // namespace tflite
