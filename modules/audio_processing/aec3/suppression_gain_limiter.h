/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AEC3_SUPPRESSION_GAIN_LIMITER_H_
#define MODULES_AUDIO_PROCESSING_AEC3_SUPPRESSION_GAIN_LIMITER_H_

#include "api/array_view.h"
#include "api/audio/echo_canceller3_config.h"
#include "rtc_base/constructormagic.h"

namespace webrtc {

class SuppressionGainUpperLimiter {
 public:
  explicit SuppressionGainUpperLimiter(const EchoCanceller3Config& config);
  void Reset();
  void Update(bool render_activity);
  float Limit() const { return suppressor_gain_limit_; }

 private:
  const EchoCanceller3Config::EchoRemovalControl::GainRampup rampup_config_;
  const float gain_rampup_increase_;
  bool call_startup_phase_ = true;
  int realignment_counter_ = 0;
  bool active_render_seen_ = false;
  float suppressor_gain_limit_ = 1.f;
  bool recent_reset_ = false;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(SuppressionGainUpperLimiter);
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AEC3_SUPPRESSION_GAIN_LIMITER_H_
