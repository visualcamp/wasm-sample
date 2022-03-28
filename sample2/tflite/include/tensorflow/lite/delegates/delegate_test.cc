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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include "flatbuffers/flatbuffers.h"  // from @flatbuffers
#include "tensorflow/lite/delegates/delegate_test_util.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/internal/compatibility.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/schema/schema_conversion_utils.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/testing/util.h"
#include "tensorflow/lite/version.h"

namespace tflite {
namespace delegates {

using test_utils::TestDelegate;
using test_utils::TestFP16Delegation;

namespace {

TEST_F(TestDelegate, BasicDelegate) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate());

  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  int node = interpreter_->execution_plan()[0];
  const auto* node_and_reg = interpreter_->node_and_registration(node);
  EXPECT_EQ(node_and_reg->second.custom_name,
            delegate_->FakeFusedRegistration().custom_name);

  const TfLiteDelegateParams* params = static_cast<const TfLiteDelegateParams*>(
      node_and_reg->first.builtin_data);
  ASSERT_EQ(params->nodes_to_replace->size, 3);
  EXPECT_EQ(params->nodes_to_replace->data[0], 0);
  EXPECT_EQ(params->nodes_to_replace->data[1], 1);
  EXPECT_EQ(params->nodes_to_replace->data[2], 2);

  ASSERT_EQ(params->input_tensors->size, 2);
  EXPECT_EQ(params->input_tensors->data[0], 0);
  EXPECT_EQ(params->input_tensors->data[1], 1);

  ASSERT_EQ(params->output_tensors->size, 2);
  EXPECT_EQ(params->output_tensors->data[0], 3);
  EXPECT_EQ(params->output_tensors->data[1], 4);
}

TEST_F(TestDelegate, DelegateNodePrepareFailure) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 1, 2}, kTfLiteDelegateFlagsNone, true /**fail_node_prepare**/));
  // ModifyGraphWithDelegate fails, since the Prepare() method in the node's
  // TfLiteRegistration returns an error status.
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteDelegateError);
  // Execution plan should remain unchanged.
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);

  std::vector<float> input = {1.0f, 2.0f, 3.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f};
  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  // Verify Invoke() behavior.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, DelegateNodeInvokeFailure) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 1, 2}, kTfLiteDelegateFlagsNone, false /**fail_node_prepare**/,
      0 /**min_ops_per_subset**/, true /**fail_node_invoke**/));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Delegation modified execution plan.
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  std::vector<float> input = {1.0f, 2.0f, 3.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f};
  constexpr int kOutputTensorIndex = 3;

  // Verify Invoke() behavior: fails first, succeeds after RemoveAllDelegates().
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  EXPECT_EQ(interpreter_->Invoke(), kTfLiteError);
  ASSERT_EQ(RemoveAllDelegates(), kTfLiteOk);
  // Delegation removed, returning to original execution plan.
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, SecondDelegationPrepareFailure) {
  // First delegate only supports nodes 1, 2. Gets applied successfully.
  // This delegate should support dynamic tensors, otherwise the second won't be
  // applied.
  delegate_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({1, 2}, kTfLiteDelegateFlagsAllowDynamicTensors));
  // Second delegate supports node 0, but fails during the delegate-node's
  // Prepare.
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0}, kTfLiteDelegateFlagsNone, true /**fail_node_prepare**/));

  // Initially, execution plan has 3 nodes.
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  // First delegate should be applied successfully, yielding a plan with 2
  // nodes.
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);
  // Second delegate won't get applied.
  // As a result, previous delegate should also get undone, restoring the
  // execution plan to its original state.
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteDelegateError);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);

  std::vector<float> input = {1.0f, 2.0f, 3.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f};
  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  // Verify Invoke() behavior.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, SecondDelegationInvokeFailure) {
  delegate_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({1, 2}, kTfLiteDelegateFlagsAllowDynamicTensors));
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0}, kTfLiteDelegateFlagsNone, false /**fail_node_prepare**/,
      0 /**min_ops_per_subset**/, true /**fail_node_invoke**/));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  std::vector<float> input = {1.0f, 2.0f, 3.0f};
  // Outputs match the AddOp path, rather than delegate path.
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f};
  constexpr int kOutputTensorIndex = 3;

  // Verify Invoke() behavior to ensure Interpreter isn't broken.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  EXPECT_EQ(interpreter_->Invoke(), kTfLiteError);
  EXPECT_EQ(RemoveAllDelegates(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

// This test ensures that node indices in multi-delegate application are handled
// correctly by the TFLite partitioning algorithm.
TEST_F(TestDelegate, TwoDelegates_ExecutionPlanIndicesDifferent) {
  // First delegate supports nodes 0, 1.
  // After this delegation, the execution plan size is 2.
  delegate_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({0, 1}, kTfLiteDelegateFlagsAllowDynamicTensors));
  // Second delegate supports (original) node index 2.
  // The execution plan has 2 nodes, so this verifies that the partitioning
  // algorithm correctly refers to (original) node indices instead of execution
  // plan indices.
  delegate2_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({2}, kTfLiteDelegateFlagsNone));

  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  // Verify Invoke works.
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
}

