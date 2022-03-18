/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/time_util.h"

#include <algorithm>

#include "rtc_base/checks.h"
#include "rtc_base/numerics/divide_round.h"

namespace webrtc {

uint32_t SaturatedToCompactNtp(TimeDelta delta) {
  constexpr uint32_t kMaxCompactNtp = 0xFFFFFFFF;
  constexpr int kCompactNtpInSecond = 0x10000;
  if (delta <= TimeDelta::Zero())
    return 0;
  if (delta.us() >= kMaxCompactNtp * int64_t{1'000'000} / kCompactNtpInSecond)
    return kMaxCompactNtp;
  // To convert to compact ntp need to divide by 1e6 to get seconds,
  // then multiply by 0x10000 to get the final result.
  // To avoid float operations, multiplication and division swapped.
  return DivideRoundToNearest(delta.us() * kCompactNtpInSecond, 1'000'000);
}

TimeDelta CompactNtpRttToTimeDelta(uint32_t compact_ntp_interval) {
  static constexpr TimeDelta kMinRtt = TimeDelta::Millis(1);
  // Interval to convert expected to be positive, e.g. rtt or delay.
  // Because interval can be derived from non-monotonic ntp clock,
  // it might become negative that is indistinguishable from very large values.
  // Since very large rtt/delay are less likely than non-monotonic ntp clock,
  // those values consider to be negative and convert to minimum value of 1ms.
  if (compact_ntp_interval > 0x80000000)
    return kMinRtt;
  // Convert to 64bit value to avoid multiplication overflow.
  int64_t value = static_cast<int64_t>(compact_ntp_interval);
  // To convert to milliseconds need to divide by 2^16 to get seconds,
  // then multiply by 1000 to get milliseconds. To avoid float operations,
  // multiplication and division swapped.
  int64_t us = DivideRoundToNearest(value * 1'000'000, 1 << 16);
  // small Rtt value considered too good to be true and increased to 1ms.
  return std::max(TimeDelta::Micros(us), kMinRtt);
}
}  // namespace webrtc
