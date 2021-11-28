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
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/UseDefLists.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Translation.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Parser.h"

#include "llvm-c/Core.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

#include "OneFlow/OneFlowDialect.h"
#include "OneFlow/OneFlowOps.h"
#include "OneFlow/MLIROneFlowTranslation.h"
#include "OneFlow/Passes.h"

#include "oneflow/core/common/data_type.pb.h"
#include "oneflow/core/framework/user_op_conf.pb.h"
#include "oneflow/core/job/job.pb.h"
#include "oneflow/core/operator/op_conf.pb.h"

#include <cstddef>
#include <cstdint>
#include <google/protobuf/text_format.h>
#include <iostream>
#include <fstream>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace mlir {

using PbMessage = google::protobuf::Message;

class JobImporter : Importer {
 public:
  JobImporter(RoundTripOneFlowJobWrapperInterface& job_wrapper, MLIRContext* context,
              ModuleOp module)
      : Importer(context, module), job_(job_wrapper.job()), job_wrapper_(job_wrapper) {}
  virtual ~JobImporter() = default;
  LogicalResult AppendDataInOperand(const std::string& lbn,
                                    std::vector<::mlir::Value>& operand_vec) override;
  LogicalResult AppendCtrlInOperand(const ::oneflow::OperatorConf& op,
                                    std::vector<::mlir::Value>& operand_vec) override;
  LogicalResult AddDeviceName(const ::oneflow::OperatorConf& op,
                              std::vector<NamedAttribute>& attr_vec) override;
  LogicalResult InsertOpResults(const ::oneflow::OperatorConf& op, Operation*) override;
  LogicalResult ProcessSystemOp(const ::oneflow::OperatorConf& op) override;
  LogicalResult ProcessVariableOp(const ::oneflow::OperatorConf& op);
  LogicalResult ProcessInputOp(const ::oneflow::OperatorConf& op_conf);
  LogicalResult ProcessOutputOp(const ::oneflow::OperatorConf& op_conf);
  LogicalResult ProcessJob();
  LogicalResult TryToUpdateJob();
  Type GetTensorTypeOfLbn(const std::string& lbn) override;
  ::oneflow::AttrType QueryAttrType(const std::string& op_type_name,
                                    const std::string& attr_name) override {
    return job_wrapper_.QueryAttrType(op_type_name, attr_name);
  }

 private:
  std::unordered_map<std::string, mlir::OpResult> lbn2result_;
  std::unordered_map<std::string, mlir::OpResult> op_name2ctrl_result_;
  const ::oneflow::Job* job_;
  RoundTripOneFlowJobWrapperInterface& job_wrapper_;
};

LogicalResult JobImporter::AppendCtrlInOperand(const ::oneflow::OperatorConf& op,
                                               std::vector<::mlir::Value>& operand_vec) {
  for (auto& ctrl_in_op_name : op.ctrl_in_op_name()) {
    auto it = op_name2ctrl_result_.find(ctrl_in_op_name);
    if (it == op_name2ctrl_result_.end()) {
      GetModule().emitError("IR result not found for ctrl in op: " + ctrl_in_op_name);
      return failure();
    } else {
      operand_vec.push_back(it->second);
    }
  }
  return success();
}

LogicalResult JobImporter::AppendDataInOperand(const std::string& lbn,
                                               std::vector<::mlir::Value>& operand_vec) {
  auto it = lbn2result_.find(lbn);
  if (it == lbn2result_.end()) {
    GetModule().emitError("IR result not found for: " + lbn);
    return failure();
  } else {
    operand_vec.push_back(it->second);
    return success();
  }
}

LogicalResult JobImporter::InsertOpResults(const ::oneflow::OperatorConf& op_conf, Operation* op) {
  for (const auto& data_out : llvm::enumerate(GetDataOutputResults(op))) {
    auto output_lbns = op->getAttrOfType<ArrayAttr>("output_lbns");
    auto data_out_index = data_out.index();
    lbn2result_.insert({output_lbns[data_out_index].dyn_cast<StringAttr>().getValue().str(),
                        data_out.value().dyn_cast<OpResult>()});
  }
  if (auto ctrl_out = GetCtrlOutputResult(op)) {
    op_name2ctrl_result_.insert({op->getAttrOfType<StringAttr>("op_name").getValue().str(),
                                 ctrl_out->dyn_cast<OpResult>()});
  }
  return success();
}

