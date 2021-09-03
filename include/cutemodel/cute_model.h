//
// Created by YongGyu Lee on 2021/03/04.
//

#ifndef CUTE_MODEL_H_
#define CUTE_MODEL_H_

#include <cstddef>
#include <string>
#include <vector>

namespace cute {

struct Tensor {};

std::string tensorName(const Tensor* tensor);
std::vector<int> tensorDims(const Tensor* tensor);

// Pimpl and builder pattern

class CuteModel {
 private:
  class Impl;
  Impl* pImpl;

  int input_index = 0;

  void setInputInner(int index, const void* data);

 public:
  CuteModel();
  ~CuteModel();

  CuteModel(const CuteModel&) = delete;
  CuteModel& operator = (const CuteModel&) = delete;
  CuteModel(CuteModel&&) = default;
  CuteModel& operator = (CuteModel&&) = default;

  CuteModel& loadBuffer(const void* buffer, std::size_t buffer_size) &;
  CuteModel& loadFile(const std::string& path) &;

  CuteModel& setNumThreads(int num) &;
  CuteModel& setUseGPU(bool use) &;

  void build();
  bool isBuilt() const;

  template<class Input>                   void setInput(const Input input);
  template<class Input, class ...Inputs>  void setInput(const Input input, const Inputs ...inputs);

  template<typename T> std::vector<T> getOutput(int index) const;
  void copyOutput(int index, void* dst) const;

  void invoke();

  Tensor* intputTensor(int index);
  const Tensor* inputTensor(int index) const;
  const Tensor* outputTensor(int index) const;

  std::vector<int> inputTensorDims(int index) const;
  std::vector<int> outputTensorDims(int index) const;

  size_t inputTensorCount() const;
  size_t outputTensorCount() const;

  std::size_t outputBytes(int index) const;

  std::string summarize() const;
};


template<typename Input>
void CuteModel::setInput(const Input input) {
  setInputInner(input_index, input);
  ++input_index;
}

template<typename Input, typename ...Inputs>
void CuteModel::setInput(const Input input, const Inputs ...inputs) {
  setInput(input);
  setInput(inputs...);
}

template<typename T>
std::vector<T> CuteModel::getOutput(int index) const {
  std::vector<T> output(outputBytes(index) / sizeof(T));
  copyOutput(index, output.data());
  return output;
}

struct CuteModelBuilderOptions {
  const void* buffer;
  std::size_t buffer_size;
  int num_threads = 2;
  bool use_gpu = false;

  inline CuteModelBuilderOptions(const void* buffer, std::size_t buffer_size, int num_threads = 2, bool use_gpu = false)
    : buffer(buffer), buffer_size(buffer_size), num_threads(num_threads), use_gpu(use_gpu) {}

};

class CuteModelBuilder {
 private:
  CuteModelBuilderOptions option;
 public:
  inline CuteModelBuilder(const CuteModelBuilderOptions& option)
    : option(option) {}

  inline bool build(CuteModel& model) const & {
    model.loadBuffer(option.buffer, option.buffer_size)
         .setNumThreads(option.num_threads)
         .setUseGPU(option.use_gpu)
         .build();
    return model.isBuilt();
  }
};

}

#endif //CUTE_MODEL_H_
