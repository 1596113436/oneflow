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
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

/*static*/ Maybe<void> SspVariableProxyOp::GetSbp(user_op::SbpContext* ctx) {
  const auto& var_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("var", 0);
  FOR_RANGE(int64_t, i, 0, var_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("var", 0), i)
        .Split(user_op::OpArg("ref", 0), i)
        .Split(user_op::OpArg("value", 0), i)
        .Build();
  }
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> SspVariableProxyOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const Shape& var_shape = ctx->InputShape("var", 0);
  *ctx->OutputShape("ref", 0) = var_shape;
  *ctx->OutputShape("value", 0) = var_shape;
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> SspVariableProxyOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}
/*static*/ Maybe<void> SspVariableProxyOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->OutputDType("ref", 0) = ctx->InputDType("var", 0);
  *ctx->OutputDType("value", 0) = ctx->InputDType("var", 0);
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> SspVariableProxyOp::ModifyOutputArg(
    const GetOutputArgModifier& GetOutputArgModifierFn, const user_op::UserOpConfWrapper&) {
  user_op::OutputArgModifier* out_modifier = GetOutputArgModifierFn("ref", 0);
  CHECK_OR_RETURN(out_modifier != nullptr);
  out_modifier->set_is_mutable(true);
  return Maybe<void>::Ok();
}

}  // namespace oneflow