LogicalResult JobImporter::AddDeviceName(const ::oneflow::OperatorConf& op,
                                         std::vector<NamedAttribute>& attr_vec) {
  const ::oneflow::ParallelConf& pc = job_wrapper_.ParallelConf4OpName(op.name());
  std::vector<llvm::StringRef> device_vec = {pc.device_name().begin(), pc.device_name().end()};
  attr_vec.push_back(
      GetBuilder().getNamedAttr("device_name", GetBuilder().getStrArrayAttr(device_vec)));
  if (pc.has_hierarchy()) {
    attr_vec.push_back(GetBuilder().getNamedAttr(
        "hierarchy",
        GetBuilder().getI64ArrayAttr({pc.hierarchy().dim().begin(), pc.hierarchy().dim().end()})));
  }
  return success();
}

Type JobImporter::GetTensorTypeOfLbn(const std::string& lbn) {
  Type ret = this->GetBuilder().getNoneType();
  job_wrapper_.QueryLogicalBlob(
      lbn,
      [this, &ret](const int64_t* shape_begin, const int64_t* shape_end, ::oneflow::DataType dt) {
        if (auto t = this->GetTypeFromOneFlowDataType(dt)) {
          ret = RankedTensorType::get(ArrayRef<int64_t>(shape_begin, shape_end), t.getValue());
        }
      });
  return ret;
}

LogicalResult JobImporter::ProcessSystemOp(const ::oneflow::OperatorConf& op) {
  if (op.has_user_conf()) {
    GetModule().emitError("Not a sys op. op name: " + op.name());
    return failure();
  }
  if (op.has_variable_conf()) { return ProcessVariableOp(op); }
  if (op.has_input_conf()) { return ProcessInputOp(op); }
  if (op.has_output_conf()) { return ProcessOutputOp(op); }

  auto input_bns_lbns = job_wrapper_.InputBns4OpName(op.name());
  auto input_bns = input_bns_lbns.first;
  auto input_lbns = input_bns_lbns.second;
  auto output_lbns = job_wrapper_.OutputLbns4OpName(op.name());
  job_wrapper_.OutputLbns4OpName(op.name());
  std::vector<NamedAttribute> attr_vec;
  if (failed(AddOpConf(op, attr_vec))) { return failure(); }
  if (failed(AddDeviceName(op, attr_vec))) { return failure(); }
  attr_vec.push_back(GetBuilder().getNamedAttr(
      "input_bns", GetBuilder().getStrArrayAttr(
                       std::vector<llvm::StringRef>({input_bns.begin(), input_bns.end()}))));
  attr_vec.push_back(GetBuilder().getNamedAttr(
      "output_lbns", GetBuilder().getStrArrayAttr(
                         std::vector<llvm::StringRef>({output_lbns.begin(), output_lbns.end()}))));
  OperationState state(FileLineColLoc::get(GetMLIRContext(), op.name(), 0, 0), "oneflow.system");
  attr_vec.push_back(
      GetBuilder().getNamedAttr("op_type_case", GetBuilder().getI32IntegerAttr(op.op_type_case())));
  if (failed(AddOperandSegmentSizes(static_cast<int>(input_lbns.size()), op.ctrl_in_op_name_size(),
                                    attr_vec))) {
    return failure();
  }
  if (failed(AddResultSegmentSizes(output_lbns.size(), attr_vec))) { return failure(); }
  state.addAttributes(attr_vec);
  std::vector<::mlir::Value> operand_vec;
  for (const auto& input_lbn : input_lbns) {
    if (failed(AppendDataInOperand(input_lbn, operand_vec))) { return failure(); }
  }
  if (failed(AppendCtrlInOperand(op, operand_vec))) { return failure(); }
  auto out_types = llvm::SmallVector<Type, 8>();
  for (const auto& output_lbn : output_lbns) {
    out_types.push_back(GetTensorTypeOfLbn(output_lbn));
  }
  if (failed(AppendCtrlOutType(out_types))) { return failure(); }
  state.addOperands(operand_vec);
  state.addTypes(out_types);
  auto created_op = GetBuilder().createOperation(state);
  if (failed(InsertOpResults(op, created_op))) { return failure(); }
  if (!created_op) {
    GetModule()->emitError("fail to create op, name: " + op.name());
    return failure();
  }
  return success();
}

