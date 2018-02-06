/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/level_estimator.h"

#include <algorithm>
#include <cmath>

#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace agc2 {

LevelEstimator::LevelEstimator(size_t sample_rate_hz,
                               ApmDataDumper* apm_data_dumper)
    : attack_filter_constant_(kAttackFilterConstant),
      decay_filter_constant_(kDecayFilterConstant),
      samples_in_frame_(rtc::CheckedDivExact(sample_rate_hz * kFrameDurationMs,
                                             static_cast<size_t>(1000))),
      samples_in_sub_frame_(
          rtc::CheckedDivExact(samples_in_frame_, kSubFramesInFrame)),
      apm_data_dumper_(apm_data_dumper) {
  CheckParameterCombination();
  RTC_DCHECK(apm_data_dumper_);
  apm_data_dumper_->DumpRaw("agc2_level_estimator_samplerate", sample_rate_hz);
}

void LevelEstimator::CheckParameterCombination() {
  RTC_CHECK_LE(kSubFramesInFrame, samples_in_frame_);
  RTC_CHECK_EQ(samples_in_frame_ % kSubFramesInFrame, 0);
  RTC_CHECK_GT(samples_in_sub_frame_, 1);
}

std::array<float, kSubFramesInFrame> LevelEstimator::ComputeLevel(
    const FloatAudioFrame& float_frame) {
  RTC_DCHECK_GT(float_frame.num_channels(), 0);
  RTC_DCHECK_EQ(float_frame.samples_per_channel(), samples_in_frame_);

  // Compute max envelope without smoothing.
  std::array<float, kSubFramesInFrame> envelope{};
  for (size_t channel_idx = 0; channel_idx < float_frame.num_channels();
       ++channel_idx) {
    RTC_DCHECK_EQ(float_frame.channel(channel_idx).size(), samples_in_frame_);
    const auto channel = float_frame.channel(channel_idx);
    for (size_t sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
      for (size_t sample_in_sub_frame = 0;
           sample_in_sub_frame < samples_in_sub_frame_; ++sample_in_sub_frame) {
        envelope[sub_frame] =
            std::max(envelope[sub_frame],
                     std::abs(channel[sub_frame * samples_in_sub_frame_ +
                                      sample_in_sub_frame]));
      }
    }
  }

  // Make sure envelope increases happen one step earlier so that the
  // corresponding *gain decrease* doesn't miss a sudden signal
  // increase due to interpolation.
  for (size_t sub_frame = 0; sub_frame < kSubFramesInFrame - 1; ++sub_frame) {
    if (envelope[sub_frame] < envelope[sub_frame + 1]) {
      envelope[sub_frame] = envelope[sub_frame + 1];
    }
  }

  // Add attack / decay smoothing.
  for (size_t sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
    const float envelope_value = envelope[sub_frame];
    if (envelope_value > filter_state_level_) {
      envelope[sub_frame] = envelope_value * (1 - attack_filter_constant_) +
                            filter_state_level_ * attack_filter_constant_;
    } else {
      envelope[sub_frame] = envelope_value * (1 - decay_filter_constant_) +
                            filter_state_level_ * decay_filter_constant_;
    }
    filter_state_level_ = envelope[sub_frame];

    // Dump data for debug.
    RTC_DCHECK(apm_data_dumper_);
    const auto channel = float_frame.channel(0);
    apm_data_dumper_->DumpRaw("agc2_level_estimator_samples",
                              samples_in_sub_frame_,
                              &channel[sub_frame * samples_in_sub_frame_]);
    apm_data_dumper_->DumpRaw("agc2_level_estimator_level",
                              envelope[sub_frame]);
  }

  return envelope;
}

void LevelEstimator::SetSampleRate(size_t sample_rate_hz) {
  samples_in_frame_ = rtc::CheckedDivExact(sample_rate_hz * kFrameDurationMs,
                                           static_cast<size_t>(1000));
  samples_in_sub_frame_ =
      rtc::CheckedDivExact(samples_in_frame_, kSubFramesInFrame);
  CheckParameterCombination();
}

void LevelEstimator::SetAttackMs(float duration_ms) {
  const float subframe_duration = static_cast<float>(kFrameDurationMs) /
                                  static_cast<float>(kSubFramesInFrame);
  attack_filter_constant_ =
      std::pow(10, (-1.0 / 20.0 * subframe_duration / duration_ms));
}

void LevelEstimator::SetDecayMs(float duration_ms) {
  const float subframe_duration = static_cast<float>(kFrameDurationMs) /
                                  static_cast<float>(kSubFramesInFrame);
  decay_filter_constant_ =
      std::pow(10, (-1.0 / 20.0 * subframe_duration / duration_ms));
}
}  // namespace agc2
}  // namespace webrtc
