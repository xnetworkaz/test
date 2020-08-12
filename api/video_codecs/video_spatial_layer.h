/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_VIDEO_CODECS_VIDEO_SPATIAL_LAYER_H_
#define API_VIDEO_CODECS_VIDEO_SPATIAL_LAYER_H_

namespace webrtc {

struct VideoSpatialLayer {
  bool operator==(const VideoSpatialLayer& other) const;
  bool operator!=(const VideoSpatialLayer& other) const {
    return !(*this == other);
  }

  unsigned short width;
  unsigned short height;
  float maxFramerate;  // fps.
  unsigned char numberOfTemporalLayers;
  unsigned int maxBitrate;     // kilobits/sec.
  unsigned int targetBitrate;  // kilobits/sec.
  unsigned int minBitrate;     // kilobits/sec.
  unsigned int qpMax;          // minimum quality
  bool active;                 // encoded and sent.
};

}  // namespace webrtc
#endif  // API_VIDEO_CODECS_VIDEO_SPATIAL_LAYER_H_