LogicalResult JobImporter::ProcessVariableOp(const ::oneflow::OperatorConf& op_conf) {
  if (!op_conf.has_variable_conf()) {
    GetModule().emitError("Not a variable op. op name: " + op_conf.name());
    return failure();
  }

  if (op_conf.variable_conf().has_tick()) {
    GetModule().emitError("variable op has tick input. op name: " + op_conf.name());
    return failure();
  }

  OperationState state(FileLineColLoc::get(GetMLIRContext(), op_conf.name(), 0, 0),
                       "oneflow.variable");
  // attrs
  std::vector<NamedAttribute> attr_vec;
  if (failed(AddOpConf(op_conf, attr_vec))) { return failure(); }
  if (failed(AddDeviceName(op_conf, attr_vec))) { return failure(); }
  // attr output_lbns
  auto output_lbns_attr = GetBuilder().getStrArrayAttr({op_conf.name() + "/out"});
  attr_vec.emplace_back(GetBuilder().getNamedAttr("output_lbns", output_lbns_attr));
  // attr shape
  auto shape_attr = GetBuilder().getI64VectorAttr(
      {op_conf.variable_conf().shape().dim().begin(), op_conf.variable_conf().shape().dim().end()});
  auto shape_named_attr = GetBuilder().getNamedAttr("shape", shape_attr);
  attr_vec.emplace_back(shape_named_attr);
  // attr data_type
  if (op_conf.variable_conf().has_data_type()) {
    std::string dtype_str;
    if (failed(StringifyDataType(op_conf.variable_conf().data_type(), dtype_str))) {
      return failure();
    }
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("data_type", GetBuilder().getStringAttr(dtype_str)));
  }
  // attr model_name
  if (op_conf.variable_conf().has_model_name()) {
    const std::string& model_name = op_conf.variable_conf().model_name();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("model_name", GetBuilder().getStringAttr(model_name)));
  }
  // attr l1 l2 regularization
  if (op_conf.variable_conf().has_regularizer()
      && op_conf.variable_conf().regularizer().has_l1_l2_conf()) {
    if (op_conf.variable_conf().regularizer().l1_l2_conf().has_l1()) {
      float l1_regularization = op_conf.variable_conf().regularizer().l1_l2_conf().l1();
      attr_vec.emplace_back(GetBuilder().getNamedAttr(
          "l1_regularization", GetBuilder().getF32FloatAttr(l1_regularization)));
    }
    if (op_conf.variable_conf().regularizer().l1_l2_conf().has_l2()) {
      float l2_regularization = op_conf.variable_conf().regularizer().l1_l2_conf().l2();
      attr_vec.emplace_back(GetBuilder().getNamedAttr(
          "l2_regularization", GetBuilder().getF32FloatAttr(l2_regularization)));
    }
  }
  // attr trainable
  if (op_conf.variable_conf().has_trainable()) {
    bool trainable = op_conf.variable_conf().trainable();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("trainable", GetBuilder().getBoolAttr(trainable)));
  }
  // attr nd_sbp
  const std::vector<StringRef> nd_sbp_str_vec{op_conf.variable_conf().nd_sbp().begin(),
                                              op_conf.variable_conf().nd_sbp().end()};
  auto nd_sbp_attr = GetBuilder().getStrArrayAttr(makeArrayRef(nd_sbp_str_vec));
  attr_vec.emplace_back(GetBuilder().getNamedAttr("nd_sbp", nd_sbp_attr));
  // trait_attr operand_segment_sizes
  // if (failed(AddOperandSegmentSizes(0, op_conf.ctrl_in_op_name_size(), attr_vec))) {
  //   return failure();
  // }
  // add attrs
  state.addAttributes(attr_vec);
  // operands
  std::vector<::mlir::Value> operand_vec;
  if (failed(AppendCtrlInOperand(op_conf, operand_vec))) { return failure(); }
  state.addOperands(operand_vec);
  // result types
  llvm::SmallVector<Type, 8> out_types;
  auto output_lbn = op_conf.name() + "/out";
  out_types.push_back(GetTensorTypeOfLbn(output_lbn));
  if (failed(AppendCtrlOutType(out_types))) { return failure(); }
  state.addTypes(out_types);
  // create op
  auto op = GetBuilder().createOperation(state);
  if (!op) {
    GetModule()->emitError("fail to create op, name: " + op_conf.name());
    return failure();
  }
  // record result
  if (op->getNumResults() != 2) {
    op->emitError("variable op should has two results (out and ctrl_output), but got "
                  + std::to_string(op->getNumResults()) + "\n");
    return failure();
  }
  if (!lbn2result_.emplace(output_lbn, op->getResult(0)).second) {
    op->emitError("lbn already exists, lbn: ") << output_lbn;
    return failure();
  }
  if (!op_name2ctrl_result_.emplace(op_conf.name(), op->getResult(1)).second) {
    op->emitError("ctrl output already exists, op_name: ") << op_conf.name();
    return failure();
  }
  return success();
}

