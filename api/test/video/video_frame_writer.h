/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_TEST_VIDEO_VIDEO_FRAME_WRITER_H_
#define API_TEST_VIDEO_VIDEO_FRAME_WRITER_H_

#include <memory>

#include "absl/strings/string_view.h"
#include "api/video/video_frame.h"

namespace webrtc {
namespace test {

class VideoFrameWriter {
 public:
  virtual ~VideoFrameWriter() = default;

  // Writes `VideoFrame` and returns true if operation was sucessful, false
  // otherwise.
  //
  // Can be invoked only until `Close` was invoked.
  virtual bool WriteFrame(const VideoFrame& frame) = 0;

  // Closes writer and cleans up all resources. No invokations to `WriteFrame`
  // are allowed after `Close` was invoked.
  virtual void Close() = 0;
};

}  // namespace test
}  // namespace webrtc

#endif  // API_TEST_VIDEO_VIDEO_FRAME_WRITER_H_
