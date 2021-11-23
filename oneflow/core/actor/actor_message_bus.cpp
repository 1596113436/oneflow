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
#include "oneflow/core/actor/actor_message_bus.h"
#include "oneflow/core/control/global_process_ctx.h"
#include "oneflow/core/job/id_manager.h"
#include "oneflow/core/thread/thread_manager.h"
#include "oneflow/core/comm_network/comm_network.h"

namespace oneflow {

void ActorMsgBus::SendMsg(const ActorMsg& msg) {
  int64_t dst_machine_id = MachineId4ActorId(msg.dst_actor_id());
  if (dst_machine_id == GlobalProcessCtx::Rank()) {
    SendMsgWithoutCommNet(msg);
  } else {
    if (msg.IsDataRegstMsgToConsumer()) {
      int64_t comm_net_sequence;
      {
        std::unique_lock<std::mutex> lock(
            regst_desc_id_dst_actor_id2comm_net_sequence_number_mutex_);
        int64_t& comm_net_sequence_ref =
            regst_desc_id_dst_actor_id2comm_net_sequence_number_[std::make_pair(
                msg.regst_desc_id(), msg.dst_actor_id())];
        comm_net_sequence = comm_net_sequence_ref;
        comm_net_sequence_ref += 1;
      }
      ActorMsg new_msg = msg;
      new_msg.set_comm_net_sequence_number(comm_net_sequence);
      size_t token_size = 0;
      char* serial_data =
          Global<CommNet>::Get()->SerialTokenToData(new_msg.regst()->comm_net_token(), &token_size);
      new_msg.AddUserData(token_size, serial_data);
      free(serial_data);
      size_t msg_size = sizeof(new_msg);
      void* addr = reinterpret_cast<void*>(&new_msg);
      Global<CommNet>::Get()->SendMsg(dst_machine_id, addr, msg_size);
    } else {
      size_t msg_size = sizeof(msg);
      char* data_addr = const_cast<char*>(reinterpret_cast<const char*>(&msg));
      void* addr = reinterpret_cast<void*>(data_addr);
      Global<CommNet>::Get()->SendMsg(dst_machine_id, addr, msg_size);
    }
  }
}

void ActorMsgBus::SendMsgWithoutCommNet(const ActorMsg& msg) {
  CHECK_EQ(MachineId4ActorId(msg.dst_actor_id()), GlobalProcessCtx::Rank());
  int64_t thrd_id = ThrdId4ActorId(msg.dst_actor_id());
  Global<ThreadMgr>::Get()->GetThrd(thrd_id)->EnqueueActorMsg(msg);
}

void ActorMsgBus::HandleRecvData(void* data, size_t size) {
  ActorMsg msg = *(reinterpret_cast<ActorMsg*>(data));
  size_t token_size = 0;
  if (msg.IsDataRegstMsgToConsumer()) {
    void* token = Global<CommNet>::Get()->DeSerialDataToToken((char*)msg.user_data(), &token_size);
    msg.set_comm_net_token(token);
  }

  SendMsgWithoutCommNet(msg);
}

}  // namespace oneflow
