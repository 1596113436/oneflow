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
#include "oneflow/core/eager/critical_section_phy_instr_operand.h"
#include "oneflow/core/framework/device.h"
#include "oneflow/core/kernel/kernel_util.h"
#include "oneflow/core/common/decorator.h"
#include "oneflow/core/device/device_context.h"
#include "oneflow/core/device/cuda_event_record.h"
#include "oneflow/core/register/ofblob.h"
#include "oneflow/core/common/container_util.h"

namespace oneflow {
namespace vm {

void CriticalSectionBeginPhyInstrOperand::ForEachMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  for (const auto& eager_blob_object : *eager_blob_objects_) {
    DoEach(nullptr,
           CHECK_JUST(eager_blob_object->compute_local_dep_object())->mut_mirrored_object());
  }
}

void CriticalSectionEndPhyInstrOperand::ForEachMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  DoEach(nullptr,
         CHECK_JUST(eager_blob_object_->compute_local_dep_object())->mut_mirrored_object());
}

namespace {

Maybe<LocalDepObject*> RawCriticalSectionLocalDepObject() {
  return JUST(Device::New("critical_section"))->mut_schedule_local_dep_object();
}

constexpr auto* CriticalSectionLocalDepObject =
    DECORATE(&RawCriticalSectionLocalDepObject, ThreadLocal);

}  // namespace

void CriticalSectionBeginPhyInstrOperand::ForEachMutMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  DoEach(nullptr, CHECK_JUST(CriticalSectionLocalDepObject())->mut_mirrored_object());
}

void CriticalSectionBeginPhyInstrOperand::FinishInvalidInterfaceEventRecords() {
  for (const auto& op_name : interfaces_op_names()) {
    size_t index = CHECK_JUST(MapAt(op_name2interface_index_, op_name));
    if (!interfaces_valid().at(index)) {
      const auto& iter = op_name2end_event_record_->find(op_name);
      CHECK(iter != op_name2end_event_record_->end());
      iter->second->Init(std::make_shared<NaiveEventRecord>());
    }
  }
}

void CriticalSectionBeginPhyInstrOperand::Finish() {
  for (const auto& pair : *op_name2end_event_record_) {
    pair.second->TryInit(std::make_shared<NaiveEventRecord>());
  }
}

void InputCriticalSectionBeginPhyInstrOperand::AccessBlobByCallback(int64_t of_blob_ptr,
                                                                    const std::string& op_name) {
  int64_t i = CHECK_JUST(MapAt(op_name2interface_index_, op_name));
  CHECK(interfaces_valid().at(i));
  OfBlob* of_blob = reinterpret_cast<OfBlob*>(of_blob_ptr);
  const auto& eager_blob_object = eager_blob_objects_->at(i);
  const Blob* blob = &eager_blob_object->blob();
  CHECK_NOTNULL(blob);
  of_blob->mut_blob()->CopyHeaderFrom(of_blob->mut_device_ctx(), blob);
  const auto& end_event_record = op_name2end_event_record_->at(op_name);
  if (blob->dptr() == nullptr) {
    end_event_record->Init(std::make_shared<NaiveEventRecord>());
  } else {
    AutoMemcpy(of_blob->mut_device_ctx(), of_blob->mut_blob(), blob);
    auto* event_record_provider =
        CHECK_NOTNULL(dynamic_cast<EventRecordProvider*>(of_blob->mut_device_ctx()));
    end_event_record->Init(event_record_provider->MakeEventRecord());
  }
}

void OutputCriticalSectionBeginPhyInstrOperand::AccessBlobByCallback(int64_t of_blob_ptr,
                                                                     const std::string& op_name) {
  int64_t i = CHECK_JUST(MapAt(op_name2interface_index_, op_name));
  CHECK(interfaces_valid().at(i));
  OfBlob* of_blob = reinterpret_cast<OfBlob*>(of_blob_ptr);
  const auto& eager_blob_object = eager_blob_objects_->at(i);
  Blob* mut_blob = eager_blob_object->mut_blob();
  CHECK_NOTNULL(mut_blob);
  mut_blob->CopyHeaderFrom(of_blob->mut_device_ctx(), &of_blob->blob());
  const auto& end_event_record = op_name2end_event_record_->at(op_name);
  if (mut_blob->dptr() == nullptr) {
    end_event_record->Init(std::make_shared<NaiveEventRecord>());
  } else {
    AutoMemcpy(of_blob->mut_device_ctx(), mut_blob, &of_blob->blob());
    auto* event_record_provider =
        CHECK_NOTNULL(dynamic_cast<EventRecordProvider*>(of_blob->mut_device_ctx()));
    end_event_record->Init(event_record_provider->MakeEventRecord());
  }
}

void CriticalSectionEndPhyInstrOperand::ForEachMutMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  DoEach(nullptr, CHECK_JUST(CriticalSectionLocalDepObject())->mut_mirrored_object());
}

}  // namespace vm
}  // namespace oneflow