TEST_F(TestDelegate, StaticDelegateMakesGraphImmutable) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  // Deliberately try to set tensor params with quantization while immutable,
  // ensuring quantization is properly freed.
  TfLiteQuantization quant = {};
  quant.type = kTfLiteAffineQuantization;
  auto quant_params = static_cast<TfLiteAffineQuantization*>(
      malloc(sizeof(TfLiteAffineQuantization)));
  quant_params->scale = nullptr;
  quant_params->zero_point = nullptr;
  quant_params->quantized_dimension = 0;
  quant.params = quant_params;
  ASSERT_NE(interpreter_->SetTensorParametersReadWrite(0, kTfLiteInt8, "", {3},
                                                       quant),
            kTfLiteOk);
}

TEST_F(TestDelegate, ComplexDelegate) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({1, 2}));
  interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate());

  ASSERT_EQ(interpreter_->execution_plan().size(), 2);
  // 0th should be a non-delegated original op
  ASSERT_EQ(interpreter_->execution_plan()[0], 0);
  // 1st should be a new macro op (3) which didn't exist)
  ASSERT_EQ(interpreter_->execution_plan()[1], 3);
  const auto* node_and_reg = interpreter_->node_and_registration(3);
  ASSERT_EQ(node_and_reg->second.custom_name,
            delegate_->FakeFusedRegistration().custom_name);
}

TEST_F(TestDelegate, SetBufferHandleToInput) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  TfLiteDelegate* delegate = delegate_->get_tf_lite_delegate();
  interpreter_->ModifyGraphWithDelegate(delegate);

  constexpr int kOutputTensorIndex = 0;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  ASSERT_EQ(tensor->delegate, nullptr);
  ASSERT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);

  TfLiteBufferHandle handle = AllocateBufferHandle();
  TfLiteStatus status =
      interpreter_->SetBufferHandle(kOutputTensorIndex, handle, delegate);
  ASSERT_EQ(status, kTfLiteOk);
  EXPECT_EQ(tensor->delegate, delegate);
  EXPECT_EQ(tensor->buffer_handle, handle);
}

TEST_F(TestDelegate, SetBufferHandleToOutput) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  TfLiteDelegate* delegate = delegate_->get_tf_lite_delegate();
  interpreter_->ModifyGraphWithDelegate(delegate);

  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  // Before setting the buffer handle, the tensor's `delegate` is already set
  // because it will be written by the delegate.
  ASSERT_EQ(tensor->delegate, delegate);
  ASSERT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);

  TfLiteBufferHandle handle = AllocateBufferHandle();
  TfLiteStatus status =
      interpreter_->SetBufferHandle(kOutputTensorIndex, handle, delegate);
  ASSERT_EQ(status, kTfLiteOk);
  EXPECT_EQ(tensor->delegate, delegate);
  EXPECT_EQ(tensor->buffer_handle, handle);
}

TEST_F(TestDelegate, SetInvalidHandleToTensor) {
  interpreter_->Invoke();
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  TfLiteDelegate* delegate = delegate_->get_tf_lite_delegate();
  interpreter_->ModifyGraphWithDelegate(delegate);

  SimpleDelegate another_simple_delegate({0, 1, 2});

  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  // Before setting the buffer handle, the tensor's `delegate` is already set
  // because it will be written by the delegate.
  ASSERT_EQ(tensor->delegate, delegate);
  ASSERT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);

  TfLiteBufferHandle handle = AllocateBufferHandle();
  TfLiteStatus status = interpreter_->SetBufferHandle(
      kOutputTensorIndex, handle,
      another_simple_delegate.get_tf_lite_delegate());
  // Setting a buffer handle to a tensor with another delegate will fail.
  ASSERT_EQ(status, kTfLiteError);
  EXPECT_EQ(tensor->delegate, delegate);
  EXPECT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);
}

