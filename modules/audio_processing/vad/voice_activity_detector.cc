/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/vad/voice_activity_detector.h"

#include <algorithm>

#include <math.h>

#include "rtc_base/checks.h"

#include "common_audio/include/audio_util.h"

namespace webrtc {
namespace {

const size_t kNumChannels = 1;

const double kDefaultVoiceValue = 1.0;
const double kNeutralProbability = 0.5;
const double kLowProbability = 0.01;

}  // namespace

VoiceActivityDetector::VoiceActivityDetector()
    : vad_and_level_(),
      last_voice_probability_(kDefaultVoiceValue),
      standalone_vad_(StandaloneVad::Create()) {}

VoiceActivityDetector::~VoiceActivityDetector() = default;

// Because ISAC has a different chunk length, it updates
// |chunkwise_voice_probabilities_| and |chunkwise_rms_| when there is new data.
// Otherwise it clears them.
void VoiceActivityDetector::ProcessChunk(const int16_t* audio,
                                         size_t length,
                                         int sample_rate_hz) {
  RTC_DCHECK_EQ(length, sample_rate_hz / 100);
  // Resample to the required rate.
  const int16_t* resampled_ptr = audio;
  if (sample_rate_hz != kSampleRateHz) {
    RTC_CHECK_EQ(
        resampler_.ResetIfNeeded(sample_rate_hz, kSampleRateHz, kNumChannels),
        0);
    resampler_.Push(audio, length, resampled_, kLength10Ms, length);
    resampled_ptr = resampled_;
  }
  RTC_DCHECK_EQ(length, kLength10Ms);

  // Each chunk needs to be passed into |standalone_vad_|, because internally it
  // buffers the audio and processes it all at once when GetActivity() is
  // called.
  RTC_CHECK_EQ(standalone_vad_->AddAudio(resampled_ptr, length), 0);

  audio_processing_.ExtractFeatures(resampled_ptr, length, &features_);

  chunkwise_voice_probabilities_.resize(features_.num_frames);
  chunkwise_rms_.resize(features_.num_frames);
  std::copy(features_.rms, features_.rms + chunkwise_rms_.size(),
            chunkwise_rms_.begin());
  if (features_.num_frames > 0) {
    if (features_.silence) {
      // The other features are invalid, so set the voice probabilities to an
      // arbitrary low value.
      std::fill(chunkwise_voice_probabilities_.begin(),
                chunkwise_voice_probabilities_.end(), kLowProbability);
    } else {
      std::fill(chunkwise_voice_probabilities_.begin(),
                chunkwise_voice_probabilities_.end(), kNeutralProbability);
      RTC_CHECK_GE(
          standalone_vad_->GetActivity(&chunkwise_voice_probabilities_[0],
                                       chunkwise_voice_probabilities_.size()),
          0);
      RTC_CHECK_GE(pitch_based_vad_.VoicingProbability(
                       features_, &chunkwise_voice_probabilities_[0]),
                   0);
    }
    last_voice_probability_ = chunkwise_voice_probabilities_.back();
  }
  for (size_t i = 0; i < features_.num_frames; ++i) {
    vad_and_level_[i] = LevelAndProbability(
        chunkwise_voice_probabilities_[i], FloatS16ToDbfs(chunkwise_rms_[i]),
        FloatS16ToDbfs(chunkwise_rms_[i]));  // TODO(aleloi): fix last argument.
  }
}

rtc::ArrayView<const VadWithLevel::LevelAndProbability>
VoiceActivityDetector::AnalyzeFrame(AudioFrameView<const float> frame) {
  // First attempt: we only feed the first channel to the VAD. The VAD
  // takes int16 values. We convert the first channel to int16.
  std::array<int16_t, 480> first_channel_as_int;
  RTC_DCHECK_LE(frame.samples_per_channel(), 480);
  std::transform(frame.channel(0).begin(),
                 frame.channel(0).begin() + frame.samples_per_channel(),
                 first_channel_as_int.begin(), [](float f) {
                   RTC_DCHECK_LE(std::abs(static_cast<int16_t>(f)),
                                 std::numeric_limits<int16_t>::max());
                   return static_cast<int16_t>(
                       f);  // * std::numeric_limits<int16_t>::max();
                 });

  ProcessChunk(first_channel_as_int.begin(), frame.samples_per_channel(),
               frame.samples_per_channel() * 100);

  // const std::vector<double>& probabilities =
  //     vad_.chunkwise_voice_probabilities();

  // const std::vector<double>& rms_values = vad_.chunkwise_rms();

  return rtc::ArrayView<const VadWithLevel::LevelAndProbability>(
      &vad_and_level_[0], chunkwise_rms_.size());

  // std::vector<LevelAndProbability> result;
  // for (size_t i = 0; i < probabilities.size(); ++i) {
  //   result.emplace_back(probabilities[i], FloatS16ToDbfs(rms_values[i]),
  //   -90.f);
  // }

  // return result;
}

}  // namespace webrtc
