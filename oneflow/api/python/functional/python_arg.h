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

#ifndef ONEFLOW_API_PYTHON_FUNCTIONAL_PYTHON_ARG_H_
#define ONEFLOW_API_PYTHON_FUNCTIONAL_PYTHON_ARG_H_

#include <pybind11/pybind11.h>

#include "oneflow/core/framework/attr_map.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/tensor_tuple.h"
#include "oneflow/core/framework/user_op_attr.cfg.h"
#include "oneflow/core/functional/scalar.h"
#include "oneflow/api/python/functional/common.h"

namespace py = pybind11;

namespace oneflow {
namespace one {
namespace functional {

class PythonArg {
 public:
  PythonArg() = default;
  PythonArg(py::object value) : value_(value.ptr()) {}

  virtual ~PythonArg() = default;

#define IMPLICIT_TRANSFORM_OP(T) \
  operator T() const { return py::cast<T>(Borrow()); }

  OF_PP_FOR_EACH_TUPLE(IMPLICIT_TRANSFORM_OP,
                       ARITHMETIC_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(std::string));
#undef IMPLICIT_TRANSFORM_OP

  operator std::vector<int32_t>() const;
  operator std::vector<uint32_t>() const;
  operator std::vector<int64_t>() const;
  operator std::vector<uint64_t>() const;
  operator std::vector<float>() const;
  operator std::vector<double>() const;
  operator std::vector<bool>() const;
  operator std::vector<std::string>() const;

  operator Scalar() const;

  operator std::shared_ptr<one::Tensor>() const {
    return py::cast<std::shared_ptr<one::Tensor>>(Borrow());
  }

  operator std::shared_ptr<one::TensorTuple>() const;
  operator one::TensorTuple() const { return *(std::shared_ptr<one::TensorTuple>(*this)); }

  operator std::shared_ptr<cfg::AttrValue>() const;

  operator AttrMap() const;

 private:
  py::object Borrow() const { return py::reinterpret_borrow<py::object>(value_); }
  PyObject* value_;
};

}  // namespace functional
}  // namespace one
}  // namespace oneflow

#endif  // ONEFLOW_API_PYTHON_FUNCTIONAL_PYTHON_ARG_H_