// We utilize delegation in such a way as to allow node subsets with a minimum
// number of ops only.
TEST_F(TestDelegate, TestDelegationWithPartitionPreview) {
  // We set kTfLiteDelegateFlagsAllowDynamicTensors to ensure the second
  // delegate can be applied.
  // Ops 0 and 2 are delegated but end up in the same partition (based on
  // dependency analysis). However, since min_ops_per_subset = 3, no delegation
  // takes place.
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 2}, kTfLiteDelegateFlagsAllowDynamicTensors,
      false /**fail_node_prepare**/, 3 /**min_ops_per_subset**/));
  interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate());

  // Original execution plan remains.
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(interpreter_->execution_plan()[0], 0);
  ASSERT_EQ(interpreter_->execution_plan()[1], 1);
  ASSERT_EQ(interpreter_->execution_plan()[2], 2);

  // Same ops supported, but min_ops_per_subset = 2.
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 2}, kTfLiteDelegateFlagsAllowDynamicTensors,
      false /**fail_node_prepare**/, 2 /**min_ops_per_subset**/));
  interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate());

  ASSERT_EQ(interpreter_->execution_plan().size(), 2);
  ASSERT_EQ(interpreter_->execution_plan()[0], 3);
  const auto* node_and_reg = interpreter_->node_and_registration(3);
  ASSERT_EQ(node_and_reg->second.custom_name,
            delegate2_->FakeFusedRegistration().custom_name);
  ASSERT_EQ(interpreter_->execution_plan()[1], 1);
}

TEST_F(TestDelegate, TestResizeInputWithNonDynamicDelegate) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);

  // Try resizing input to same shape as before (which should be a No-op).
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  // This should fail, since the previous application of the delegate will be
  // re-done automatically, making the graph immutable again.
  ASSERT_NE(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Ensure graph has been restored to its valid delegated state.
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  // Verify Invoke() behavior.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }

  // Resize again, but call AllocateTensors as usual afterwards.
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 4 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 4 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, TestResizeInputWithMultipleDelegates) {
  // First delegate only supports node 0.
  // This delegate should support dynamic tensors, otherwise the second won't be
  // applied.
  delegate_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({0}, kTfLiteDelegateFlagsAllowDynamicTensors));
  // Second delegate supports nodes 1 & 2, and makes the graph immutable.
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({1, 2}));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Should be two delegates nodes.
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  // Try resizing input to same shape as before (which should be a No-op).
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  // Resizing input tensors should temporarily restore original execution plan
  // of 3 nodes.
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 3}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  // This should fail, since the previous application of the delegate will be
  // re-done automatically, making the graph immutable again.
  ASSERT_NE(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Ensure graph has been restored to its valid delegated state.
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 2;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  // Verify Invoke() behavior.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }

  // Resize again, but call AllocateTensors as usual afterwards.
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 4 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 4 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

// If a delegate sets kTfLiteDelegateFlagsRequirePropagatedShapes but not
// kTfLiteDelegateFlagsAllowDynamicTensors, the former is redundant.
TEST_F(TestDelegate, TestRequirePropagatedShapes_NonDynamicDelegate) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 1, 2}, kTfLiteDelegateFlagsRequirePropagatedShapes));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);

  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  // Resizing should revert execution plan to original state.
  ASSERT_EQ(interpreter_->execution_plan().size(), 3);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 4 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 4 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, TestRequirePropagatedShapes_DynamicDelegateWithFlag) {
  // Delegate sets both flags and in its Prepare, ensures that shapes have been
  // propagated by runtime.
  int delegate_flags = kTfLiteDelegateFlagsAllowDynamicTensors |
                       kTfLiteDelegateFlagsRequirePropagatedShapes;
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 1, 2}, delegate_flags, false /**fail_node_prepare**/,
      3 /**min_ops_per_subset**/, false /**fail_node_invoke**/,
      true /**automatic_shape_propagation**/));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);

  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 4 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 4 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

