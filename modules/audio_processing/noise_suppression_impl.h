/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_NOISE_SUPPRESSION_IMPL_H_
#define MODULES_AUDIO_PROCESSING_NOISE_SUPPRESSION_IMPL_H_

#include <memory>
#include <vector>

#include "rtc_base/constructor_magic.h"

namespace webrtc {

class AudioBuffer;

// The noise suppression (NS) component attempts to remove noise while
// retaining speech. Recommended to be enabled on the client-side.
class NoiseSuppressionImpl {
 public:
  // Determines the aggressiveness of the suppression. Increasing the level
  // will reduce the noise level at the expense of a higher speech distortion.
  enum Level { kLow, kModerate, kHigh, kVeryHigh };

  explicit NoiseSuppressionImpl(size_t channels, int sample_rate_hz);
  ~NoiseSuppressionImpl();

  void AnalyzeCaptureAudio(AudioBuffer* audio);
  void ProcessCaptureAudio(AudioBuffer* audio);

  int set_level(Level level);
  Level level() const;

  // LEGACY: Returns the internally computed prior speech probability of current
  // frame averaged over output channels. This is not supported in fixed point,
  // for which |kUnsupportedFunctionError| is returned.
  float speech_probability() const;

  // LEGACY: Returns the size of the noise vector returned by NoiseEstimate().
  static size_t num_noise_bins();

  // LEGACY: Returns the noise estimate per frequency bin averaged over all
  // channels.
  std::vector<float> NoiseEstimate();

 private:
  class Suppressor;

  Level level_ = kModerate;
  size_t channels_;
  int sample_rate_hz_;
  std::vector<std::unique_ptr<Suppressor>> suppressors_;
  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(NoiseSuppressionImpl);
};
}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_NOISE_SUPPRESSION_IMPL_H_
