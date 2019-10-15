/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/aec3/erle_estimator.h"

#include "modules/audio_processing/aec3/aec3_common.h"
#include "rtc_base/checks.h"

namespace webrtc {

ErleEstimator::ErleEstimator(size_t startup_phase_length_blocks_,
                             const EchoCanceller3Config& config,
                             size_t num_capture_channels)
    : startup_phase_length_blocks__(startup_phase_length_blocks_),
      use_signal_dependent_erle_(config.erle.num_sections > 1),
      fullband_erle_estimator_(config.erle, num_capture_channels),
      subband_erle_estimator_(config, num_capture_channels),
      signal_dependent_erle_estimator_(config, num_capture_channels) {
  Reset(true);
}

ErleEstimator::~ErleEstimator() = default;

void ErleEstimator::Reset(bool delay_change) {
  fullband_erle_estimator_.Reset();
  subband_erle_estimator_.Reset();
  signal_dependent_erle_estimator_.Reset();
  if (delay_change) {
    blocks_since_reset_ = 0;
  }
}

void ErleEstimator::Update(
    const RenderBuffer& render_buffer,
    const std::vector<std::array<float, kFftLengthBy2Plus1>>&
        filter_frequency_response,
    rtc::ArrayView<const float, kFftLengthBy2Plus1> reverb_render_spectrum,
    rtc::ArrayView<const float, kFftLengthBy2Plus1> capture_spectrum,
    rtc::ArrayView<const float, kFftLengthBy2Plus1> subtractor_spectrum,
    bool converged_filter,
    bool onset_detection) {
  const auto& X2_reverb = reverb_render_spectrum;
  const auto& Y2 = capture_spectrum;
  const auto& E2 = subtractor_spectrum;

  if (++blocks_since_reset_ < startup_phase_length_blocks__) {
    return;
  }

  subband_erle_estimator_.Update(X2_reverb, Y2, E2, converged_filter,
                                 onset_detection);

  if (use_signal_dependent_erle_) {
    signal_dependent_erle_estimator_.Update(
        render_buffer, filter_frequency_response, X2_reverb, Y2, E2,
        subband_erle_estimator_.Erle(), converged_filter);
  }

  fullband_erle_estimator_.Update(X2_reverb, Y2, E2, converged_filter);
}

void ErleEstimator::Dump(
    const std::unique_ptr<ApmDataDumper>& data_dumper) const {
  fullband_erle_estimator_.Dump(data_dumper);
  subband_erle_estimator_.Dump(data_dumper);
  signal_dependent_erle_estimator_.Dump(data_dumper);
}

}  // namespace webrtc
