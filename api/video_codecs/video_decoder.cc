/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video_codecs/video_decoder.h"

namespace webrtc {

int32_t DecodedImageCallback::Decoded(VideoFrame& decodedImage,
                                      int64_t decode_time_ms) {
  // The default implementation ignores custom decode time value.
  return Decoded(decodedImage);
}

void DecodedImageCallback::Decoded(VideoFrame& decodedImage,
                                   rtc::Optional<int32_t> decode_time_ms,
                                   rtc::Optional<uint8_t> qp) {
  Decoded(decodedImage,
          decode_time_ms ? static_cast<int32_t>(*decode_time_ms) : -1);
}

int32_t DecodedImageCallback::ReceivedDecodedReferenceFrame(
    const uint64_t pictureId) {
  return -1;
}

int32_t DecodedImageCallback::ReceivedDecodedFrame(const uint64_t pictureId) {
  return -1;
}

bool VideoDecoder::PrefersLateDecoding() const {
  return true;
}

const char* VideoDecoder::ImplementationName() const {
  return "unknown";
}

}  // namespace webrtc
