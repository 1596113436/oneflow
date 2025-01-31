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
#include "oneflow/core/vm/vm_object.h"
#include "oneflow/core/vm/instruction.h"
#include "oneflow/core/common/util.h"

namespace oneflow {
namespace vm {

void RwMutexedObjectAccess::__Init__() {
  clear_instruction();
  clear_mirrored_object();
  clear_rw_mutexed_object();
}

void RwMutexedObjectAccess::__Init__(Instruction* instruction, MirroredObject* mirrored_object,
                                     OperandAccessType access_type) {
  __Init__();
  set_instruction(instruction);
  set_mirrored_object(mirrored_object);
  set_rw_mutexed_object(mirrored_object->mut_rw_mutexed_object());
  set_access_type(access_type);
  mut_mirrored_object_id()->CopyFrom(mirrored_object->mirrored_object_id());
}

void MirroredObject::__Init__(LogicalObject* logical_object, int64_t global_device_id) {
  __Init__();
  mut_mirrored_object_id()->__Init__(logical_object->logical_object_id(), global_device_id);
  set_global_device_id(global_device_id);
  mut_rw_mutexed_object();
}

}  // namespace vm
}  // namespace oneflow
