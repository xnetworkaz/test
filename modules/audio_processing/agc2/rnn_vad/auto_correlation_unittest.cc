/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/rnn_vad/auto_correlation.h"

#include "modules/audio_processing/agc2/rnn_vad/pitch_search_internal.h"
#include "modules/audio_processing/agc2/rnn_vad/test_utils.h"
#include "test/gtest.h"

namespace webrtc {
namespace rnn_vad {
namespace test {

TEST(RnnVadTest, PitchBufferAutoCorrelationWithinTolerance) {
  PitchTestData test_data;
  std::array<float, kBufSize12kHz> pitch_buf_decimated;
  Decimate2x(test_data.GetPitchBufView(), pitch_buf_decimated);
  std::array<float, kNumPitchBufAutoCorrCoeffs> computed_output;
  {
    // TODO(bugs.webrtc.org/8948): Add when the issue is fixed.
    // FloatingPointExceptionObserver fpe_observer;
    AutoCorrelationCalculator auto_corr_calculator;
    auto_corr_calculator.ComputeOnPitchBuffer(pitch_buf_decimated,
                                              computed_output);
  }
  auto auto_corr_view = test_data.GetPitchBufAutoCorrCoeffsView();
  ExpectNearAbsolute({auto_corr_view.data(), auto_corr_view.size()},
                     computed_output, 3e-3f);
}

// Check that the auto correlation function computes the right thing for a
// simple use case.
TEST(RnnVadTest, CheckAutoCorrelationOnConstantPitchBuffer) {
  // Create constant signal with no pitch.
  std::array<float, kBufSize12kHz> pitch_buf_decimated;
  std::fill(pitch_buf_decimated.begin(), pitch_buf_decimated.end(), 1.f);
  std::array<float, kNumPitchBufAutoCorrCoeffs> computed_output;
  {
    // TODO(bugs.webrtc.org/8948): Add when the issue is fixed.
    // FloatingPointExceptionObserver fpe_observer;
    AutoCorrelationCalculator auto_corr_calculator;
    auto_corr_calculator.ComputeOnPitchBuffer(pitch_buf_decimated,
                                              computed_output);
  }
  // The expected output is constantly the length of the fixed 'x'
  // array in ComputePitchAutoCorrelation.
  std::array<float, kNumPitchBufAutoCorrCoeffs> expected_output;
  std::fill(expected_output.begin(), expected_output.end(),
            kBufSize12kHz - kMaxPitch12kHz);
  ExpectNearAbsolute(expected_output, computed_output, 4e-5f);
}

}  // namespace test
}  // namespace rnn_vad
}  // namespace webrtc
