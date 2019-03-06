/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_SYNCHRONIZATION_WATCHDOG_TIMER_H_
#define RTC_BASE_SYNCHRONIZATION_WATCHDOG_TIMER_H_

#include <atomic>

#include "rtc_base/location.h"

namespace webrtc {

// A watchdog timer, useful for discovering when threads are stuck. In your
// thread, do something like this:
//
//   WatchdogTimer wt(RTC_FROM_HERE);
//   while (true) {
//     DoSomething();
//     wt.Poke();
//   }
//
// And in one central location in your program, call WatchdogTimer::CheckAll()
// periodically. If the thread ever fails to call wt.Poke() at least once
// between any two CheckAll() calls, this will be logged as an error.
class WatchdogTimer {
 public:
  // Creates a new watchdog timer and registers it in a global list. (It's
  // created in a poked state, so you don't need to call Poke() immediately.)
  explicit WatchdogTimer(rtc::Location location);

  // Watchdog timers may not be copied or moved; a global list has poiners to
  // them.
  WatchdogTimer(const WatchdogTimer&) = delete;
  WatchdogTimer(WatchdogTimer&&) = delete;
  WatchdogTimer& operator=(const WatchdogTimer&) = delete;
  WatchdogTimer& operator=(WatchdogTimer&&) = delete;

  // Destroys the watchdog timer and removes it from the global list.
  ~WatchdogTimer();

  // Pokes the timer. This needs to be done at least once between one CheckAll()
  // call and the next. This is a very cheap atomic operation, so there's no
  // need to avoid calling it fairly often.
  void Poke() {
    // Relaxed memory order is sufficient here, since we only need to sequence
    // the values of this one variable, and not any other parts of memory.
    needs_poking_.store(false, std::memory_order_relaxed);
  }

  // Checks that all WatchdogTimer instances in the global list have been poked
  // at least once since the last call to CheckAll(). Logs errors for any
  // unpoked timers.
  static void CheckAll();

 private:
  // Have we been poked recently, or do we need poking?
  std::atomic<bool> needs_poking_;

  // Debug info that we log in case CheckAll() discovers that this instance
  // hasn't been poked.
  const rtc::Location created_here_;
};

}  // namespace webrtc

#endif  // RTC_BASE_SYNCHRONIZATION_WATCHDOG_TIMER_H_
