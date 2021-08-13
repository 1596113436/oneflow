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

#include "oneflow/core/framework/tensor_pool.h"

namespace oneflow {
namespace one {

DTRTensorPool::DTRTensorPool() {
    start_time_ = std::chrono::steady_clock::now();
}

Maybe<vm::DTREagerBlobObject*> DTRTensorPool::find_best_tensor() {
    double min_cost = -1;
    vm::DTREagerBlobObject* best(nullptr);
    for (auto tensor : candidates_) {
        if (static_cast<bool>(tensor->compute_path()) && !tensor->is_pinned()) {
            double cur_cost = tensor->neighbor_cost();
            if (min_cost < 0 || min_cost > cur_cost) {
                best = tensor;
                min_cost = cur_cost;
            }
        }
    }
    return best;
}

Maybe<void> DTRTensorPool::find_best_tensor_and_evict() {
    auto* best = JUST(find_best_tensor());
    CHECK_NOTNULL_OR_RETURN(best);
    JUST(best->evict());
    return Maybe<void>::Ok();
}

Maybe<void> DTRTensorPool::insert(vm::DTREagerBlobObject* blob_object) {
    CHECK_NOTNULL_OR_RETURN(blob_object);
    candidates_.insert(blob_object);
    return Maybe<void>::Ok();
}

Maybe<void> DTRTensorPool::evict(vm::DTREagerBlobObject* blob_object) {
    CHECK_NOTNULL_OR_RETURN(blob_object);
    candidates_.erase(blob_object);
    return Maybe<void>::Ok();
}

double DTRTensorPool::duration() {
    auto t2 = std::chrono::steady_clock::now();
    // time in seconds
    std::chrono::duration<double> time_span = t2 - start_time_;
    // // time in milli
    // std::chrono::duration<double ,std::milli> time_span = t2 - start_time_;
    return time_span.count();
}

}   // namespace one
}   // namespace oneflow
