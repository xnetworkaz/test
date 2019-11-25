/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef RTC_BASE_REF_COUNTER_H_
#define RTC_BASE_REF_COUNTER_H_

#include <atomic>

#include "rtc_base/ref_count.h"

namespace webrtc {
namespace webrtc_impl {

class RefCounter {
 public:
  explicit RefCounter(int ref_count) : ref_count_(ref_count) {}
  RefCounter() = delete;

  void IncRef() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  // Returns kDroppedLastRef if this call dropped the last reference; the caller
  // should therefore free the resource protected by the reference counter.
  // Otherwise, returns kOtherRefsRemained (note that in case of multithreading,
  // some other caller may have dropped the last reference by the time this call
  // returns; all we know is that we didn't do it).
  rtc::RefCountReleaseStatus DecRef() {
    return ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0
               ? rtc::RefCountReleaseStatus::kDroppedLastRef
               : rtc::RefCountReleaseStatus::kOtherRefsRemained;
  }

  // Return whether the reference count is one. If the reference count is used
  // in the conventional way, a reference count of 1 implies that the current
  // thread owns the reference and no other thread shares it. This call performs
  // the test for a reference count of one, and performs the memory barrier
  // needed for the owning thread to act on the resource protected by the
  // reference counter, knowing that it has exclusive access.
  bool HasOneRef() const {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }

 private:
  std::atomic<int> ref_count_;
};

}  // namespace webrtc_impl
}  // namespace webrtc

#endif  // RTC_BASE_REF_COUNTER_H_