// If the delegate implementation expects shapes to be automatically propagated
// but does not set the required flag, its Prepare should fail.
TEST_F(TestDelegate, TestRequirePropagatedShapes_DynamicDelegateWithoutFlag) {
  // Delegate sets both flags and in its Prepare, ensures that shapes have been
  // propagated by runtime.
  int delegate_flags = kTfLiteDelegateFlagsAllowDynamicTensors;
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0, 1, 2}, delegate_flags, false /**fail_node_prepare**/,
      3 /**min_ops_per_subset**/, false /**fail_node_invoke**/,
      true /**automatic_shape_propagation**/));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);

  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteError);
}

TEST_F(TestDelegate, TestRequirePropagatedShapes_MultipleDelegates) {
  // First delegate needs to support dynamic tensors to allow second delegation.
  // This delegate does not require automatic propagation.
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {0}, kTfLiteDelegateFlagsAllowDynamicTensors,
      false /**fail_node_prepare**/, 1 /**min_ops_per_subset**/,
      false /**fail_node_invoke**/, false /**automatic_shape_propagation**/));
  // Second delegate supports nodes 1 & 2, and requires automatic shape
  // propagation.
  int delegate_flags = kTfLiteDelegateFlagsAllowDynamicTensors |
                       kTfLiteDelegateFlagsRequirePropagatedShapes;
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate(
      {1, 2}, delegate_flags, false /**fail_node_prepare**/,
      1 /**min_ops_per_subset**/, false /**fail_node_invoke**/,
      true /**automatic_shape_propagation**/));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Should be two delegate nodes.
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(1, {1, 4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 2;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 4 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 4 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }
}

TEST_F(TestDelegate, ReleaseNonPersistentMemoryWithDelegates) {
  // First delegate only supports node 0.
  // This delegate should support dynamic tensors, otherwise the second won't be
  // applied.
  delegate_ = std::unique_ptr<SimpleDelegate>(
      new SimpleDelegate({0}, kTfLiteDelegateFlagsAllowDynamicTensors));
  // Second delegate supports nodes 1 & 2, and makes the graph immutable.
  delegate2_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({1, 2}));

  // No-op.
  ASSERT_EQ(interpreter_->ReleaseNonPersistentMemory(), kTfLiteOk);

  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate2_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Should be two delegates nodes.
  ASSERT_EQ(interpreter_->execution_plan().size(), 2);

  ASSERT_EQ(interpreter_->ReleaseNonPersistentMemory(), kTfLiteOk);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  // This should fail, since the graph is immutable.
  ASSERT_NE(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);

  std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
  std::vector<float> expected_output = {2.0f, 4.0f, 6.0f, 8.0f};
  constexpr int kOutputTensorIndex = 2;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);

  // Verify Invoke() behavior.
  memcpy(interpreter_->typed_tensor<float>(0), input.data(), 3 * sizeof(float));
  memcpy(interpreter_->typed_tensor<float>(1), input.data(), 3 * sizeof(float));
  interpreter_->Invoke();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(tensor->data.f[i], expected_output[i]) << i;
  }

  ASSERT_EQ(interpreter_->ReleaseNonPersistentMemory(), kTfLiteOk);
}

TEST_F(TestDelegate, TestCopyFromBufferInvoke) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  TfLiteDelegate* delegate = delegate_->get_tf_lite_delegate();
  interpreter_->ModifyGraphWithDelegate(delegate);

  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  std::vector<float> floats = {1.0f, 2.0f, 3.0f};
  memcpy(interpreter_->typed_tensor<float>(0), floats.data(),
         floats.size() * sizeof(float));

  memcpy(interpreter_->typed_tensor<float>(1), floats.data(),
         floats.size() * sizeof(float));

  // Before setting the buffer handle, the tensor's `delegate` is already set
  // because it will be written by the delegate.
  ASSERT_EQ(tensor->delegate, delegate);
  ASSERT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);

  // Called Invoke without setting the buffer will not call the CopyFromBuffer
  interpreter_->Invoke();
  std::vector<float> res = {2.0f, 4.0f, 6.0f};
  for (int i = 0; i < tensor->dims->data[0]; ++i) {
    ASSERT_EQ(tensor->data.f[i], res[i]);
  }
}

