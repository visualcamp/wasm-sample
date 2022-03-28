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

#include "absl/memory/memory.h"
#include "tensorflow/lite/delegates/gpu/common/operations.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/tasks/conv_buffer_1x1.h"
#include "tensorflow/lite/delegates/gpu/common/tasks/conv_powervr.h"
#include "tensorflow/lite/delegates/gpu/common/tasks/fully_connected.h"

namespace tflite {
namespace gpu {

std::unique_ptr<GPUOperation> SelectFullyConnectedGeneric(
    const FullyConnectedAttributes& attr, const GpuInfo& gpu_info,
    const OperationDef& op_def, int batch_size) {
  if (op_def.IsBatchSupported()) {
    BHWC dst_shape = BHWC(batch_size, 1, 1, attr.weights.shape.o);
    ConvPowerVR conv = CreateConvPowerVR(gpu_info, op_def, attr, &dst_shape);
    return absl::make_unique<ConvPowerVR>(std::move(conv));
  } else {
    FullyConnected fc = CreateFullyConnected(gpu_info, op_def, attr);
    return absl::make_unique<FullyConnected>(std::move(fc));
  }
}

std::unique_ptr<GPUOperation> SelectFullyConnectedAdreno(
    const FullyConnectedAttributes& attr, const GpuInfo& gpu_info,
    const OperationDef& op_def, int batch_size) {
  if (op_def.IsBatchSupported()) {
    BHWC dst_shape = BHWC(batch_size, 1, 1, attr.weights.shape.o);
    ConvPowerVR conv = CreateConvPowerVR(gpu_info, op_def, attr, &dst_shape);
    return absl::make_unique<ConvPowerVR>(std::move(conv));
  } else {
    FullyConnected fc = CreateFullyConnected(gpu_info, op_def, attr);
    return absl::make_unique<FullyConnected>(std::move(fc));
  }
}

std::unique_ptr<GPUOperation> SelectFullyConnectedPowerVR(
    const FullyConnectedAttributes& attr, const GpuInfo& gpu_info,
    const OperationDef& op_def, int batch_size) {
  if (op_def.IsBatchSupported()) {
    ConvPowerVR conv = CreateConvPowerVR(gpu_info, op_def, attr);
    return absl::make_unique<ConvPowerVR>(std::move(conv));
  } else {
    FullyConnected fc = CreateFullyConnected(gpu_info, op_def, attr);
    return absl::make_unique<FullyConnected>(std::move(fc));
  }
}

std::unique_ptr<GPUOperation> SelectFullyConnectedMali(
    const FullyConnectedAttributes& attr, const GpuInfo& gpu_info,
    const OperationDef& op_def, int batch_size) {
  if (op_def.IsBatchSupported()) {
    if (op_def.src_tensors[0].storage_type == TensorStorageType::BUFFER) {
      ConvBuffer1x1 conv = CreateConvBuffer1x1(gpu_info, op_def, attr);
      return absl::make_unique<ConvBuffer1x1>(std::move(conv));
    } else {
      BHWC dst_shape = BHWC(batch_size, 1, 1, attr.weights.shape.o);
      ConvPowerVR conv =
          CreateConvPowerVR(gpu_info, op_def, attr, &dst_shape);
      return absl::make_unique<ConvPowerVR>(std::move(conv));
    }
  } else {
    FullyConnected fc = CreateFullyConnected(gpu_info, op_def, attr);
    return absl::make_unique<FullyConnected>(std::move(fc));
  }
}

std::unique_ptr<GPUOperation> SelectFullyConnected(
    const FullyConnectedAttributes& attr, const GpuInfo& gpu_info,
    const OperationDef& op_def, int batch_size) {
  if (gpu_info.IsAdreno()) {
    return SelectFullyConnectedAdreno(attr, gpu_info, op_def, batch_size);
  } else if (gpu_info.IsPowerVR() || gpu_info.IsAMD() ||
             gpu_info.IsNvidia() || gpu_info.IsIntel()) {
    return SelectFullyConnectedPowerVR(attr, gpu_info, op_def, batch_size);
  } else if (gpu_info.IsMali()) {
    return SelectFullyConnectedMali(attr, gpu_info, op_def, batch_size);
  } else {
    return SelectFullyConnectedGeneric(attr, gpu_info, op_def, batch_size);
  }
}

}  // namespace gpu
}  // namespace tflite