LogicalResult JobImporter::ProcessInputOp(const ::oneflow::OperatorConf& op_conf) {
  if (!op_conf.has_input_conf()) {
    GetModule().emitError("Not a input op. op name: " + op_conf.name());
    return failure();
  }

  if (op_conf.input_conf().has_tick()) {
    GetModule().emitError("input op has tick input. op name: " + op_conf.name());
    return failure();
  }

  OperationState state(FileLineColLoc::get(GetMLIRContext(), op_conf.name(), 0, 0),
                       "oneflow.input");
  // attrs
  std::vector<NamedAttribute> attr_vec;
  if (failed(AddOpConf(op_conf, attr_vec))) { return failure(); }
  if (failed(AddDeviceName(op_conf, attr_vec))) { return failure(); }
  // attr output_lbns
  auto output_lbns_attr = GetBuilder().getStrArrayAttr({op_conf.name() + "/out"});
  attr_vec.emplace_back(GetBuilder().getNamedAttr("output_lbns", output_lbns_attr));
  // attr shape
  if (op_conf.input_conf().blob_conf().has_shape()) {
    auto shape_attr =
        GetBuilder().getI64VectorAttr({op_conf.input_conf().blob_conf().shape().dim().begin(),
                                       op_conf.input_conf().blob_conf().shape().dim().end()});
    attr_vec.emplace_back(GetBuilder().getNamedAttr("shape", shape_attr));
  }
  // attr data_type
  if (op_conf.input_conf().blob_conf().has_data_type()) {
    std::string dtype_str;
    if (failed(StringifyDataType(op_conf.input_conf().blob_conf().data_type(), dtype_str))) {
      return failure();
    }
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("data_type", GetBuilder().getStringAttr(dtype_str)));
  }
  // attr is_dynamic
  if (op_conf.input_conf().blob_conf().has_is_dynamic()) {
    bool is_dynamic = op_conf.input_conf().blob_conf().is_dynamic();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("is_dynamic", GetBuilder().getBoolAttr(is_dynamic)));
  }
  // attr nd_sbp
  if (op_conf.input_conf().blob_conf().has_nd_sbp()) {
    std::vector<StringRef> nd_sbp_strref_vec;
    nd_sbp_strref_vec.reserve(op_conf.input_conf().blob_conf().nd_sbp().sbp_parallel_size());
    for (const auto& sbp : op_conf.input_conf().blob_conf().nd_sbp().sbp_parallel()) {
      if (sbp.has_split_parallel()) {
        nd_sbp_strref_vec.emplace_back("S(" + std::to_string(sbp.split_parallel().axis()) + ")");
      } else if (sbp.has_broadcast_parallel()) {
        nd_sbp_strref_vec.emplace_back("B");
      } else if (sbp.has_partial_sum_parallel()) {
        nd_sbp_strref_vec.emplace_back("P");
      } else {
        GetModule().emitError("unsupported sbp");
      }
    }
    auto nd_sbp_attr = GetBuilder().getStrArrayAttr(makeArrayRef(nd_sbp_strref_vec));
    attr_vec.emplace_back(GetBuilder().getNamedAttr("nd_sbp", nd_sbp_attr));
  }
  // attr job_name
  if (op_conf.input_conf().has_job_name()) {
    const std::string& job_name = op_conf.input_conf().job_name();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("job_name", GetBuilder().getStringAttr(job_name)));
  }
  // trait_attr operand_segment_sizes
  // if (failed(AddOperandSegmentSizes(0, op_conf.ctrl_in_op_name_size(), attr_vec))) {
  //   return failure();
  // }
  // add attrs
  state.addAttributes(attr_vec);
  // operands
  std::vector<::mlir::Value> operand_vec;
  if (failed(AppendCtrlInOperand(op_conf, operand_vec))) { return failure(); }
  state.addOperands(operand_vec);
  // result types
  llvm::SmallVector<Type, 8> out_types;
  auto output_lbn = op_conf.name() + "/out";
  out_types.push_back(GetTensorTypeOfLbn(output_lbn));
  if (failed(AppendCtrlOutType(out_types))) { return failure(); }
  state.addTypes(out_types);
  // create op
  auto op = GetBuilder().createOperation(state);
  if (!op) {
    GetModule()->emitError("fail to create op, name: " + op_conf.name());
    return failure();
  }
  // record result
  if (op->getNumResults() != 2) {
    op->emitError("input op should has two results (out and ctrl_output), but got "
                  + std::to_string(op->getNumResults()) + "\n");
    return failure();
  }
  if (!lbn2result_.emplace(output_lbn, op->getResult(0)).second) {
    op->emitError("lbn already exists, lbn: ") << output_lbn;
    return failure();
  }
  if (!op_name2ctrl_result_.emplace(op_conf.name(), op->getResult(1)).second) {
    op->emitError("ctrl output already exists, op_name: ") << op_conf.name();
    return failure();
  }
  return success();
}

