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
#ifndef ONEFLOW_CORE_COMMON_BUFFER_H_
#define ONEFLOW_CORE_COMMON_BUFFER_H_

#include "oneflow/core/common/util.h"

namespace oneflow {

#define BUFFER_PULL_IMPL(item)                                            \
  std::unique_lock<std::mutex> lock(mutex_);                              \
  cond_.wait(lock, [this]() { return (!queue_.empty()) || is_closed_; }); \
  if (queue_.empty()) { return kBufferStatusErrorClosed; }                \
  front_assign(item);                                                     \
  queue_.pop();                                                           \
  if (queue_.size() < max_len_) { cond_.notify_all(); }                   \
  return kBufferStatusSuccess

#define BUFFER_TRY_RECEIVE_IMPL(item)                                                        \
  std::unique_lock<std::mutex> lock(mutex_);                                                 \
  if (queue_.empty()) { return is_closed_ ? kBufferStatusErrorClosed : kBufferStatusEmpty; } \
  front_assign(item);                                                                        \
  queue_.pop();                                                                              \
  if (queue_.size() < max_len_) { cond_.notify_all(); }                                      \
  return kBufferStatusSuccess

enum BufferStatus { kBufferStatusSuccess = 0, kBufferStatusErrorClosed, kBufferStatusEmpty };

template<typename T>
class Buffer final {
 public:
  OF_DISALLOW_COPY_AND_MOVE(Buffer);
  Buffer(size_t max_len) : max_len_(max_len), is_closed_(false) {}
  ~Buffer() = default;

  template<typename U>
  BufferStatus Push(U item) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]() { return queue_.size() < max_len_ || is_closed_; });
    if (is_closed_) { return kBufferStatusErrorClosed; }
    queue_.push(std::forward<U>(item));
    cond_.notify_one();
    return kBufferStatusSuccess;
  }

  BufferStatus Pull(T& item) { BUFFER_PULL_IMPL(item); }
  BufferStatus Pull(T* item) { BUFFER_PULL_IMPL(item); }

  BufferStatus TryReceive(T& item) { BUFFER_TRY_RECEIVE_IMPL(item); }
  BufferStatus TryReceive(T* item) { BUFFER_TRY_RECEIVE_IMPL(item); }

  void Close() {
    std::unique_lock<std::mutex> lock(mutex_);
    is_closed_ = true;
    cond_.notify_all();
  }

 private:
  void front_assign(T* item) { *item = queue_.front(); }
  void front_assign(T& item) { item = std::move(queue_.front()); }

  std::queue<T> queue_;
  mutable std::mutex mutex_;
  size_t max_len_;
  bool is_closed_;
  std::condition_variable cond_;
};

#undef BUFFER_PULL_IMPL
#undef BUFFER_TRY_RECEIVE_IMPL

}  // namespace oneflow

#endif  // ONEFLOW_CORE_COMMON_BUFFER_H_
