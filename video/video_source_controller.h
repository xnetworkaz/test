/*
 *  Copyright 2019 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_VIDEO_SOURCE_CONTROLLER_H_
#define VIDEO_VIDEO_SOURCE_CONTROLLER_H_

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "call/adaptation/resource_adaptation_module_interface.h"
#include "rtc_base/critical_section.h"

namespace webrtc {

// VideoSourceProxy is soooooooooooooooooooooooooooooooooooooooooooooooo 2019.
// TODO(hbos): Do the corresponding logging that VideoSourceProxy did.
class VideoSourceController {
 public:
  VideoSourceController(rtc::VideoSinkInterface<VideoFrame>* sink,
                        rtc::VideoSourceInterface<VideoFrame>* source);

  void SetSource(rtc::VideoSourceInterface<VideoFrame>* source);
  // Must be called in order fo changes to settings to have an effect.
  void PushSourceSinkSettings();

  VideoSourceRestrictions restrictions() const;
  absl::optional<size_t> pixels_per_frame_upper_limit() const;
  absl::optional<double> frame_rate_upper_limit() const;
  bool rotation_applied() const;
  int resolution_alignment() const;

  void SetRestrictions(VideoSourceRestrictions restrictions);
  void SetPixelsPerFrameUpperLimit(
      absl::optional<size_t> pixels_per_frame_upper_limit);
  void SetFrameRateUpperLimit(absl::optional<double> frame_rate_upper_limit);
  void SetRotationApplied(bool rotation_applied);
  void SetResolutionAlignment(int resolution_alignment);

  rtc::VideoSinkWants CurrentSettingsToSinkWantsForTesting() const;

 private:
  rtc::VideoSinkWants CurrentSettingsToSinkWants() const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // TODO(hbos): Handle everything on the same sequence (VideoStreamEncoder's
  // encoder queue) and replace |crit_| with a sequence checker.
  mutable rtc::CriticalSection crit_;
  rtc::VideoSinkInterface<VideoFrame>* const sink_;
  rtc::VideoSourceInterface<VideoFrame>* source_ RTC_GUARDED_BY(&crit_);
  // Pixel and frame rate restrictions.
  VideoSourceRestrictions restrictions_ RTC_GUARDED_BY(&crit_);
  // Ensures that even if we are not restricted, the sink is never configured
  // above this limit. Example: We are not CPU limited (no |restrictions_|) but
  // our encoder is capped at 30 fps (= |max_frame_rate_upper_limit_|).
  absl::optional<size_t> pixels_per_frame_upper_limit_ RTC_GUARDED_BY(&crit_);
  absl::optional<double> frame_rate_upper_limit_ RTC_GUARDED_BY(&crit_);
  bool rotation_applied_ RTC_GUARDED_BY(&crit_) = false;
  int resolution_alignment_ RTC_GUARDED_BY(&crit_) = 1;
};

}  // namespace webrtc

#endif  // VIDEO_VIDEO_SOURCE_CONTROLLER_H_