TEST_F(TestDelegate, TestCopyFromBuffer) {
  delegate_ = std::unique_ptr<SimpleDelegate>(new SimpleDelegate({0, 1, 2}));
  TfLiteDelegate* delegate = delegate_->get_tf_lite_delegate();
  interpreter_->ModifyGraphWithDelegate(delegate);

  constexpr int kOutputTensorIndex = 3;
  TfLiteTensor* tensor = interpreter_->tensor(kOutputTensorIndex);
  std::vector<float> floats = {1.0f, 2.0f, 3.0f};
  memcpy(interpreter_->typed_tensor<float>(0), floats.data(),
         floats.size() * sizeof(float));

  memcpy(interpreter_->typed_tensor<float>(1), floats.data(),
         floats.size() * sizeof(float));

  // Before setting the buffer handle, the tensor's `delegate` is already set
  // because it will be written by the delegate.
  ASSERT_EQ(tensor->delegate, delegate);
  ASSERT_EQ(tensor->buffer_handle, kTfLiteNullBufferHandle);

  TfLiteBufferHandle handle = AllocateBufferHandle();
  TfLiteStatus status =
      interpreter_->SetBufferHandle(kOutputTensorIndex, handle, delegate);
  interpreter_->Invoke();
  ASSERT_EQ(status, kTfLiteOk);
  EXPECT_EQ(tensor->delegate, delegate);
  EXPECT_EQ(tensor->buffer_handle, handle);
  for (int i = 0; i < tensor->dims->data[0]; ++i) {
    ASSERT_EQ(tensor->data.f[i], 6.0f);
  }
}

TEST_F(TestDelegate, DelegateCustomOpResolution) {
  // Build a flatbuffer model that contains the "my_add" custom op which gets
  // resolved only after SimpleDelegate is applied.
  flatbuffers::FlatBufferBuilder builder;
  // Tensors.
  const int32_t shape[1] = {3};
  flatbuffers::Offset<Tensor> tensors[3] = {
      CreateTensor(builder, builder.CreateVector<int32_t>(shape, 1),
                   TensorType_FLOAT32, /*buffer=*/0, builder.CreateString("X")),
      CreateTensor(builder, builder.CreateVector<int32_t>(shape, 1),
                   TensorType_FLOAT32, /*buffer=*/0, builder.CreateString("Y")),
      CreateTensor(builder, builder.CreateVector<int32_t>(shape, 1),
                   TensorType_FLOAT32, /*buffer=*/0, builder.CreateString("Z")),
  };
  // Custom op definition.
  flatbuffers::Offset<OperatorCode> op_code =
      CreateOperatorCodeDirect(builder, BuiltinOperator_CUSTOM, "my_add");
  const int32_t inputs[2] = {0, 1};
  const int32_t outputs[1] = {2};
  flatbuffers::Offset<Operator> op = CreateOperator(
      builder, /*opcode_index=*/0, builder.CreateVector<int32_t>(inputs, 2),
      builder.CreateVector<int32_t>(outputs, 1), BuiltinOptions_NONE,
      /*builtin_options=*/0,
      /*custom_options=*/0, tflite::CustomOptionsFormat_FLEXBUFFERS);
  // Subgraph & Model.
  flatbuffers::Offset<SubGraph> subgraph =
      CreateSubGraph(builder, builder.CreateVector(tensors, 3),
                     builder.CreateVector<int32_t>(inputs, 2),
                     builder.CreateVector<int32_t>(outputs, 1),
                     builder.CreateVector(&op, 1), /*name=*/0);
  flatbuffers::Offset<Buffer> buffers[1] = {
      CreateBuffer(builder, builder.CreateVector({})),
  };
  flatbuffers::Offset<Model> model_buffer = CreateModel(
      builder, TFLITE_SCHEMA_VERSION, builder.CreateVector(&op_code, 1),
      builder.CreateVector(&subgraph, 1), builder.CreateString("test_model"),
      builder.CreateVector(buffers, 1));
  builder.Finish(model_buffer);
  std::vector<char> buffer =
      std::vector<char>(builder.GetBufferPointer(),
                        builder.GetBufferPointer() + builder.GetSize());
  const Model* model = GetModel(buffer.data());

  // Build an interpreter with the model. Initialization should work fine.
  std::unique_ptr<Interpreter> interpreter;
  ASSERT_EQ(
      InterpreterBuilder(
          model, ::tflite::ops::builtin::BuiltinOpResolver())(&interpreter),
      kTfLiteOk);
  // AllocateTensors should fail, since my_add hasn't been resolved.
  ASSERT_EQ(interpreter->AllocateTensors(), kTfLiteError);

  // Applying static delegate won't work, since the interpreter will first try
  // to Prepare all original nodes.
  std::unique_ptr<SimpleDelegate> static_delegate(new SimpleDelegate({0}));
  ASSERT_EQ(interpreter->ModifyGraphWithDelegate(
                static_delegate->get_tf_lite_delegate()),
            kTfLiteError);

  // Applying delegate that supports dynamic tensors should work.
  std::unique_ptr<SimpleDelegate> dynamic_delegate(
      new SimpleDelegate({0}, kTfLiteDelegateFlagsAllowDynamicTensors));
  ASSERT_EQ(interpreter->ModifyGraphWithDelegate(
                dynamic_delegate->get_tf_lite_delegate()),
            kTfLiteOk);
  // AllocateTensors will now work.
  ASSERT_EQ(interpreter->AllocateTensors(), kTfLiteOk);
}

