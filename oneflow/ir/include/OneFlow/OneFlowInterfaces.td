#ifndef ONEFLOW_IR_INCLUDE_ONEFLOW_ONEFLOWINTERFACES_H_
#define ONEFLOW_IR_INCLUDE_ONEFLOW_ONEFLOWINTERFACES_H_

include "mlir/IR/OpBase.td"

def UserOpCompatibleInterface : OpInterface<"UserOpCompatible"> {
  let description = [{
    Interface to getting the hard-coded bn
  }];

  let methods = [
    StaticInterfaceMethod<"",
        "const std::vector<std::string>*", "inputKeys", (ins), [{
        static std::vector<std::string> val(mlir::oneflow::support::GetInputKeys(ConcreteOp::getOperationName().split('.').second.str()));
        return &val;
    }]>,
    StaticInterfaceMethod<"",
        "const std::vector<std::string>*", "outputKeys", (ins), [{
        static std::vector<std::string> val(mlir::oneflow::support::GetOutputKeys(ConcreteOp::getOperationName().split('.').second.str()));
        return &val;
    }]>,
    InterfaceMethod<"",
        "std::pair<unsigned, unsigned>", "getODSOperandIndexAndLength", (ins "unsigned":$index), [{
        return $_op.getODSOperandIndexAndLength(index);
    }]>,
    InterfaceMethod<"",
        "std::pair<unsigned, unsigned>", "getODSResultIndexAndLength", (ins "unsigned":$index), [{
        return $_op.getODSResultIndexAndLength(index);
    }]>
  ];
}

def AlternativeOpTypeNameInterface : OpInterface<"HasAlternativeOpTypeName"> {
  let description = [{
    Interface to getting control edges
  }];

  let methods = [
    StaticInterfaceMethod<"",
        "std::string", "getOriginalOpTypeName", (ins)
    >,
    StaticInterfaceMethod<"",
        "const std::vector<std::string>*", "inputKeys", (ins), [{
        static std::vector<std::string> val(mlir::oneflow::support::GetInputKeys(ConcreteOp::getOriginalOpTypeName()));
        return &val;
    }]>,
    StaticInterfaceMethod<"",
        "const std::vector<std::string>*", "outputKeys", (ins), [{
        static std::vector<std::string> val(mlir::oneflow::support::GetOutputKeys(ConcreteOp::getOriginalOpTypeName()));
        return &val;
    }]>,
  ];
}

def ControlEdgeCompatibleInterface : OpInterface<"ControlEdgeCompatible"> {
  let description = [{
    Interface to getting control edges
  }];

  let methods = [
    InterfaceMethod<"",
        "::mlir::OperandRange", "dataInputOperands", (ins)
    >,
    InterfaceMethod<"",
        "::mlir::OperandRange", "ctrlInputOperands", (ins)
    >,
    InterfaceMethod<"",
        "::mlir::ResultRange", "dataOutputResults", (ins)
    >,
    InterfaceMethod<"",
        "::mlir::Value", "ctrlOutputResult", (ins)
    >
  ];
}

def NoGrad : OpInterface<"NoGrad"> {
  let description = [{
  }];
}

def CpuOnly : OpInterface<"CpuOnly"> {
  let description = [{
  }];
}


#endif  // ONEFLOW_IR_INCLUDE_ONEFLOW_ONEFLOWINTERFACES_H_
