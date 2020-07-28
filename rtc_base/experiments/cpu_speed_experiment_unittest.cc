/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/cpu_speed_experiment.h"

#include "rtc_base/gunit.h"
#include "test/field_trial.h"
#include "test/gmock.h"

namespace webrtc {

TEST(CpuSpeedExperimentTest, NoValueIfNotEnabled) {
  CpuSpeedExperiment cpu_speed_config;
  EXPECT_FALSE(cpu_speed_config.GetValue(1));
}

TEST(CpuSpeedExperimentTest, GetValue) {
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000,cpu_speed:-12/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_EQ(-12, cpu_speed_config.GetValue(1));
  EXPECT_EQ(-12, cpu_speed_config.GetValue(1000));
  EXPECT_EQ(-16, cpu_speed_config.GetValue(1001));
}

TEST(CpuSpeedExperimentTest, GetValueWithList) {
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000|2000|3000,cpu_speed:-1|-10|-16/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_EQ(-1, cpu_speed_config.GetValue(1));
  EXPECT_EQ(-1, cpu_speed_config.GetValue(1000));
  EXPECT_EQ(-10, cpu_speed_config.GetValue(1001));
  EXPECT_EQ(-10, cpu_speed_config.GetValue(2000));
  EXPECT_EQ(-16, cpu_speed_config.GetValue(2001));
  EXPECT_EQ(-16, cpu_speed_config.GetValue(3000));
  EXPECT_EQ(-16, cpu_speed_config.GetValue(3001));
}

TEST(CpuSpeedExperimentTest, GetValueFailsForTooSmallValue) {
  // Supported range: [-16, -1].
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000|2000|3000,cpu_speed:-1|-10|-17/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_FALSE(cpu_speed_config.GetValue(1));
}

TEST(CpuSpeedExperimentTest, GetValueFailsForTooLargeValue) {
  // Supported range: [-16, -1].
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000|2000|3000,cpu_speed:0|-10|-16/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_FALSE(cpu_speed_config.GetValue(1));
}

TEST(CpuSpeedExperimentTest, GetValueFailsIfPixelsDecreases) {
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000|999|3000,cpu_speed:-5|-10|-16/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_FALSE(cpu_speed_config.GetValue(1));
}

TEST(CpuSpeedExperimentTest, GetValueFailsIfCpuSpeedIncreases) {
  webrtc::test::ScopedFieldTrials field_trials(
      "WebRTC-VP8-CpuSpeed-Arm/pixels:1000|2000|3000,cpu_speed:-5|-4|-16/");

  CpuSpeedExperiment cpu_speed_config;
  EXPECT_FALSE(cpu_speed_config.GetValue(1));
}

}  // namespace webrtc
