#ifndef ONEFLOW_DIALECT
#define ONEFLOW_DIALECT

include "mlir/IR/OpBase.td"

def OneFlow_Dialect : Dialect {
    let name = "oneflow";
    let summary = "OneFlow MLIR dialect.";
    let description = [{
        This dialect is the IR of OneFlow.
    }];
    let cppNamespace = "::mlir::oneflow";
    let dependentDialects = [
        "StandardOpsDialect"
    ];
}

#endif // ONEFLOW_DIALECT
