//
// Created by YongGyu Lee on 2021/03/04.
//

#ifndef CUTE_MODEL_IMPL_
#define CUTE_MODEL_IMPL_

//
// Created by YongGyu Lee on 2020-03-26.
//

//#include "tensorflow/lite/op_resolver.h"
#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/register.h"

#include <sstream>
#include <vector>
#include <string>

namespace cute {

class CuteModel::Impl {
 public:
  Impl() = default;

  void loadBuffer(const void *buffer, size_t bufferSize) {
    model = tflite::FlatBufferModel::BuildFromBuffer(static_cast<const char *>(buffer), bufferSize);
    tflite::InterpreterBuilder builder(*model, resolver);
    if (builder(&interpreter) != kTfLiteOk) {
      assert(((void)"Failed to build Tensorflow Lite interpreter", false));
    }
    interpreter->AllocateTensors();
  }

  void loadFile(const std::string& path) {
    model = tflite::FlatBufferModel::BuildFromFile(path.c_str());
    tflite::InterpreterBuilder builder(*model, resolver);
    if (builder(&interpreter) != kTfLiteOk) {
      assert(((void)"Failed to build Tensorflow Lite interpreter", false));
    }
    interpreter->AllocateTensors();
  }

  void setNumThreads(int num) {
    interpreter->SetNumThreads(num);
  }

  void setUseGPU() {
    // We currently do not use GPU in Android and Web
  }

  void build() {
  }

  bool isBuilt() const noexcept {
    return interpreter != nullptr;
  }

  void setInput(int index, const void* data) {
    auto tensor = interpreter->input_tensor(index);
    std::memcpy(tensor->data.data, data, tensor->bytes);
  }

  void copyOutput(int index, void* dst) {
    auto tensor = interpreter->output_tensor(index);
    std::memcpy(dst, tensor->data.data, tensor->bytes);
  }

  void invoke() noexcept {
    interpreter->Invoke();
  }

  size_t inputTensorCount() const {
    return interpreter->inputs().size();
  }

  size_t outputTensorCount() const {
    return interpreter->outputs().size();
  }

  TfLiteTensor* inputTensor(int index) {
    return interpreter->input_tensor(index);
  }

  const TfLiteTensor* inputTensor(int index) const {
    return interpreter->input_tensor(index);
  }

  const TfLiteTensor* outputTensor(int index) const {
    return interpreter->output_tensor(index);
  }

  std::vector<int> inputTensorDims(int index) const {
    auto tensor = inputTensor(index);
    std::vector<int> v(tensor->dims->size);
    for(int i=0; i<tensor->dims->size; ++i)
      v[i] = tensor->dims->data[i];
    return v;
  }

  std::vector<int> outputTensorDims(int index) const {
    auto tensor = outputTensor(index);
    std::vector<int> v(tensor->dims->size);
    for(int i=0; i<tensor->dims->size; ++i)
      v[i] = tensor->dims->data[i];
    return v;
  }

  std::size_t outputBytes(int index) const {
    return interpreter->output_tensor(index)->bytes;
  }

  std::string summarize() const {
    if (interpreter == nullptr)
      return "Interpreter is not built.";

    static decltype(auto) getTensorInfo = [](const TfLiteTensor *tensor) {
      std::stringstream log;

      log << tensor->name << ' ';
      log << tensor->bytes << ' ';
//        log << tensor->type << ' ';

      if (tensor->dims->size == 0) log << "None";
      else {
        log << tensor->dims->data[0];
        for (int i = 1; i < tensor->dims->size; ++i)
          log << 'x' << tensor->dims->data[i];
      }

      return log.str();
    };

    std::stringstream log;

    log << " Input Tensor\n";
    log << " Number / Name / Byte / Type / Size\n";
    for (int i = 0; i < inputTensorCount(); ++i) {
      log << "  #" << i << ' ' << getTensorInfo(this->inputTensor(i)) << '\n';
    }
    log << '\n';

    log << " Output Tensor\n";
    log << " Number / Name / Byte / Type / Size\n";
    for (int i = 0; i < outputTensorCount(); ++i) {
      log << "  #" << i << ' ' << getTensorInfo(this->outputTensor(i)) << '\n';
    }

    return log.str();
  }

 private:
  std::unique_ptr<tflite::FlatBufferModel> model;
  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
};

}

#endif //CUTE_MODEL_IMPL_CC_
