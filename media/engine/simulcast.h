/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_ENGINE_SIMULCAST_H_
#define MEDIA_ENGINE_SIMULCAST_H_

#include <string>
#include <vector>

#include "api/video_codecs/video_encoder_config.h"

namespace cricket {

// TODO(pthatcher): Write unit tests just for these functions,
// independent of WebrtcVideoEngine.

// Gets the total maximum bitrate for the |streams|.
int GetTotalMaxBitrateBps(const std::vector<webrtc::VideoStream>& streams);

// Adds any bitrate of |max_bitrate_bps| that is above the total maximum bitrate
// for the |layers| to the highest quality layer.
void BoostMaxSimulcastLayer(int max_bitrate_bps,
                            std::vector<webrtc::VideoStream>* layers);

// Gets simulcast settings.
// TODO(sprang): Remove default parameter when it's not longer referenced.
// TODO(asapersson): Remove max_bitrate_bps.
std::vector<webrtc::VideoStream> GetSimulcastConfig(
    size_t max_layers,
    int width,
    int height,
    int /*max_bitrate_bps*/,
    double bitrate_priority,
    int max_qp,
    int max_framerate,
    bool is_screenshare = false);

// Gets the simulcast config layers for a non-screensharing case.
std::vector<webrtc::VideoStream> GetNormalSimulcastLayers(
    size_t max_layers,
    int width,
    int height,
    double bitrate_priority,
    int max_qp,
    int max_framerate);

// Gets simulcast config layers for screenshare settings.
std::vector<webrtc::VideoStream> GetScreenshareLayers(
    size_t max_layers,
    int width,
    int height,
    double bitrate_priority,
    int max_qp,
    int max_framerate,
    bool screenshare_simulcast_enabled);

bool ScreenshareSimulcastFieldTrialEnabled();

}  // namespace cricket

#endif  // MEDIA_ENGINE_SIMULCAST_H_