LogicalResult JobImporter::ProcessOutputOp(const ::oneflow::OperatorConf& op_conf) {
  if (!op_conf.has_output_conf()) {
    GetModule().emitError("Not a output op. op name: " + op_conf.name());
    return failure();
  }

  OperationState state(FileLineColLoc::get(GetMLIRContext(), op_conf.name(), 0, 0),
                       "oneflow.output");
  // attrs
  std::vector<NamedAttribute> attr_vec;
  if (failed(AddOpConf(op_conf, attr_vec))) { return failure(); }
  if (failed(AddDeviceName(op_conf, attr_vec))) { return failure(); }
  // attr output_lbns
  auto output_lbns_attr = GetBuilder().getStrArrayAttr({op_conf.name() + "/out"});
  attr_vec.emplace_back(GetBuilder().getNamedAttr("output_lbns", output_lbns_attr));
  // attr shape
  if (op_conf.output_conf().blob_conf().has_shape()) {
    auto shape_attr =
        GetBuilder().getI64VectorAttr({op_conf.output_conf().blob_conf().shape().dim().begin(),
                                       op_conf.output_conf().blob_conf().shape().dim().end()});
    attr_vec.emplace_back(GetBuilder().getNamedAttr("shape", shape_attr));
  }
  // attr data_type
  if (op_conf.output_conf().blob_conf().has_data_type()) {
    std::string dtype_str;
    if (failed(StringifyDataType(op_conf.output_conf().blob_conf().data_type(), dtype_str))) {
      return failure();
    }
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("data_type", GetBuilder().getStringAttr(dtype_str)));
  }
  // attr is_dynamic
  if (op_conf.output_conf().blob_conf().has_is_dynamic()) {
    bool is_dynamic = op_conf.output_conf().blob_conf().is_dynamic();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("is_dynamic", GetBuilder().getBoolAttr(is_dynamic)));
  }
  // attr nd_sbp
  if (op_conf.output_conf().blob_conf().has_nd_sbp()) {
    std::vector<StringRef> nd_sbp_strref_vec;
    nd_sbp_strref_vec.reserve(op_conf.output_conf().blob_conf().nd_sbp().sbp_parallel_size());
    for (const auto& sbp : op_conf.output_conf().blob_conf().nd_sbp().sbp_parallel()) {
      if (sbp.has_split_parallel()) {
        nd_sbp_strref_vec.emplace_back("S(" + std::to_string(sbp.split_parallel().axis()) + ")");
      } else if (sbp.has_broadcast_parallel()) {
        nd_sbp_strref_vec.emplace_back("B");
      } else if (sbp.has_partial_sum_parallel()) {
        nd_sbp_strref_vec.emplace_back("P");
      } else {
        GetModule().emitError("unsupported sbp");
      }
    }
    auto nd_sbp_attr = GetBuilder().getStrArrayAttr(makeArrayRef(nd_sbp_strref_vec));
    attr_vec.emplace_back(GetBuilder().getNamedAttr("nd_sbp", nd_sbp_attr));
  }
  // attr job_name
  if (op_conf.output_conf().has_job_name()) {
    const std::string& job_name = op_conf.output_conf().job_name();
    attr_vec.emplace_back(
        GetBuilder().getNamedAttr("job_name", GetBuilder().getStringAttr(job_name)));
  }
  // add attrs
  state.addAttributes(attr_vec);
  // operands
  std::vector<::mlir::Value> operand_vec;
  auto input_bns_lbns = job_wrapper_.InputBns4OpName(op_conf.name());
  if (input_bns_lbns.second.size() != 1) {
    GetModule()->emitError("output op should has only one input, op_name: " + op_conf.name());
    return failure();
  }
  if (failed(AppendDataInOperand(input_bns_lbns.second[0], operand_vec))) { return failure(); }
  if (failed(AppendCtrlInOperand(op_conf, operand_vec))) { return failure(); }
  state.addOperands(operand_vec);
  // result types
  llvm::SmallVector<Type, 8> out_types;
  auto output_lbn = op_conf.name() + "/out";
  out_types.push_back(GetTensorTypeOfLbn(output_lbn));
  if (failed(AppendCtrlOutType(out_types))) { return failure(); }
  state.addTypes(out_types);
  // create op
  auto op = GetBuilder().createOperation(state);
  if (!op) {
    GetModule()->emitError("fail to create op, name: " + op_conf.name());
    return failure();
  }
  // record result
  if (op->getNumResults() != 2) {
    op->emitError("output_conf op should has two results (out and ctrl_output), but got "
                  + std::to_string(op->getNumResults()) + "\n");
    return failure();
  }
  if (!lbn2result_.emplace(output_lbn, op->getResult(0)).second) {
    op->emitError("lbn already exists, lbn: ") << output_lbn;
    return failure();
  }
  if (!op_name2ctrl_result_.emplace(op_conf.name(), op->getResult(1)).second) {
    op->emitError("ctrl output already exists, op_name: ") << op_conf.name();
    return failure();
  }
  return success();
}

