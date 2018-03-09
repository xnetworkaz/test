/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AGC2_SATURATION_PROTECTOR_H_
#define MODULES_AUDIO_PROCESSING_AGC2_SATURATION_PROTECTOR_H_

#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/vad/vad_with_level.h"

// #include "modules/audio_processing/vad/voice_activity_detector.h"

namespace webrtc {

class SaturationProtector {
 public:
  // TODO(aleloi): Possibly set some config.
  SaturationProtector() = default;

  // Update and return margin estimate. This method should be called
  // whenever a frame is reliably classified as 'speech'.
  //
  // Returned value is in DB scale.
  void UpdateMargin(VadWithLevel::LevelAndProbability vad_data,
                    float last_speech_level_estimate_dbfs);

  // Returns latest computed margin. Used in cases when speech is not
  // detected.
  float LastMargin() const;

 private:
  float last_margin_ = kInitialSaturationMarginDb;
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AGC2_SATURATION_PROTECTOR_H_
