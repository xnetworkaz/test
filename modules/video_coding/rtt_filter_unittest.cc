/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/rtt_filter.h"

#include "api/units/time_delta.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

TEST(RttFilterTest, RttIsCapped) {
  VCMRttFilter rtt_filter;
  rtt_filter.Update(TimeDelta::Seconds(500));

  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Seconds(3));
}

// If the difference between samples is more than away from the mean
// then this is considered a jump. After 5 jumps are detected, a short filter is
// applied.
TEST(RttFilterTest, PositiveJumpDetection) {
  VCMRttFilter rtt_filter;

  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));

  // Trigger 5 jumps.
  rtt_filter.Update(TimeDelta::Millis(1400));
  rtt_filter.Update(TimeDelta::Millis(1500));
  rtt_filter.Update(TimeDelta::Millis(1600));
  rtt_filter.Update(TimeDelta::Millis(1600));

  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(1600));

  rtt_filter.Update(TimeDelta::Millis(1600));
  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(1600));
}

TEST(RttFilterTest, NegativeJumpDetection) {
  VCMRttFilter rtt_filter;

  for (int i = 0; i < 10; ++i)
    rtt_filter.Update(TimeDelta::Millis(1500));

  // Trigger 5 negative jumps.
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  // Before 5 jumps, max RTT is still 1500.
  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(1500));

  rtt_filter.Update(TimeDelta::Millis(300));
  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(300));
}

TEST(RttFilterTest, JumpsResetByDirectionShift) {
  VCMRttFilter rtt_filter;
  for (int i = 0; i < 10; ++i)
    rtt_filter.Update(TimeDelta::Millis(1500));

  // Trigger 5 negative jumps.
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(200));
  rtt_filter.Update(TimeDelta::Millis(2000));
  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(2000));

  rtt_filter.Update(TimeDelta::Millis(300));
  EXPECT_EQ(rtt_filter.Rtt(), TimeDelta::Millis(2000));
}

// If the difference between the max and average is more than 3.5 stddevs away
// then a drift is detected, and a short filter is applied to find a new max
// rtt.
TEST(RttFilterTest, DriftDetection) {
  VCMRttFilter rtt_filter;

  // Descend RTT by 30ms and settle at 700ms RTT. A drift is detected after rtt
  // of 700ms is reported around 50 times for these targets.
  constexpr TimeDelta kStartRtt = TimeDelta::Millis(1000);
  constexpr TimeDelta kDriftTarget = TimeDelta::Millis(700);
  constexpr TimeDelta kDelta = TimeDelta::Millis(30);
  for (TimeDelta rtt = kStartRtt; rtt >= kDriftTarget; rtt -= kDelta)
    rtt_filter.Update(rtt);

  EXPECT_EQ(rtt_filter.Rtt(), kStartRtt);

  for (int i = 0; i < 50; ++i)
    rtt_filter.Update(kDriftTarget);
  EXPECT_EQ(rtt_filter.Rtt(), kDriftTarget);
}

}  // namespace webrtc
