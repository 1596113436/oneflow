/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/user/kernels/scalar_math_kernels.h"
#include "oneflow/core/primitive/include/broadcast_elementwise_binary.h"
#include "oneflow/core/common/scalar.h"

namespace oneflow {

namespace {

template<typename Context>
std::unique_ptr<primitive::BroadcastElementwiseBinary> NewBroadcastElementwiseBinaryPrimitive(
    Context* ctx, primitive::BinaryOp op) {
  const user_op::TensorDesc* x = ctx->TensorDesc4ArgNameAndIndex("in", 0);
  const user_op::TensorDesc* y = ctx->TensorDesc4ArgNameAndIndex("out", 0);
  const int64_t ndims = y->shape().NumAxes();
  return primitive::NewPrimitive<primitive::BroadcastElementwiseBinaryFactory>(
      ctx->device_type(), op, x->data_type(), y->data_type(), ndims);
}

template<primitive::BinaryOp op>
hob::HobContextGetter<user_op::KernelRegContext, bool> BroadcastElementwiseBinaryPrimitiveExists() {
  return user_op::HobCtxGetter<bool>(
      "BroadcastElementwiseBinaryPrimitiveExists", [](const user_op::KernelRegContext& ctx) {
        return NewBroadcastElementwiseBinaryPrimitive(&ctx, op).operator bool();
      });
}

}  // namespace

template<primitive::BinaryOp op>
class ScalarMathKernel final : public user_op::OpKernel {
 public:
  ScalarMathKernel() = default;
  ~ScalarMathKernel() = default;

 private:
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    Scalar value;
    if (ctx->Attr<bool>("has_int_operand")) {
      value = Scalar(ctx->Attr<int64_t>("int_operand"));
    } else if (ctx->Attr<bool>("has_float_operand")) {
      value = Scalar(ctx->Attr<double>("float_operand"));
    } else {
      UNIMPLEMENTED();
    }

    if (out->shape().NumAxes() != 0) {
      std::unique_ptr<primitive::BroadcastElementwiseBinary> primitive =
          NewBroadcastElementwiseBinaryPrimitive(ctx, op);
      CHECK(primitive);
      primitive->Launch(ctx->stream_ctx(), in->shape().NumAxes(), in->shape().ptr(), in->dptr(),
                        value, out->mut_dptr());
    } else {
      // For 0-d Tensor
      return;
    }
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define SCALAR_MATH_SEQ                                                                 \
  OF_PP_MAKE_TUPLE_SEQ("scalar_add", primitive::BinaryOp::kAdd)                         \
  OF_PP_MAKE_TUPLE_SEQ("scalar_mul", primitive::BinaryOp::kMul)                         \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_equal", primitive::BinaryOp::kEqual)             \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_not_equal", primitive::BinaryOp::kNotEqual)      \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_greater", primitive::BinaryOp::kLessThan)        \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_greater_equal", primitive::BinaryOp::kLessEqual) \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_less", primitive::BinaryOp::kGreaterThan)        \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_less_equal", primitive::BinaryOp::kGreaterEqual) \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_and", primitive::BinaryOp::kLogicalAnd)          \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_or", primitive::BinaryOp::kLogicalOr)            \
  OF_PP_MAKE_TUPLE_SEQ("scalar_logical_xor", primitive::BinaryOp::kLogicalXor)

#define REGISTER_UNARY_MATH_SCALAR_ELEMWISE_USER_KERNEL(op_name, binary_op)                 \
  REGISTER_USER_KERNEL(op_name).SetCreateFn<ScalarMathKernel<binary_op>>().SetIsMatchedHob( \
      (BroadcastElementwiseBinaryPrimitiveExists<binary_op>() == true));

OF_PP_FOR_EACH_TUPLE(REGISTER_UNARY_MATH_SCALAR_ELEMWISE_USER_KERNEL, SCALAR_MATH_SEQ)

// we register uint8_t, int8_t, int32_t, int64_t, float, double, float16.

template<DeviceType device_type, typename T>
class CpuScalarPowGradKernel final : public user_op::OpKernel {
 public:
  CpuScalarPowGradKernel() = default;
  ~CpuScalarPowGradKernel() = default;

 private:
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* x_tensor = ctx->Tensor4ArgNameAndIndex("x", 0);
    const user_op::Tensor* dy_tensor = ctx->Tensor4ArgNameAndIndex("dy", 0);
    user_op::Tensor* dx_tensor = ctx->Tensor4ArgNameAndIndex("dx", 0);
    const T* x_ptr = x_tensor->dptr<T>();
    const T* dy_ptr = dy_tensor->dptr<T>();
    T* dx_ptr = dx_tensor->mut_dptr<T>();
    T scalar_operand = static_cast<T>(0);
    if (ctx->Attr<bool>("has_int_operand")) {
      scalar_operand = static_cast<T>(ctx->Attr<int64_t>("int_operand"));
    } else if (ctx->Attr<bool>("has_float_operand")) {
      scalar_operand = static_cast<T>(ctx->Attr<double>("float_operand"));
    } else {
      UNIMPLEMENTED();
    }

    const int32_t elem_cnt = x_tensor->shape().elem_cnt();
    FOR_RANGE(int32_t, i, 0, elem_cnt) {
      dx_ptr[i] =
          scalar_operand * (std::pow(x_ptr[i], scalar_operand - static_cast<T>(1))) * dy_ptr[i];
    }
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(device, dtype)  \
  REGISTER_USER_KERNEL("scalar_pow_grad")                   \
      .SetCreateFn<CpuScalarPowGradKernel<device, dtype>>() \
      .SetIsMatchedHob((user_op::HobDeviceTag() == device)  \
                       & (user_op::HobDataType("dx", 0) == GetDataType<dtype>::value));

REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, uint8_t);
REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, int8_t);
REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, int32_t);
REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, int64_t);
REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, float);
REGISTER_CPU_SCALAR_POW_GRAD_KERNEL(DeviceType::kCPU, double);

}  // namespace oneflow
