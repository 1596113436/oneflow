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
#include "oneflow/core/common/global.h"
#include "oneflow/core/common/protobuf.h"
#include "oneflow/core/common/container_util.h"
#include "oneflow/core/common/str_util.h"
#include "oneflow/core/rpc/include/global_process_ctx.h"

namespace oneflow {

void GlobalProcessCtx::GetCurrentMachineIdAndDeviceId(int64_t* machine_id, int64_t* device_id) {
  *machine_id = Rank();
  *device_id = LocalRank();
}

int64_t GlobalProcessCtx::Rank() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return Global<ProcessCtx>::Get()->rank();
}

int64_t GlobalProcessCtx::LocalRank() {
  char* local_rank_env = std::getenv("LOCAL_RANK");
  if (!local_rank_env) { return 0; }
  CHECK(IsStrInt(local_rank_env));
  static int64_t local_rank = std::stol(local_rank_env);
  return local_rank;
}

int64_t GlobalProcessCtx::NodeSize() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return Global<ProcessCtx>::Get()->node_size();
}

int64_t GlobalProcessCtx::ThisNodeId() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return NodeId4Rank(Rank());
}

int64_t GlobalProcessCtx::NodeId4Rank(int64_t rank) {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  const auto& rank2node_id = Global<ProcessCtx>::Get()->rank_info_in_cluster().rank2node_id();
  return CHECK_JUST(MapAt(rank2node_id, rank));
}

HashMap<int64_t, int64_t> GlobalProcessCtx::NodeId2RankOffset() {
  if (Global<RankInfoInCluster>::Get() != nullptr) {
    return PbMap2HashMap(Global<RankInfoInCluster>::Get()->node_id2rankoffset());
  }
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return PbMap2HashMap(Global<ProcessCtx>::Get()->rank_info_in_cluster().node_id2rankoffset());
}

int64_t GlobalProcessCtx::NumOfProcessOnNode() {
  if (Global<RankInfoInCluster>::Get() != nullptr) {
    return Global<RankInfoInCluster>::Get()->num_process_distribution().num_process(0);
  }
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  int64_t node_id = ThisNodeId();
  return Global<ProcessCtx>::Get()->rank_info_in_cluster().num_process_distribution().num_process(
      node_id);
}

const NumProcessDistribution& GlobalProcessCtx::NumProcessDistributionInCluster() {
  if (Global<RankInfoInCluster>::Get() != nullptr) {
    return Global<RankInfoInCluster>::Get()->num_process_distribution();
  }
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return Global<ProcessCtx>::Get()->rank_info_in_cluster().num_process_distribution();
}

bool GlobalProcessCtx::IsThisProcessMaster() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return Global<ProcessCtx>::Get()->rank() == 0;
}

size_t GlobalProcessCtx::WorldSize() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  return Global<ProcessCtx>::Get()->ctrl_addr().size();
}

std::string GlobalProcessCtx::LogDirEntry() {
  CHECK_NOTNULL(Global<ProcessCtx>::Get());
  const auto& process_ctx = *Global<ProcessCtx>::Get();
  const auto& addr = process_ctx.ctrl_addr(process_ctx.rank());
  CHECK(addr.has_host());
  return addr.host() + "-" + std::to_string(addr.port()) + "-" + std::to_string(process_ctx.rank());
}

/* static */ int64_t GlobalProcessCtx::LocalRank(int64_t rank) {
  return rank % NumOfProcessPerNode();
}

}  // namespace oneflow
