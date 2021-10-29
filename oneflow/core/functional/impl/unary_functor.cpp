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

#include "oneflow/core/functional/impl/unary_functor.h"
#include "oneflow/core/functional/impl/binary_functor.h"

#include "oneflow/core/framework/op_builder.h"
#include "oneflow/core/functional/function_library.h"
#include "oneflow/user/ops/math_unary_elementwise_seq.h"

namespace oneflow {
namespace one {
namespace functional {

namespace impl {

#define INPLACE_UNARY_FLOAT_FUNC_SEQ OF_PP_MAKE_TUPLE_SEQ("sin", InplaceSin)

#define UNARY_FUNC_SEQ                                       \
  OF_PP_MAKE_TUPLE_SEQ("abs", Abs)                           \
  OF_PP_MAKE_TUPLE_SEQ("acos", Acos)                         \
  OF_PP_MAKE_TUPLE_SEQ("ceil", Ceil)                         \
  OF_PP_MAKE_TUPLE_SEQ("cosh", Cosh)                         \
  OF_PP_MAKE_TUPLE_SEQ("floor", Floor)                       \
  OF_PP_MAKE_TUPLE_SEQ("lgamma", Lgamma)                     \
  OF_PP_MAKE_TUPLE_SEQ("log_sigmoid", LogSigmoid)            \
  OF_PP_MAKE_TUPLE_SEQ("reciprocal_no_nan", ReciprocalNoNan) \
  OF_PP_MAKE_TUPLE_SEQ("rint", Rint)                         \
  OF_PP_MAKE_TUPLE_SEQ("round", Round)                       \
  OF_PP_MAKE_TUPLE_SEQ("softplus", Softplus)

#define FLOAT_UNARY_FUNC_SEQ                     \
  OF_PP_MAKE_TUPLE_SEQ("acosh", Acosh)           \
  OF_PP_MAKE_TUPLE_SEQ("asin", Asin)             \
  OF_PP_MAKE_TUPLE_SEQ("asinh", Asinh)           \
  OF_PP_MAKE_TUPLE_SEQ("atan", Atan)             \
  OF_PP_MAKE_TUPLE_SEQ("atanh", Atanh)           \
  OF_PP_MAKE_TUPLE_SEQ("sin", Sin)               \
  OF_PP_MAKE_TUPLE_SEQ("cos", Cos)               \
  OF_PP_MAKE_TUPLE_SEQ("erf", Erf)               \
  OF_PP_MAKE_TUPLE_SEQ("erfc", Erfc)             \
  OF_PP_MAKE_TUPLE_SEQ("exp", Exp)               \
  OF_PP_MAKE_TUPLE_SEQ("expm1", Expm1)           \
  OF_PP_MAKE_TUPLE_SEQ("log", Log)               \
  OF_PP_MAKE_TUPLE_SEQ("log1p", Log1p)           \
  OF_PP_MAKE_TUPLE_SEQ("negative", Negative)     \
  OF_PP_MAKE_TUPLE_SEQ("reciprocal", Reciprocal) \
  OF_PP_MAKE_TUPLE_SEQ("rsqrt", Rsqrt)           \
  OF_PP_MAKE_TUPLE_SEQ("sigmoid_v2", Sigmoid)    \
  OF_PP_MAKE_TUPLE_SEQ("sign", Sign)             \
  OF_PP_MAKE_TUPLE_SEQ("sinh", Sinh)             \
  OF_PP_MAKE_TUPLE_SEQ("sqrt", Sqrt)             \
  OF_PP_MAKE_TUPLE_SEQ("square", Square)         \
  OF_PP_MAKE_TUPLE_SEQ("tan", Tan)               \
  OF_PP_MAKE_TUPLE_SEQ("tanh", Tanh)

#define UNARY_ELEMENTWISE_FUNCTOR(op_type_name, class_name, base)                    \
  class class_name##Functor : public base {                                          \
   public:                                                                           \
    class_name##Functor() {                                                          \
      op_ = CHECK_JUST(one::OpBuilder(op_type_name).Input("x").Output("y").Build()); \
    }                                                                                \
  };

#define UNARY_ELEMENTWISE_GRAD_FUNCTOR(op_type_name, class_name, base)                    \
  class class_name##GradFunctor : public base {                                          \
   public:                                                                           \
    class_name##GradFunctor() {                                                          \
      op_ = CHECK_JUST(one::OpBuilder(std::string("") + op_type_name + "_grad").Input("x").Input("dy").Output("dx").Build()); \
    }                                                                                \
  };

