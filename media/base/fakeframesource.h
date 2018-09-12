/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_BASE_FAKEFRAMESOURCE_H_
#define MEDIA_BASE_FAKEFRAMESOURCE_H_

#include "api/video/video_frame.h"

#include "rtc_base/timeutils.h"

namespace cricket {

class FakeFrameSource {
 public:
  FakeFrameSource(int width, int height, int interval_us);
  FakeFrameSource(int width,
                  int height,
                  int interval_us,
                  int64_t timestamp_offset_us);

  webrtc::VideoRotation GetRotation() const;
  void SetRotation(webrtc::VideoRotation rotation);

  webrtc::VideoFrame GetFrame();
  webrtc::VideoFrame GetFrameRotationApplied();

  // Override configuration.
  webrtc::VideoFrame GetFrame(int width,
                              int height,
                              webrtc::VideoRotation rotation,
                              int interval_us);

 private:
  const int width_;
  const int height_;
  const int interval_us_;
  const int64_t timestamp_offset_us_;

  webrtc::VideoRotation rotation_ = webrtc::kVideoRotation_0;
  int64_t next_timestamp_us_ = 0;
};

}  // namespace cricket

#endif  // MEDIA_BASE_FAKEFRAMESOURCE_H_
