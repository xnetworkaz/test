/*
 *  Copyright 2022 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/inline_task_queue.h"

#include <memory>
#include <utility>

#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {

InlineTaskQueue::InlineTaskQueue(
    std::unique_ptr<TaskQueueBase, TaskQueueDeleter> base_task_queue)
    : base_task_queue_(std::move(base_task_queue)) {}

void InlineTaskQueue::Delete() {
  delete this;
}

void InlineTaskQueue::PostTask(absl::AnyInvocable<void() &&> task) {
  PostTask<absl::AnyInvocable<void() &&>>(std::move(task));
}

void InlineTaskQueue::PostDelayedTask(absl::AnyInvocable<void() &&> task,
                                      TimeDelta duration) {
  base_task_queue_->PostDelayedTask(
      WrappedDelayedTask(std::move(task), shared_state_, this), duration);
}

void InlineTaskQueue::PostDelayedHighPrecisionTask(
    absl::AnyInvocable<void() &&> task,
    TimeDelta duration) {
  base_task_queue_->PostDelayedHighPrecisionTask(
      WrappedDelayedTask(std::move(task), shared_state_, this), duration);
}

void InlineTaskQueue::PostTaskNoInline(absl::AnyInvocable<void() &&> task) {
  base_task_queue_->PostTask(WrappedImmediateTask(std::move(task),
                                                  shared_state_, this,
                                                  /*queue_slot_taken=*/false));
}

bool InlineTaskQueue::SharedState::TryBeginInlineExecution() {
  if (queue_size_.fetch_add(1) == 0) {
    // Need to take the `task_mu` lock to ensure serial execution with other
    // concurrent deferred or delayed tasks.
    task_mu_.Lock();
    return true;
  }
  return false;
}

void InlineTaskQueue::SharedState::EndInlineExecution() {
  task_mu_.Unlock();
  queue_size_--;
}

InlineTaskQueue::WrappedImmediateTask::WrappedImmediateTask(
    absl::AnyInvocable<void() &&> task,
    rtc::scoped_refptr<rtc::FinalRefCountedObject<SharedState>> shared_state,
    TaskQueueBase* queue,
    bool queue_slot_taken)
    : task_(std::move(task)),
      shared_state_(std::move(shared_state)),
      queue_(queue) {
  if (!queue_slot_taken) {
    shared_state_->queue_size_++;
  }
}

InlineTaskQueue::WrappedImmediateTask::~WrappedImmediateTask() {
  if (shared_state_)
    shared_state_->queue_size_--;
}

void InlineTaskQueue::WrappedImmediateTask::operator()() && {
  CurrentTaskQueueSetter setter(queue_);
  MutexLock lock(&shared_state_->task_mu_);
  std::move(task_)();
}

InlineTaskQueue::WrappedDelayedTask::WrappedDelayedTask(
    absl::AnyInvocable<void() &&> task,
    rtc::scoped_refptr<rtc::FinalRefCountedObject<SharedState>> shared_state,
    TaskQueueBase* queue)
    : task_(std::move(task)),
      shared_state_(std::move(shared_state)),
      queue_(queue) {}

void InlineTaskQueue::WrappedDelayedTask::operator()() && {
  CurrentTaskQueueSetter setter(queue_);
  shared_state_->queue_size_++;
  {
    MutexLock lock(&shared_state_->task_mu_);
    std::move(task_)();
    // To decrease the chance that inline PostTask callers contend `task_mu_`,
    // we unlock the mutex before reducing `queue_size_`.
  }
  shared_state_->queue_size_--;
}

}  // namespace webrtc
