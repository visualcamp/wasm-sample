//
// Created by YongGyu Lee on 2020-03-26.
//

#include "cutemodel/cute_model.h"
#include "cutemodel/impl.h"

namespace cute {

std::string tensorName(const Tensor* tensor) {
  return ((const TfLiteTensor *)tensor)->name;
}

std::vector<int> tensorDims(const Tensor* tensor) {
  auto dims = ((const TfLiteTensor *)tensor)->dims;
  std::vector<int> res(dims->size);
  for(int i=0; i<dims->size; ++i)
    res[i] = dims->data[i];
  return res;
}

CuteModel::CuteModel()
  : pImpl(nullptr)
{
  pImpl = new Impl();
}

CuteModel::~CuteModel() {
  delete pImpl;
}

CuteModel& CuteModel::loadBuffer(const void *buffer, std::size_t buffer_size) & {
  pImpl->loadBuffer(buffer, buffer_size);
  return *this;
}

CuteModel& CuteModel::loadFile(const std::string& path) & {
  pImpl->loadFile(path);
  return *this;
}

CuteModel &CuteModel::setNumThreads(int num) & {
  pImpl->setNumThreads(num);
  return *this;
}

CuteModel& CuteModel::setUseGPU(bool use) &{
  if(use)
    pImpl->setUseGPU();
  return *this;
}

void CuteModel::build() {
  return pImpl->build();
}

bool CuteModel::isBuilt() const {
  return pImpl->isBuilt();
}

void CuteModel::setInputInner(int index, const void *data) {
  return pImpl->setInput(index, data);
}

void CuteModel::invoke() {
  input_index = 0;
  return pImpl->invoke();
}

void CuteModel::copyOutput(int index, void *dst) const {
  return pImpl->copyOutput(index, dst);
}

Tensor* CuteModel::intputTensor(int index) {
  return (Tensor *)pImpl->inputTensor(index);
}

const Tensor* CuteModel::inputTensor(int index) const {
  return (const Tensor *)pImpl->inputTensor(index);
}

const Tensor* CuteModel::outputTensor(int index) const {
  return (const Tensor *)pImpl->outputTensor(index);
}

std::vector<int> CuteModel::inputTensorDims(int index) const {
  return pImpl->inputTensorDims(index);
}

std::vector<int> CuteModel::outputTensorDims(int index) const {
  return pImpl->outputTensorDims(index);
}

std::size_t CuteModel::outputBytes(int index) const {
  return pImpl->outputBytes(index);
}

std::string CuteModel::summarize() const {
  return pImpl->summarize();
}
size_t CuteModel::inputTensorCount() const {
  return pImpl->inputTensorCount();
}
size_t CuteModel::outputTensorCount() const {
  return pImpl->outputTensorCount();
}

}


