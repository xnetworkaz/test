/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_DESKTOP_CAPTURE_WIN_FULL_SCREEN_WINDOW_DETECTOR_WIN_H_
#define MODULES_DESKTOP_CAPTURE_WIN_FULL_SCREEN_WINDOW_DETECTOR_WIN_H_

#include "modules/desktop_capture/full_screen_window_detector.h"

namespace webrtc {

class FullScreenWindowDetectorWin : public FullScreenWindowDetector {
 protected:
  std::unique_ptr<FullScreenApplicationHandler> CreateApplicationHandler(
      DesktopCapturer::SourceId source_id) override;
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_WIN_FULL_SCREEN_WINDOW_DETECTOR_WIN_H_
