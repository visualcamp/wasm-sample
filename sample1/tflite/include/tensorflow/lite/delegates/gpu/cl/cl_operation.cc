/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/lite/delegates/gpu/cl/cl_operation.h"

namespace tflite {
namespace gpu {
namespace cl {
namespace {
int3 GetWorkGroupsCount(int grid_dimension, const int3& grid_size,
                        const int3& work_group_size,
                        const int3& work_group_launch_order) {
  int3 work_groups_count;
  if (grid_dimension == 1) {
    work_groups_count.x = DivideRoundUp(grid_size.x, work_group_size.x);
    work_groups_count.y = 1;
    work_groups_count.z = 1;
  } else if (grid_dimension == 2) {
    int3 wgs;
    wgs.x = DivideRoundUp(grid_size.x, work_group_size.x);
    wgs.y = DivideRoundUp(grid_size.y, work_group_size.y);
    work_groups_count.x = wgs[work_group_launch_order[0]];
    work_groups_count.y = wgs[work_group_launch_order[1]];
    work_groups_count.z = 1;
  } else {  // grid_dimension == 3
    int3 wgs;
    wgs.x = DivideRoundUp(grid_size.x, work_group_size.x);
    wgs.y = DivideRoundUp(grid_size.y, work_group_size.y);
    wgs.z = DivideRoundUp(grid_size.z, work_group_size.z);
    work_groups_count.x = wgs[work_group_launch_order[0]];
    work_groups_count.y = wgs[work_group_launch_order[1]];
    work_groups_count.z = wgs[work_group_launch_order[2]];
  }
  return work_groups_count;
}
}  // namespace

ClOperation::ClOperation(ClOperation&& operation)
    : operation_(std::move(operation.operation_)),
      kernel_(std::move(operation.kernel_)),
      cl_args_(std::move(operation.cl_args_)) {}

ClOperation& ClOperation::operator=(ClOperation&& operation) {
  if (this != &operation) {
    operation_ = std::move(operation.operation_);
    kernel_ = std::move(operation.kernel_);
    cl_args_ = std::move(operation.cl_args_);
  }
  return *this;
}

absl::Status ClOperation::AddOperation(ClOperation* operation) {
  return operation_->AddOperation(operation->operation_.get());
}

absl::Status ClOperation::UpdateParams() {
  for (int i = 0; i < operation_->src_tensors_names_.size(); ++i) {
    const auto* cl_spatial_tensor =
        dynamic_cast<const Tensor*>(operation_->src_[i]);
    if (!cl_spatial_tensor) {
      return absl::InvalidArgumentError("Expected CLSpatialTensor.");
    }
    RETURN_IF_ERROR(cl_args_.SetObjectRef(operation_->src_tensors_names_[i],
                                          cl_spatial_tensor));
  }
  for (int i = 0; i < operation_->dst_tensors_names_.size(); ++i) {
    const auto* cl_spatial_tensor =
        dynamic_cast<const Tensor*>(operation_->dst_[i]);
    if (!cl_spatial_tensor) {
      return absl::InvalidArgumentError("Expected CLSpatialTensor.");
    }
    RETURN_IF_ERROR(cl_args_.SetObjectRef(operation_->dst_tensors_names_[i],
                                          cl_spatial_tensor));
  }
  RETURN_IF_ERROR(operation_->BindArguments(&cl_args_));
  operation_->grid_size_ = operation_->GetGridSize();
  operation_->work_groups_count_ = GetWorkGroupsCount(
      operation_->grid_dimension_, operation_->grid_size_,
      operation_->work_group_size_, operation_->work_group_launch_order_);
  return absl::OkStatus();
}

absl::Status ClOperation::Compile(const CreationContext& creation_context) {
  operation_->AssembleCode(creation_context.GetGpuInfo());
  RETURN_IF_ERROR(cl_args_.Init(
      creation_context.GetGpuInfo(),
      {{operation_->dst_tensors_names_[0], operation_->elementwise_code_}},
      creation_context.context, &operation_->args_, &operation_->code_));
  RETURN_IF_ERROR(creation_context.cache->GetOrCreateCLKernel(
      operation_->code_, "main_function", operation_->compiler_options_,
      *creation_context.context, *creation_context.device, &kernel_));
  return operation_->PostCompileCheck(creation_context.GetGpuInfo(),
                                      kernel_.info_);
}

absl::Status ClOperation::CompileDeserialized(
    const CreationContext& creation_context) {
  RETURN_IF_ERROR(cl_args_.Init(creation_context.GetGpuInfo(),
                                &operation_->args_, creation_context.context));
  return creation_context.cache->GetOrCreateCLKernel(
      operation_->code_, "main_function", operation_->compiler_options_,
      *creation_context.context, *creation_context.device, &kernel_);
}

absl::Status ClOperation::Tune(TuningType tuning_type, const GpuInfo& gpu_info,
                               ProfilingCommandQueue* profiling_queue) {
  std::vector<int3> possible_work_groups;
  operation_->GetPossibleKernelWorkGroups(tuning_type, gpu_info, kernel_.info_,
                                          &possible_work_groups);
  if (possible_work_groups.empty()) {
    return absl::NotFoundError(
        "Can not found work_group size to launch kernel");
  }
  if (possible_work_groups.size() == 1) {
    operation_->work_group_size_ = possible_work_groups[0];
    operation_->work_groups_count_ = GetWorkGroupsCount(
        operation_->grid_dimension_, operation_->grid_size_,
        operation_->work_group_size_, operation_->work_group_launch_order_);
    return absl::OkStatus();
  } else {
    std::vector<int3> work_groups_count(possible_work_groups.size());
    for (int i = 0; i < work_groups_count.size(); ++i) {
      work_groups_count[i] = GetWorkGroupsCount(
          operation_->grid_dimension_, operation_->grid_size_,
          possible_work_groups[i], operation_->work_group_launch_order_);
    }
    RETURN_IF_ERROR(cl_args_.Bind(kernel_.kernel()));
    int best_work_group_index;
    RETURN_IF_ERROR(profiling_queue->GetBestWorkGroupIndex(
        kernel_, gpu_info, work_groups_count, possible_work_groups,
        &best_work_group_index));
    operation_->work_group_size_ = possible_work_groups[best_work_group_index];
    operation_->work_groups_count_ = GetWorkGroupsCount(
        operation_->grid_dimension_, operation_->grid_size_,
        operation_->work_group_size_, operation_->work_group_launch_order_);
    return absl::OkStatus();
  }
}

}  // namespace cl
}  // namespace gpu
}  // namespace tflite