#define INPLACE_UNARY_FUNCOTRS(op_type_name, class_name) \
  UNARY_ELEMENTWISE_FUNCTOR(op_type_name, class_name, InplaceUnaryFunctor)

#define INPLACE_FLOAT_UNARY_FUNCOTRS(op_type_name, class_name) \
  UNARY_ELEMENTWISE_FUNCTOR(op_type_name, class_name, InplaceFloatUnaryFunctor) \
  UNARY_ELEMENTWISE_GRAD_FUNCTOR(op_type_name, class_name, BinaryFloatFunctor)
#define UNARY_FUNCOTRS(op_type_name, class_name) \
  UNARY_ELEMENTWISE_FUNCTOR(op_type_name, class_name, UnaryFunctor) \
  UNARY_ELEMENTWISE_GRAD_FUNCTOR(op_type_name, class_name, BinaryFunctor)
#define FLOAT_UNARY_FUNCOTRS(op_type_name, class_name) \
  UNARY_ELEMENTWISE_FUNCTOR(op_type_name, class_name, FloatUnaryFunctor) \
  UNARY_ELEMENTWISE_GRAD_FUNCTOR(op_type_name, class_name, BinaryFunctor)

OF_PP_FOR_EACH_TUPLE(INPLACE_FLOAT_UNARY_FUNCOTRS, INPLACE_UNARY_FLOAT_FUNC_SEQ);
OF_PP_FOR_EACH_TUPLE(UNARY_FUNCOTRS, UNARY_FUNC_SEQ);
OF_PP_FOR_EACH_TUPLE(FLOAT_UNARY_FUNCOTRS, FLOAT_UNARY_FUNC_SEQ);

}  // namespace impl

using namespace impl;
#define ADD_FUNCTOR(class_name, functor_name) \
  m.add_functor<class_name##Functor>(functor_name); \
  m.add_functor<class_name##GradFunctor>(std::string("")+functor_name+"Grad");

ONEFLOW_FUNCTION_LIBRARY(m) {
  ADD_FUNCTOR(Abs, "Abs");
  ADD_FUNCTOR(Acos, "Acos");
  ADD_FUNCTOR(Acosh, "Acosh");
  ADD_FUNCTOR(Asin, "Asin");
  ADD_FUNCTOR(Asinh, "Asinh");
  ADD_FUNCTOR(Atan, "Atan");
  ADD_FUNCTOR(Atanh, "Atanh");
  ADD_FUNCTOR(Ceil, "Ceil");
  ADD_FUNCTOR(Cos, "Cos");
  ADD_FUNCTOR(Cosh, "Cosh");
  ADD_FUNCTOR(Erf, "Erf");
  ADD_FUNCTOR(Erfc, "Erfc");
  ADD_FUNCTOR(Exp, "Exp");
  ADD_FUNCTOR(Expm1, "Expm1");
  ADD_FUNCTOR(Floor, "Floor");
  ADD_FUNCTOR(Lgamma, "Lgamma");
  ADD_FUNCTOR(Log, "Log");
  ADD_FUNCTOR(Log1p, "Log1p");
  ADD_FUNCTOR(LogSigmoid, "LogSigmoid");
  ADD_FUNCTOR(Negative, "Negative");
  ADD_FUNCTOR(Reciprocal, "Reciprocal");
  ADD_FUNCTOR(ReciprocalNoNan, "ReciprocalNoNan");
  ADD_FUNCTOR(Rint, "Rint");
  ADD_FUNCTOR(Round, "Round");
  ADD_FUNCTOR(Rsqrt, "Rsqrt");
  ADD_FUNCTOR(Sigmoid, "Sigmoid");
  ADD_FUNCTOR(Sign, "Sign");
  ADD_FUNCTOR(Sin, "Sin");
  ADD_FUNCTOR(Sinh, "Sinh");
  ADD_FUNCTOR(Softplus, "Softplus");
  ADD_FUNCTOR(Sqrt, "Sqrt");
  ADD_FUNCTOR(Square, "Square");
  ADD_FUNCTOR(Tan, "Tan");
  ADD_FUNCTOR(Tanh, "Tanh");
  // // FIXME: add grad for inplace sin
  m.add_functor<InplaceSinFunctor>("Sin_");
};

}  // namespace functional
}  // namespace one
}  // namespace oneflow
