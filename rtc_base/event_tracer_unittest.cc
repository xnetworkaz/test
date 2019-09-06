/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/event_tracer.h"

#include "rtc_base/critical_section.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/trace_event.h"
#include "test/gtest.h"

namespace {

class TestStatistics {
 public:
  void Reset() {
    rtc::CritScope cs(&crit_);
    events_logged_ = 0;
  }

  void Increment() {
    rtc::CritScope cs(&crit_);
    ++events_logged_;
  }

  int Count() const {
    rtc::CritScope cs(&crit_);
    return events_logged_;
  }

  static TestStatistics* Get() {
    // google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
    static auto& test_stats = *new TestStatistics();
    return &test_stats;
  }

 private:
  rtc::CriticalSection crit_;
  int events_logged_ RTC_GUARDED_BY(crit_) = 0;
};

const unsigned char* GetCategoryEnabledHandler(const char* /*name*/) {
  return reinterpret_cast<const unsigned char*>("test");
}

void TraceEventHandler(char /*phase*/,
                       const unsigned char* /*category_enabled*/,
                       const char* /*name*/,
                       unsigned long long /*id*/,
                       int /*num_args*/,
                       const char** /*arg_names*/,
                       const unsigned char* /*arg_types*/,
                       const unsigned long long* /*arg_values*/,
                       unsigned char /*flags*/) {
  TestStatistics::Get()->Increment();
}

}  // namespace

namespace webrtc {

TEST(EventTracerTest, EventTracerDisabled) {
  { TRACE_EVENT0("test", "EventTracerDisabled"); }
  EXPECT_FALSE(TestStatistics::Get()->Count());
  TestStatistics::Get()->Reset();
}

TEST(EventTracerTest, ScopedTraceEvent) {
  SetupEventTracer(&GetCategoryEnabledHandler, &TraceEventHandler);
  { TRACE_EVENT0("test", "ScopedTraceEvent"); }
  EXPECT_EQ(2, TestStatistics::Get()->Count());
  TestStatistics::Get()->Reset();
}

}  // namespace webrtc
