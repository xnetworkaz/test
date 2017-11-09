/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/test/conversational_speech/mock_wavreader_factory.h"

#include "modules/audio_processing/test/conversational_speech/mock_wavreader.h"
#include "rtc_base/logging.h"
#include "rtc_base/pathutils.h"
#include "test/gmock.h"

namespace webrtc {
namespace test {
namespace conversational_speech {

using testing::_;
using testing::Invoke;

MockWavReaderFactory::MockWavReaderFactory(
    const Params& default_params,
    const std::map<std::string, const Params>& params)
    : default_params_(default_params), audiotrack_names_params_(params) {
  ON_CALL(*this, Create(_))
      .WillByDefault(Invoke(this, &MockWavReaderFactory::CreateMock));
}

MockWavReaderFactory::MockWavReaderFactory(const Params& default_params)
    : MockWavReaderFactory(default_params,
                           std::map<std::string, const Params>{}) {}

MockWavReaderFactory::~MockWavReaderFactory() = default;

std::unique_ptr<WavReaderInterface> MockWavReaderFactory::CreateMock(
    const std::string& filepath) {
  // Search the parameters corresponding to filepath.
  const rtc::Pathname audiotrack_file_path(filepath);
  const auto it =
      audiotrack_names_params_.find(audiotrack_file_path.filename());

  // If not found, use default parameters.
  if (it == audiotrack_names_params_.end()) {
    LOG(LS_VERBOSE) << "using default parameters for " << filepath;
    return std::unique_ptr<WavReaderInterface>(new MockWavReader(
        default_params_.sample_rate, default_params_.num_channels,
        default_params_.num_samples));
  }

  // Found, use the audiotrack-specific parameters.
  LOG(LS_VERBOSE) << "using ad-hoc parameters for " << filepath;
  LOG(LS_VERBOSE) << "sample_rate " << it->second.sample_rate;
  LOG(LS_VERBOSE) << "num_channels " << it->second.num_channels;
  LOG(LS_VERBOSE) << "num_samples " << it->second.num_samples;
  return std::unique_ptr<WavReaderInterface>(new MockWavReader(
      it->second.sample_rate, it->second.num_channels, it->second.num_samples));
}

}  // namespace conversational_speech
}  // namespace test
}  // namespace webrtc
