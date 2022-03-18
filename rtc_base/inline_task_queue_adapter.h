/*
 *  Copyright 2022 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef RTC_BASE_INLINE_TASK_QUEUE_ADAPTER_H_
#define RTC_BASE_INLINE_TASK_QUEUE_ADAPTER_H_

#include <atomic>
#include <memory>
#include <utility>

#include "api/scoped_refptr.h"
#include "api/task_queue/queued_task.h"
#include "api/task_queue/task_queue_base.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_utils/to_queued_task.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class RTC_LOCKABLE RTC_EXPORT InlineTaskQueueAdapter : public TaskQueueBase {
 public:
  explicit InlineTaskQueueAdapter(
      std::unique_ptr<TaskQueueBase, TaskQueueDeleter> base_task_queue);

  template <class Closure>
  void PostTask(Closure&& closure) {
    TaskQueueBase* current;
    if (shared_->TryBeginExecutionInline(this, &current)) {
      std::forward<Closure>(closure)();
      shared_->EndInlineExecution(current);
      return;
    }
    base_task_queue_->PostTask(std::make_unique<WrapperTask>(
        ToQueuedTask(std::forward<Closure>(closure)), shared_, this,
        /*take_queue_slot=*/false));
  }

  template <class Safety, class Closure>
  void PostTask(Safety&& safety, Closure&& closure) {
    TaskQueueBase* current;
    if (shared_->TryBeginExecutionInline(this, &current)) {
      std::forward<Closure>(closure)();
      shared_->EndInlineExecution(current);
      return;
    }

    base_task_queue_->PostTask(std::make_unique<WrapperTask>(
        ToQueuedTask(std::forward(safety), std::forward<Closure>(closure)),
        shared_, this, /*take_queue_slot=*/false));
  }

  // TaskQueueBase
  void Delete() override;
  void PostTask(std::unique_ptr<QueuedTask> task) override;
  void PostDelayedTask(std::unique_ptr<QueuedTask> task,
                       uint32_t millis) override;
  void PostDelayedHighPrecisionTask(std::unique_ptr<QueuedTask> task,
                                    uint32_t milliseconds) override;

 private:
  class RTC_LOCKABLE SharedState {
   public:
    bool TryBeginExecutionInline(TaskQueueBase* queue,
                                 TaskQueueBase** original_current_queue)
        RTC_EXCLUSIVE_TRYLOCK_FUNCTION(true);
    void EndInlineExecution(TaskQueueBase* original_current_queue)
        RTC_UNLOCK_FUNCTION();
    void BeginExecution(bool take_queue_slot) RTC_EXCLUSIVE_LOCK_FUNCTION();
    void EndExecution() RTC_UNLOCK_FUNCTION();

   private:
    std::atomic<int> queue_size{0};
    Mutex mu;
  };

  class WrapperTask : public webrtc::QueuedTask {
   public:
    WrapperTask(
        std::unique_ptr<QueuedTask> task,
        rtc::scoped_refptr<rtc::FinalRefCountedObject<SharedState>> shared,
        TaskQueueBase* queue,
        bool take_queue_slot);
    bool Run() override;

   protected:
    std::unique_ptr<QueuedTask> task_;
    const rtc::scoped_refptr<rtc::FinalRefCountedObject<SharedState>> shared_;
    TaskQueueBase* const queue_;
    const bool take_queue_slot_;
  };

  const std::unique_ptr<TaskQueueBase, TaskQueueDeleter> base_task_queue_;
  rtc::scoped_refptr<rtc::FinalRefCountedObject<SharedState>> shared_ =
      rtc::make_ref_counted<SharedState>();
};

}  // namespace webrtc

#endif  // RTC_BASE_INLINE_TASK_QUEUE_ADAPTER_H_