class TestDelegateWithDynamicTensors : public ::testing::Test {
 protected:
  void SetUp() override {
    interpreter_.reset(new Interpreter);

    interpreter_->AddTensors(3);
    interpreter_->SetInputs({0});
    interpreter_->SetOutputs({1, 2});
    TfLiteQuantizationParams quant;
    interpreter_->SetTensorParametersReadWrite(0, kTfLiteFloat32, "", {3},
                                               quant);
    interpreter_->SetTensorParametersReadWrite(1, kTfLiteFloat32, "", {3},
                                               quant);
    interpreter_->SetTensorParametersReadWrite(2, kTfLiteFloat32, "", {3},
                                               quant);
    TfLiteRegistration reg = DynamicCopyOpRegistration();
    interpreter_->AddNodeWithParameters({0}, {1, 2}, nullptr, 0, nullptr, &reg);

    delegate_.Prepare = [](TfLiteContext* context,
                           TfLiteDelegate* delegate) -> TfLiteStatus {
      // In this test, the delegate replaces all the nodes if this function is
      // called.
      TfLiteIntArray* execution_plan;
      TF_LITE_ENSURE_STATUS(
          context->GetExecutionPlan(context, &execution_plan));
      context->ReplaceNodeSubsetsWithDelegateKernels(
          context, DelegateRegistration(), execution_plan, delegate);
      return kTfLiteOk;
    };
    delegate_.flags = kTfLiteDelegateFlagsNone;
  }

  static TfLiteRegistration DynamicCopyOpRegistration() {
    TfLiteRegistration reg = {nullptr, nullptr, nullptr, nullptr};

    reg.prepare = [](TfLiteContext* context, TfLiteNode* node) {
      // Output 0 is dynamic
      TfLiteTensor* output0;
      TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, 0, &output0));
      SetTensorToDynamic(output0);
      // Output 1 has the same shape as input.
      const TfLiteTensor* input;
      TF_LITE_ENSURE_OK(context, GetInputSafe(context, node, 0, &input));
      TfLiteTensor* output1;
      TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, 1, &output1));
      TF_LITE_ENSURE_STATUS(context->ResizeTensor(
          context, output1, TfLiteIntArrayCopy(input->dims)));
      return kTfLiteOk;
    };

    reg.invoke = [](TfLiteContext* context, TfLiteNode* node) {
      // Not implemented since this isn't required in testing.
      return kTfLiteOk;
    };
    return reg;
  }

  static TfLiteRegistration DelegateRegistration() {
    TfLiteRegistration reg = {nullptr, nullptr, nullptr, nullptr};

    reg.prepare = [](TfLiteContext* context, TfLiteNode* node) {
      // If tensors are resized, the runtime should propagate shapes
      // automatically if correct flag is set. Ensure values are correct.
      // Output 0 should be dynamic.
      TfLiteTensor* output0;
      TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, 0, &output0));
      TF_LITE_ENSURE(context, IsDynamicTensor(output0));
      // Output 1 has the same shape as input.
      const TfLiteTensor* input;
      TF_LITE_ENSURE_OK(context, GetInputSafe(context, node, 0, &input));
      TfLiteTensor* output1;
      TF_LITE_ENSURE_OK(context, GetOutputSafe(context, node, 1, &output1));
      TF_LITE_ENSURE(context, input->dims->size == output1->dims->size);
      TF_LITE_ENSURE(context, input->dims->data[0] == output1->dims->data[0]);
      return kTfLiteOk;
    };

    return reg;
  }

  std::unique_ptr<Interpreter> interpreter_;
  TfLiteDelegate delegate_;
};