LogicalResult JobImporter::ProcessJob() {
  auto func_type = GetBuilder().getFunctionType(llvm::None, llvm::None);
  auto function = mlir::FuncOp::create(GetRootLocation(), job_->job_conf().job_name(), func_type);
  auto& entryBlock = *function.addEntryBlock();
  GetBuilder().setInsertionPointToStart(&entryBlock);

  bool is_succeeded = true;
  job_wrapper_.TopoForEachOpConf([&](const ::oneflow::OperatorConf* op_conf) {
    const auto op = *op_conf;
    if (is_succeeded == false) { return; }
    if (op.has_user_conf()) {
      is_succeeded = succeeded(ProcessUserOp(op));
    } else {
      is_succeeded = succeeded(ProcessSystemOp(op));
    }
  });
  if (is_succeeded == false) { return failure(); }

  ReturnOp returnOp;
  if (!entryBlock.empty()) { returnOp = dyn_cast<ReturnOp>(entryBlock.back()); }
  if (!returnOp) { GetBuilder().create<ReturnOp>(GetRootLocation()); }
  GetModule().push_back(function);
  return success();
}

template<typename OpType, typename AdaptorType>
void UpdatePlacement(OpType* op, AdaptorType& adaptor, ::oneflow::Job& job) {
  auto* pg = job.mutable_placement()->add_placement_group();
  pg->mutable_op_set()->add_op_name(adaptor.op_name().getValue().str());
  pg->mutable_parallel_conf()->set_device_tag(adaptor.device_tag().getValue().str());
  for (auto p : adaptor.device_name()) {
    pg->mutable_parallel_conf()->add_device_name(
        p.template dyn_cast<StringAttr>().getValue().str());
  }
  if (adaptor.hierarchy()) {
    for (auto dim : adaptor.hierarchy()) {
      pg->mutable_parallel_conf()->mutable_hierarchy()->add_dim(
          dim.template dyn_cast<IntegerAttr>().getInt());
    }
  }
}

