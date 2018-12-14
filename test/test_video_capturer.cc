/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/test_video_capturer.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "rtc_base/scoped_ref_ptr.h"

namespace webrtc {
namespace test {
TestVideoCapturer::TestVideoCapturer()
    : video_adapter_(new cricket::VideoAdapter()) {}
TestVideoCapturer::~TestVideoCapturer() {}

absl::optional<VideoFrame> TestVideoCapturer::AdaptFrame(
    const VideoFrame& frame) {
  int cropped_width = 0;
  int cropped_height = 0;
  int out_width = 0;
  int out_height = 0;

  if (!video_adapter_->AdaptFrameResolution(
          frame.width(), frame.height(), frame.timestamp_us() * 1000,
          &cropped_width, &cropped_height, &out_width, &out_height)) {
    // Drop frame in order to respect frame rate constraint.
    return absl::nullopt;
  }

  absl::optional<VideoFrame> out_frame;
  if (out_height != frame.height() || out_width != frame.width()) {
    // Video adapter has requested a down-scale. Allocate a new buffer and
    // return scaled version.
    rtc::scoped_refptr<I420Buffer> scaled_buffer =
        I420Buffer::Create(out_width, out_height);
    scaled_buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
    out_frame.emplace(VideoFrame::Builder()
                          .set_video_frame_buffer(scaled_buffer)
                          .set_rotation(kVideoRotation_0)
                          .set_timestamp_us(frame.timestamp_us())
                          .set_id(frame.id())
                          .build());
  } else {
    // No adaptations needed, just return the frame as is.
    out_frame.emplace(frame);
  }

  return out_frame;
}

void TestVideoCapturer::AddOrUpdateSink(
    rtc::VideoSinkInterface<VideoFrame>* sink,
    const rtc::VideoSinkWants& wants) {
  video_adapter_->OnResolutionFramerateRequest(
      wants.target_pixel_count, wants.max_pixel_count, wants.max_framerate_fps);
}

}  // namespace test
}  // namespace webrtc
