/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_CODECS_VP8_INCLUDE_VP8_H_
#define MODULES_VIDEO_CODING_CODECS_VP8_INCLUDE_VP8_H_

#include <memory>

#include "api/video_codecs/vp8_frame_buffer_controller.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/deprecation.h"

namespace webrtc {

// TODO(brandtr): Move these interfaces to the api/ folder.
class VP8Encoder {
 public:
  struct Settings {
    // Allows for overriding the Vp8FrameBufferController used by the encoder.
    // If unset, a default Vp8FrameBufferController will be instantiated
    // internally.
    std::unique_ptr<Vp8FrameBufferControllerFactory>
        frame_buffer_controller_factory = nullptr;

    // TODO(https://bugs.webrtc.org/11436): Add resolution_bitrate_limits.
  };

  static std::unique_ptr<VideoEncoder> Create();
  static std::unique_ptr<VideoEncoder> Create(Settings settings);

  RTC_DEPRECATED static std::unique_ptr<VideoEncoder> Create(
      std::unique_ptr<Vp8FrameBufferControllerFactory>
          frame_buffer_controller_factory);
};

class VP8Decoder {
 public:
  static std::unique_ptr<VideoDecoder> Create();
};

}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_CODECS_VP8_INCLUDE_VP8_H_
