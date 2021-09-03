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

#include "tensorflow/lite/delegates/gpu/common/task/tensor_linear_desc.h"

#include "absl/strings/str_cat.h"
#include "tensorflow/lite/delegates/gpu/common/data_type.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/types.h"
#include "tensorflow/lite/delegates/gpu/common/util.h"

namespace tflite {
namespace gpu {

void TensorLinearDescriptor::Release() { data.clear(); }

GPUResources TensorLinearDescriptor::GetGPUResources() const {
  GPUResources resources;
  resources.ints.push_back("length");
  if (storage_type == LinearStorageType::BUFFER) {
    GPUBufferDescriptor desc;
    desc.data_type = element_type;
    desc.access_type = access_type_;
    desc.element_size = 4;
    desc.memory_type = memory_type;
    resources.buffers.push_back({"buffer", desc});
  } else {
    GPUImage2DDescriptor desc;
    desc.data_type = element_type;
    desc.access_type = access_type_;
    resources.images2d.push_back({"tex2d", desc});
  }
  return resources;
}

absl::Status TensorLinearDescriptor::PerformSelector(
    const std::string& selector, const std::vector<std::string>& args,
    const std::vector<std::string>& template_args, std::string* result) const {
  if (selector == "Length") {
    *result = "length";
    return absl::OkStatus();
  } else if (selector == "Read") {
    return PerformReadSelector(args, result);
  } else if (selector == "GetPtr") {
    if (storage_type != LinearStorageType::BUFFER) {
      return absl::InvalidArgumentError(
          "GetPtr selector supported for LinearStorageType::BUFFER only.");
    }
    *result = "buffer";
    return absl::OkStatus();
  } else {
    return absl::NotFoundError(absl::StrCat(
        "TensorLinearDescriptor don't have selector with name - ", selector));
  }
}

absl::Status TensorLinearDescriptor::PerformReadSelector(
    const std::vector<std::string>& args, std::string* result) const {
  if (args.size() != 1) {
    return absl::NotFoundError(
        absl::StrCat("TensorLinearDescriptor Read require one argument, but ",
                     args.size(), " was passed"));
  }
  if (storage_type == LinearStorageType::BUFFER) {
    *result = absl::StrCat("buffer[", args[0], "]");
    return absl::OkStatus();
  } else {
    const std::string read =
        element_type == DataType::FLOAT16 ? "read_imageh" : "read_imagef";
    *result = absl::StrCat(read, "(tex2d, smp_none, (int2)(", args[0], ", 0))");
    return absl::OkStatus();
  }
}

void TensorLinearDescriptor::UploadLinearData(
    const tflite::gpu::Tensor<Linear, DataType::FLOAT32>& src,
    int aligned_size) {
  size = aligned_size == 0 ? DivideRoundUp(src.shape.v, 4) : aligned_size;
  if (element_type == DataType::FLOAT32) {
    data.resize(size * sizeof(float) * 4);
    float* gpu_data = reinterpret_cast<float*>(data.data());
    for (int i = 0; i < size * 4; ++i) {
      if (i < src.shape.v) {
        gpu_data[i] = src.data[i];
      } else {
        gpu_data[i] = 0.0f;
      }
    }
  } else {
    data.resize(size * sizeof(half) * 4);
    half* gpu_data = reinterpret_cast<half*>(data.data());
    for (int i = 0; i < size * 4; ++i) {
      if (i < src.shape.v) {
        gpu_data[i] = src.data[i];
      } else {
        gpu_data[i] = 0.0f;
      }
    }
  }
}

}  // namespace gpu
}  // namespace tflite
