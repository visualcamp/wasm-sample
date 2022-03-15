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

#include "tensorflow/lite/delegates/gpu/metal/metal_spatial_tensor.h"

#include "tensorflow/lite/delegates/gpu/common/task/buffer_desc.h"

namespace tflite {
namespace gpu {
namespace metal {
namespace {

absl::Status AllocateTensorMemory(id<MTLDevice> device, const BHWDC& shape,
                                  const TensorDescriptor& descriptor,
                                  const void* data_ptr, id<MTLBuffer>* buffer) {
  const int slices = DivideRoundUp(shape.c, 4);
  switch (descriptor.storage_type) {
    case TensorStorageType::BUFFER:{
      const size_t data_size = shape.b * shape.w * shape.h * shape.d * slices *
                               4 * SizeOf(descriptor.data_type);
      if (data_ptr) {
        *buffer = [device newBufferWithBytes:data_ptr
                                      length:data_size
                                     options:MTLResourceStorageModeShared];
      } else {
        *buffer = [device newBufferWithLength:data_size
                                      options:MTLResourceStorageModeShared];
      }
      return absl::OkStatus();
    }
    case TensorStorageType::IMAGE_BUFFER:
    case TensorStorageType::TEXTURE_2D:
    case TensorStorageType::TEXTURE_3D:
    case TensorStorageType::TEXTURE_ARRAY:
    case TensorStorageType::SINGLE_TEXTURE_2D:
    default:
      return absl::InternalError("Unsupported tensor storage type");
  }
}

absl::Status CreateTensor(id<MTLDevice> device, const BHWDC& shape,
                          const TensorDescriptor& descriptor, id<MTLBuffer> buffer,
                          MetalSpatialTensor* result) {
  const bool memory_owner = buffer == nullptr;
  if (memory_owner) {
    RETURN_IF_ERROR(
        AllocateTensorMemory(device, shape, descriptor, nullptr, &buffer));
  }

  *result = MetalSpatialTensor(buffer, memory_owner, shape, descriptor);
  return absl::OkStatus();
}
}  // namespace

MetalSpatialTensor::MetalSpatialTensor(id<MTLBuffer> buffer, bool memory_owner, const BHWC& shape,
               const TensorDescriptor& descriptor)
    : memory_(buffer),
      memory_owner_(memory_owner),
      shape_(shape.b, shape.h, shape.w, 1, shape.c),
      descriptor_(descriptor) {}

MetalSpatialTensor::MetalSpatialTensor(id<MTLBuffer> buffer, bool memory_owner, const BHWDC& shape,
               const TensorDescriptor& descriptor)
    : memory_(buffer),
      memory_owner_(memory_owner),
      shape_(shape),
      descriptor_(descriptor) {}

MetalSpatialTensor::MetalSpatialTensor(MetalSpatialTensor&& tensor)
    : memory_(tensor.memory_),
      memory_owner_(tensor.memory_owner_),
      shape_(tensor.shape_),
      descriptor_(tensor.descriptor_) {
  tensor.memory_ = nullptr;
}

MetalSpatialTensor& MetalSpatialTensor::operator=(MetalSpatialTensor&& tensor) {
  if (this != &tensor) {
    Release();
    std::swap(memory_, tensor.memory_);
    std::swap(memory_owner_, tensor.memory_owner_);
    std::swap(shape_, tensor.shape_);
    std::swap(descriptor_, tensor.descriptor_);
  }
  return *this;
}

void MetalSpatialTensor::Release() {
  if (memory_owner_ && memory_) {
    memory_ = nullptr;
  }
}

absl::Status MetalSpatialTensor::GetGPUResources(const GPUObjectDescriptor* obj_ptr,
                                     GPUResourcesWithValue* resources) const {
  const auto* buffer_desc = dynamic_cast<const BufferDescriptor*>(obj_ptr);
  if (buffer_desc) {
    if (descriptor_.storage_type != TensorStorageType::BUFFER) {
      return absl::InvalidArgumentError(
          "Tensor can be used with BufferDescriptor only wtih "
          "TensorStorageType::BUFFER.");
    }
    resources->buffers.push_back({"buffer", memory_});
    return absl::OkStatus();
  }
  const auto* tensor_desc = dynamic_cast<const TensorDescriptor*>(obj_ptr);
  if (!tensor_desc) {
    return absl::InvalidArgumentError("Expected TensorDescriptor on input.");
  }
  if (descriptor_.HasAxis(Axis::WIDTH)) {
    resources->ints.push_back({"width", Width()});
    resources->ints.push_back({"width_div2", Width() / 2});
    resources->ints.push_back({"width_div4", Width() / 4});
    resources->ints.push_back({"width_batched", Width() * Batch()});
    resources->ints.push_back({"width_batched_div2", Width() * Batch() / 2});
    resources->ints.push_back({"width_batched_div4", Width() * Batch() / 4});
  }
  if (descriptor_.HasAxis(Axis::HEIGHT)) {
    resources->ints.push_back({"height", Height()});
  }
  if (descriptor_.HasAxis(Axis::CHANNELS)) {
    resources->ints.push_back({"slices", Slices()});
    resources->ints.push_back({"channels", Channels()});
  }
  if (descriptor_.HasAxis(Axis::BATCH)) {
    resources->ints.push_back({"batch", Batch()});
  }
  if (descriptor_.HasAxis(Axis::DEPTH)) {
    resources->ints.push_back({"depth", Depth()});
  }

  if (descriptor_.storage_type == TensorStorageType::BUFFER) {
    resources->buffers.push_back({"buffer", memory_});
  } else {
    return absl::UnimplementedError(
        "Only TensorStorageType::BUFFER supported.");
  }

  return absl::OkStatus();
}

int3 MetalSpatialTensor::GetFullTensorRegion() const {
  switch (descriptor_.storage_type) {
    case TensorStorageType::BUFFER:
    case TensorStorageType::TEXTURE_ARRAY:
    case TensorStorageType::TEXTURE_3D:
    case TensorStorageType::IMAGE_BUFFER:
      return {shape_.w * shape_.b, shape_.h, shape_.d * Slices()};
    case TensorStorageType::TEXTURE_2D:
      return {shape_.w * shape_.b * shape_.d, shape_.h * Slices(), 1};
    case TensorStorageType::SINGLE_TEXTURE_2D:
      return {shape_.w * shape_.b * shape_.d, shape_.h, 1};
    case TensorStorageType::UNKNOWN:
      return {-1, -1, -1};
  }
}

absl::Status MetalSpatialTensor::IsValid(const BHWC& shape) const {
  if (shape.b != shape_.b) {
    return absl::InvalidArgumentError(
        "Shape batch does not match tensor batch");
  }
  if (shape.w != shape_.w) {
    return absl::InvalidArgumentError(
        "Shape width does not match tensor width");
  }
  if (shape.h != shape_.h) {
    return absl::InvalidArgumentError(
        "Shape height does not match tensor height");
  }
  if (shape.c != shape_.c) {
    return absl::InvalidArgumentError(
        "Shape channels does not match tensor channels");
  }
  return absl::OkStatus();
}

absl::Status MetalSpatialTensor::IsValid(const BHWDC& shape) const {
  if (shape.b != shape_.b) {
    return absl::InvalidArgumentError(
        "Shape batch does not match tensor batch");
  }
  if (shape.w != shape_.w) {
    return absl::InvalidArgumentError(
        "Shape width does not match tensor width");
  }
  if (shape.h != shape_.h) {
    return absl::InvalidArgumentError(
        "Shape height does not match tensor height");
  }
  if (shape.d != shape_.d) {
    return absl::InvalidArgumentError(
        "Shape depth does not match tensor depth");
  }
  if (shape.c != shape_.c) {
    return absl::InvalidArgumentError(
        "Shape channels does not match tensor channels");
  }
  return absl::OkStatus();
}

uint64_t MetalSpatialTensor::GetMemorySizeInBytes() const {
  const int flt_size = SizeOf(descriptor_.data_type);
  const int flt4_size = 4 * flt_size;
  switch (descriptor_.storage_type) {
    case TensorStorageType::BUFFER:
    case TensorStorageType::IMAGE_BUFFER:
    case TensorStorageType::TEXTURE_ARRAY:
    case TensorStorageType::TEXTURE_2D:
    case TensorStorageType::TEXTURE_3D:
      return flt4_size * shape_.b * shape_.w * shape_.h * shape_.d * Slices();
    case TensorStorageType::SINGLE_TEXTURE_2D:
      return flt_size * shape_.w * shape_.h * shape_.c * shape_.b * shape_.d;
    default:
      return 0;
  }
}

int MetalSpatialTensor::GetAlignedChannels() const {
  return descriptor_.storage_type == TensorStorageType::SINGLE_TEXTURE_2D
             ? shape_.c
             : AlignByN(shape_.c, 4);
}

absl::Status MetalSpatialTensor::WriteDataBHWDC(absl::Span<const float> in) {
  void* data_ptr = nullptr;
  const int aligned_channels = GetAlignedChannels();
  const int elements_count =
      shape_.b * shape_.w * shape_.h * shape_.d * aligned_channels;

  const size_t data_size = elements_count * SizeOf(descriptor_.data_type);
  std::vector<float> data_f;
  std::vector<half> data_h;
  if (descriptor_.data_type == DataType::FLOAT32) {
    data_f.resize(elements_count);
    data_ptr = data_f.data();
    DataFromBHWDC(in, shape_, descriptor_,
                  absl::MakeSpan(data_f.data(), data_f.size()));
  } else {
    data_h.resize(elements_count);
    data_ptr = data_h.data();
    DataFromBHWDC(in, shape_, descriptor_,
                  absl::MakeSpan(data_h.data(), data_h.size()));
  }

  switch (descriptor_.storage_type) {
    case TensorStorageType::BUFFER:
      std::memcpy([memory_ contents], data_ptr, data_size);
      break;
    case TensorStorageType::IMAGE_BUFFER:
    case TensorStorageType::TEXTURE_ARRAY:
    case TensorStorageType::TEXTURE_2D:
    case TensorStorageType::TEXTURE_3D:
    case TensorStorageType::SINGLE_TEXTURE_2D:
    default:
      return absl::InternalError("Unsupported tensor storage type");
  }

  return absl::OkStatus();
}

absl::Status MetalSpatialTensor::WriteData(const TensorFloat32& src) {
  RETURN_IF_ERROR(IsValid(src.shape));
  return WriteDataBHWDC(absl::MakeConstSpan(src.data));
}

absl::Status MetalSpatialTensor::WriteData(
    const tflite::gpu::Tensor<Linear, DataType::FLOAT32>& src) {
  return WriteDataBHWDC(absl::MakeConstSpan(src.data));
}

absl::Status MetalSpatialTensor::WriteData(
    const tflite::gpu::Tensor<HWC, DataType::FLOAT32>& src) {
  return WriteDataBHWDC(absl::MakeConstSpan(src.data));
}

absl::Status MetalSpatialTensor::WriteData(const Tensor5DFloat32& src) {
  RETURN_IF_ERROR(IsValid(src.shape));
  return WriteDataBHWDC(absl::MakeConstSpan(src.data));
}

absl::Status MetalSpatialTensor::ReadDataBHWDC(absl::Span<float> out) const {
  void* data_ptr = nullptr;
  const int aligned_channels = GetAlignedChannels();
  const int elements_count =
      shape_.b * shape_.w * shape_.h * shape_.d * aligned_channels;
  const size_t data_size = elements_count * SizeOf(descriptor_.data_type);
  std::vector<float> data_f;
  std::vector<half> data_h;
  if (descriptor_.data_type == DataType::FLOAT32) {
    data_f.resize(elements_count);
    data_ptr = data_f.data();
  } else {
    data_h.resize(elements_count);
    data_ptr = data_h.data();
  }

  switch (descriptor_.storage_type) {
    case TensorStorageType::BUFFER:
      std::memcpy(data_ptr, [memory_ contents], data_size);
      break;
    case TensorStorageType::IMAGE_BUFFER:
    case TensorStorageType::TEXTURE_ARRAY:
    case TensorStorageType::TEXTURE_2D:
    case TensorStorageType::TEXTURE_3D:
    case TensorStorageType::SINGLE_TEXTURE_2D:
    default:
      return absl::InternalError("Unsupported tensor storage type");
  }

  if (descriptor_.data_type == DataType::FLOAT32) {
    DataToBHWDC(absl::MakeConstSpan(data_f.data(), data_f.size()), shape_,
                descriptor_, out);
  } else {
    DataToBHWDC(absl::MakeConstSpan(data_h.data(), data_h.size()), shape_,
                descriptor_, out);
  }

  return absl::OkStatus();
}

absl::Status MetalSpatialTensor::ReadData(TensorFloat32* dst) const {
  RETURN_IF_ERROR(IsValid(dst->shape));
  return ReadDataBHWDC(absl::MakeSpan(dst->data));
}

absl::Status MetalSpatialTensor::ReadData(Tensor5DFloat32* dst) const {
  RETURN_IF_ERROR(IsValid(dst->shape));
  return ReadDataBHWDC(absl::MakeSpan(dst->data));
}

absl::Status MetalSpatialTensor::CreateFromDescriptor(const TensorDescriptor& desc, id<MTLDevice> device) {
  shape_ = desc.shape;
  descriptor_.data_type = desc.data_type;
  descriptor_.storage_type = desc.storage_type;
  descriptor_.layout = desc.layout;
  memory_owner_ = true;
  uint8_t* data_ptr = desc.data.empty()
                          ? nullptr
                          : const_cast<unsigned char*>(desc.data.data());
  id<MTLBuffer> buffer;
  RETURN_IF_ERROR(
      AllocateTensorMemory(device, shape_, descriptor_, data_ptr, &buffer));
  memory_ = buffer;
  return absl::OkStatus();
}

void MetalSpatialTensor::SetBufferHandle(id<MTLBuffer> buffer) {
  memory_ = buffer;
}

id<MTLBuffer> MetalSpatialTensor::GetBufferHandle() const {
  return memory_;
}

absl::Status CreateTensor(id<MTLDevice> device, const BHWC& shape,
                          const TensorDescriptor& descriptor, MetalSpatialTensor* result) {
  const BHWDC shape5D(shape.b, shape.h, shape.w, 1, shape.c);
  return CreateTensor(device, shape5D, descriptor, nullptr, result);
}

absl::Status CreateTensor(id<MTLDevice> device, const BHWDC& shape,
                          const TensorDescriptor& descriptor, MetalSpatialTensor* result) {
  return CreateTensor(device, shape, descriptor, nullptr, result);
}

MetalSpatialTensor CreateSharedBufferTensor(id<MTLBuffer> buffer,
                                            const BHWC& shape,
                                            const TensorDescriptor& descriptor) {
  const BHWDC shape5D(shape.b, shape.h, shape.w, 1, shape.c);
  return MetalSpatialTensor(buffer, false, shape5D, descriptor);
}

MetalSpatialTensor CreateSharedBufferTensor(id<MTLBuffer> buffer,
                                            const BHWDC& shape,
                                            const TensorDescriptor& descriptor) {
  return MetalSpatialTensor(buffer, false, shape, descriptor);
}

}  // namespace metal
}  // namespace gpu
}  // namespace tflite