LogicalResult JobImporter::TryToUpdateJob() {
  auto new_job = ::oneflow::Job();
  new_job.CopyFrom(*job_);
  new_job.clear_net();
  new_job.mutable_placement()->clear_placement_group();
  auto convertOps = [&](Operation* op) {
    if (llvm::dyn_cast<oneflow::UserOp>(op) || op->hasAttr("op_type_name")) {
      oneflow::UserOpAdaptor user_op_adaptor(op->getOperands(), op->getAttrDictionary());
      UpdatePlacement(op, user_op_adaptor, new_job);
      ::oneflow::OperatorConf op_conf;
      auto user_conf = op_conf.mutable_user_conf();
      if (succeeded(ConvertUserOpInputs(op, user_op_adaptor, user_conf))
          && succeeded(ConvertUserOpOutputs(op, user_op_adaptor, user_conf))
          && succeeded(ConvertUserOpAttributes(op, user_op_adaptor, op_conf))
          && succeeded(ConvertCtrlInputs(op, op_conf))) {
        *(new_job.mutable_net()->add_op()) = op_conf;
      } else {
        return WalkResult::interrupt();
      }
    } else if (llvm::dyn_cast<oneflow::SystemOp>(op)) {
      oneflow::SystemOpAdaptor system_op_adaptor(op->getOperands(), op->getAttrDictionary());
      UpdatePlacement(op, system_op_adaptor, new_job);
      auto op_name = system_op_adaptor.op_name().getValue().str();
      ::oneflow::OperatorConf op_conf = job_wrapper_.OpConf4OpName(op_name);
      for (auto ibn : llvm::enumerate(op->getAttrOfType<ArrayAttr>("input_bns"))) {
        auto result = GetDataInputOperands(op)[ibn.index()].dyn_cast<OpResult>();
        std::string new_val =
            result.getDefiningOp()
                ->getAttrOfType<ArrayAttr>("output_lbns")[result.getResultNumber()]
                .dyn_cast<StringAttr>()
                .getValue()
                .str();
        job_wrapper_.ReplaceInputLbnInOpCustomizedConf(
            &op_conf, ibn.value().dyn_cast<StringAttr>().getValue().str(), new_val);
      }
      if (succeeded(ConvertCtrlInputs(op, op_conf))) {
        *(new_job.mutable_net()->add_op()) = op_conf;
      } else {
        return WalkResult::interrupt();
      }
    } else if (llvm::dyn_cast<oneflow::VariableOp>(op)) {
      oneflow::VariableOpAdaptor op_adaptor(op->getOperands(), op->getAttrDictionary());
      UpdatePlacement(op, op_adaptor, new_job);
      ::oneflow::OperatorConf op_conf;
      if (succeeded(ConvertVariableOpConf(op, op_adaptor, &op_conf))) {
        *(new_job.mutable_net()->add_op()) = op_conf;
      } else {
        return WalkResult::interrupt();
      }
    } else if (llvm::dyn_cast<oneflow::InputOp>(op)) {
      oneflow::InputOpAdaptor op_adaptor(op->getOperands(), op->getAttrDictionary());
      UpdatePlacement(op, op_adaptor, new_job);
      ::oneflow::OperatorConf op_conf;
      if (succeeded(ConvertInputOpConf(op, op_adaptor, &op_conf))) {
        *(new_job.mutable_net()->add_op()) = op_conf;
      } else {
        return WalkResult::interrupt();
      }
    } else if (llvm::dyn_cast<oneflow::OutputOp>(op)) {
      oneflow::OutputOpAdaptor op_adaptor(op->getOperands(), op->getAttrDictionary());
      UpdatePlacement(op, op_adaptor, new_job);
      ::oneflow::OperatorConf op_conf;
      if (succeeded(ConvertOutputOpConf(op, op_adaptor, &op_conf))) {
        *(new_job.mutable_net()->add_op()) = op_conf;
      } else {
        return WalkResult::interrupt();
      }
    } else if (llvm::dyn_cast<ReturnOp>(op) || llvm::dyn_cast<FuncOp>(op)
               || llvm::dyn_cast<ModuleOp>(op)) {
      return WalkResult::advance();
    } else {
      op->emitError("failed to convert op: " + op->getName().getStringRef().str()) << "\n" << *op;
      return WalkResult::interrupt();
    } /* convert op conf */
    return WalkResult::advance();
  };
  Operation* func_op = nullptr;
  auto walk_result = GetModule().getOperation()->walk([&func_op](FuncOp op) {
    if (func_op != nullptr) { return WalkResult::interrupt(); }
    func_op = op.getOperation();
    return WalkResult::advance();
  });
  if (walk_result.wasInterrupted()) {
    GetModule()->emitError("find multiply func op");
    return failure();
  }
  if (!func_op) {
    GetModule()->emitError("find no func op");
    return failure();
  }
  if (func_op->walk(convertOps).wasInterrupted()) {
    return failure();
  } else {
    job_wrapper_.UpdateJob(&new_job);
  }
  return success();
}

LogicalResult ApplyRoundTripPatterns(RoundTripOneFlowJobWrapperInterface& job_wrapper,
                                     MLIRContext* context, OwningModuleRef& module) {
  mlir::PassManager pm(context);
  pm.addNestedPass<mlir::FuncOp>(::mlir::createCanonicalizerPass());
  std::string graphviz;
  if (job_wrapper.IsLastIRPass() && std::getenv("ONEFLOW_MLIR_ENABLE_CODEGEN_FUSERS") != nullptr) {
    pm.addPass(oneflow::createOutlineJitFunctionPass());
  }
  pm.addNestedPass<mlir::FuncOp>(oneflow::createFuseIntoExistingOpPass());
  pm.addNestedPass<mlir::FuncOp>(::mlir::createCanonicalizerPass());
  llvm::raw_string_ostream os_graphviz(graphviz);
  pm.addPass(createPrintOpGraphPass(os_graphviz));
  if (mlir::failed(pm.run(*module))) {
    module->emitError("Failed to run canonicalizer pass");
    return failure();
  }
  job_wrapper.DumpLog("RoundTripOneFlowJob.mlir.dot", graphviz);
  std::string mlir;
  llvm::raw_string_ostream os_mlir(mlir);
  module->print(os_mlir);
  job_wrapper.DumpLog("RoundTripOneFlowJob.mlir", mlir);
  return success();
}