TEST_F(TestDelegateWithDynamicTensors, DisallowDynamicTensors) {
  interpreter_->ModifyGraphWithDelegate(&delegate_);

  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  // The interpreter should not call delegate's `Prepare` when dynamic tensors
  // exist. So the node ID isn't changed.
  ASSERT_EQ(interpreter_->execution_plan()[0], 0);
}

TEST_F(TestDelegateWithDynamicTensors, AllowDynamicTensors) {
  delegate_.flags = kTfLiteDelegateFlagsAllowDynamicTensors;
  interpreter_->ModifyGraphWithDelegate(&delegate_);

  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  // The node should be replaced because dynamic tensors are allowed. Therefore
  // only node ID in the execution plan is changed from 0 to 1.
  ASSERT_EQ(interpreter_->execution_plan()[0], 1);
}

TEST_F(TestDelegateWithDynamicTensors, ModifyGraphAfterAllocate) {
  // Trigger allocation *before* delegate application.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  delegate_.flags = kTfLiteDelegateFlagsAllowDynamicTensors;
  ASSERT_EQ(interpreter_->ModifyGraphWithDelegate(&delegate_), kTfLiteOk);
  ASSERT_EQ(interpreter_->execution_plan().size(), 1);
  ASSERT_EQ(interpreter_->execution_plan()[0], 1);

  // Allocation should still succeed.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
}

TEST_F(TestDelegateWithDynamicTensors, ShapePropagation_FlagSet) {
  // Trigger allocation *before* delegate application.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  delegate_.flags = kTfLiteDelegateFlagsAllowDynamicTensors |
                    kTfLiteDelegateFlagsRequirePropagatedShapes;
  ASSERT_EQ(interpreter_->ModifyGraphWithDelegate(&delegate_), kTfLiteOk);

  // Allocation before & after resizing tensors should work.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
}

TEST_F(TestDelegateWithDynamicTensors, ShapePropagation_FlagNotSet) {
  // Trigger allocation *before* delegate application.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  delegate_.flags = kTfLiteDelegateFlagsAllowDynamicTensors;
  ASSERT_EQ(interpreter_->ModifyGraphWithDelegate(&delegate_), kTfLiteOk);

  // Allocation after resizing tensors should NOT work, since runtime won't
  // propagate shape - causing delegate kernel to fail.
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  ASSERT_EQ(interpreter_->ResizeInputTensor(0, {4}), kTfLiteOk);
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteError);
}

// Tests for FP16 graphs
// =====================

TEST_P(TestFP16Delegation, NonDelegatedInterpreterWorks) {
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
  VerifyInvoke();
}

TEST_P(TestFP16Delegation, DelegationWorks) {
  delegate_ = std::unique_ptr<FP16Delegate>(
      new FP16Delegate(/**num_delegated_subsets**/ GetParam()));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteOk);
  // Should have 5 nodes: delegate, mul, add2 & 2 dequantize (one for mul &
  // add2).
  ASSERT_EQ(interpreter_->execution_plan().size(), 5);
  VerifyInvoke();
}

TEST_P(TestFP16Delegation, DelegatePrepareFails) {
  delegate_ = std::unique_ptr<FP16Delegate>(new FP16Delegate(
      /**num_delegated_subsets**/ GetParam(), /**fail_node_prepare**/ true));
  ASSERT_EQ(
      interpreter_->ModifyGraphWithDelegate(delegate_->get_tf_lite_delegate()),
      kTfLiteDelegateError);
  // Delegation failed, but runtime should go back to correct previous state.
  ASSERT_EQ(interpreter_->execution_plan().size(), 8);
  VerifyInvoke();
}

INSTANTIATE_TEST_SUITE_P(TestFP16Delegation, TestFP16Delegation,
                         ::testing::Values(1, 2));

}  // anonymous namespace
}  // namespace delegates
}  // namespace tflite

int main(int argc, char** argv) {
  ::tflite::LogToStderr();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
