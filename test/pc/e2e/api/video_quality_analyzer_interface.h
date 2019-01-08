/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_PC_E2E_API_VIDEO_QUALITY_ANALYZER_INTERFACE_H_
#define TEST_PC_E2E_API_VIDEO_QUALITY_ANALYZER_INTERFACE_H_

#include <memory>
#include <string>

#include "absl/types/optional.h"
#include "api/video/encoded_image.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder.h"

namespace webrtc {

class VideoQualityAnalyzerInterface {
 public:
  virtual ~VideoQualityAnalyzerInterface() = default;

  // Will be called by framework before test. |threads_count| is number of extra
  // threads, that analyzer can use for heavy calculations.
  virtual void Start(uint16_t threads_count) = 0;

  // Will be called when frame was generated from the input stream.
  // Returns frame id to set.
  virtual uint16_t OnFrameCaptured(std::string stream_label,
                                   const VideoFrame& frame) = 0;
  // Will be called before calling the real encoder.
  virtual void OnFramePreEncode(const VideoFrame& frame) = 0;
  // Will be called for each EncodedImage received from encoder. Single
  // VideoFrame can produce multiple EncodedImages. Each encoded image will
  // have id from VideoFrame.
  virtual void OnFrameEncoded(uint16_t frame_id,
                              const EncodedImage& encoded_image) = 0;
  // Will be called for each frame dropped by encoder.
  virtual void OnFrameDropped(EncodedImageCallback::DropReason reason) = 0;
  // Will be called before calling the real decoder.
  virtual void OnFrameReceived(uint16_t frame_id,
                               const EncodedImage& encoded_image) = 0;
  // Will be called after decoding the frame. |decode_time_ms| is a decode
  // time provided by decoder itself. If decoder doesn’t produce such
  // information can be omitted.
  virtual void OnFrameDecoded(const VideoFrame& frame,
                              absl::optional<int32_t> decode_time_ms,
                              absl::optional<uint8_t> qp) = 0;
  // Will be called when frame will be obtained from PeerConnection stack.
  virtual void OnFrameRendered(const VideoFrame& frame) = 0;
  // Will be called if real encoder return not WEBRTC_VIDEO_CODEC_OK.
  virtual void OnEncoderError(const VideoFrame& frame, int32_t error_code) = 0;
  // Will be called if real decoder return not WEBRTC_VIDEO_CODEC_OK.
  virtual void OnDecoderError(uint16_t frame_id, int32_t error_code) = 0;

  // Tells analyzer, that analysis complete and it should calculate final
  // statistics.
  virtual void Stop() = 0;
};

}  // namespace webrtc

#endif  // TEST_PC_E2E_API_VIDEO_QUALITY_ANALYZER_INTERFACE_H_