OwningModuleRef TranslateOneFlowJobToModule(llvm::StringRef str, MLIRContext* context) {
  std::string cpp_str = str.str();
  ::oneflow::Job job;
  google::protobuf::TextFormat::ParseFromString(cpp_str, &job);
  context->loadDialect<oneflow::OneFlowDialect>();
  context->loadDialect<StandardOpsDialect>();
  OwningModuleRef module(
      ModuleOp::create(FileLineColLoc::get(context, "", /*line=*/0, /*column=*/0)));
  return module;
}

void RoundTripOneFlowJob(
    RoundTripOneFlowJobWrapperInterface& job_wrapper,
    const std::function<bool(::oneflow::Job* job, std::string& reason)>& is_legit_job) {
  const ::oneflow::Job* job = job_wrapper.job();
  mlir::MLIRContext context;
  context.getOrLoadDialect<oneflow::OneFlowDialect>();
  context.loadDialect<StandardOpsDialect>();

  OwningModuleRef module(
      ModuleOp::create(FileLineColLoc::get(&context, "", /*line=*/0, /*column=*/0)));
  JobImporter imp(job_wrapper, &context, module.get());
  // TODO: Add flag in job desc to decide whether to run mlir optimizer
  if (succeeded(imp.ProcessJob())) {
    if (failed(ApplyRoundTripPatterns(job_wrapper, &context, module))) { exit(EXIT_FAILURE); }
    if (std::getenv("ONEFLOW_MLIR_STDOUT") != nullptr) { module->print(llvm::outs()); }
    // TODO: Add flag in oneflow to define if failure in MLIR is allowed
    if (failed(imp.TryToUpdateJob())) {
      llvm::errs() << "fail to update job with IR, job will stay intact, job_name: "
                   << job->job_conf().job_name() << "\n";
      exit(EXIT_FAILURE);
    }
  } else {
    llvm::errs() << "fail to convert job to IR, job_name: " << job->job_conf().job_name() << "\n";
    exit(EXIT_FAILURE);
  }
}

void SaveJobToIR(RoundTripOneFlowJobWrapperInterface& job_wrapper, const std::string& path) {
  const ::oneflow::Job* job = job_wrapper.job();
  mlir::MLIRContext context;
  context.getOrLoadDialect<oneflow::OneFlowDialect>();
  context.loadDialect<StandardOpsDialect>();

  OwningModuleRef module(
      ModuleOp::create(FileLineColLoc::get(&context, "", /*line=*/0, /*column=*/0)));
  JobImporter imp(job_wrapper, &context, module.get());
  if (succeeded(imp.ProcessJob())) {
    mlir::PassManager pm(&context);
    pm.addNestedPass<mlir::FuncOp>(::mlir::createCanonicalizerPass());
    if (mlir::failed(pm.run(*module))) {
      module->emitError("Failed to run canonicalizer pass");
      exit(EXIT_FAILURE);
    }

    std::string mlir;
    llvm::raw_string_ostream os_mlir(mlir);
    module->print(os_mlir);
    const auto& job_name = job->job_conf().job_name();
    std::string filename = path + "/" + job_name + ".mlir";
    std::ofstream fs(filename, std::ios::trunc);
    if (!fs.is_open()) {
      llvm::errs() << "fail to open file " << filename;
      exit(EXIT_FAILURE);
    }
    fs << mlir;
    fs.close();
  } else {
    llvm::errs() << "fail to convert job to IR, job_name: " << job->job_conf().job_name() << "\n";
    exit(EXIT_FAILURE);
  }
}

void LoadJobFromIR(RoundTripOneFlowJobWrapperInterface& job_wrapper, const std::string& path) {
  MLIRContext context;
  context.getOrLoadDialect<oneflow::OneFlowDialect>();
  context.loadDialect<StandardOpsDialect>();
  OwningModuleRef module = parseSourceFile<ModuleOp>(path, &context);
  JobImporter imp(job_wrapper, &context, module.get());
  if (failed(imp.TryToUpdateJob())) {
    llvm::errs() << "fail to load job from IR";
    exit(EXIT_FAILURE);
  }
}

void registerFromOneFlowJobTranslation() {
  TranslateToMLIRRegistration fromOneFlowJob("import-oneflow-job",
                                             [](llvm::StringRef str, MLIRContext* context) {
                                               return TranslateOneFlowJobToModule(str, context);
                                             });
}

}  // namespace mlir
