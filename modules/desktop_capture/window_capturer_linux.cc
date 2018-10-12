/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_capturer.h"

#if defined(USE_PIPEWIRE)
#include "modules/desktop_capture/linux/window_capturer_pipewire.h"
#endif  // defined(USE_PIPEWIRE)

#if defined(USE_X11)
#include "modules/desktop_capture/linux/window_capturer_x11.h"
#endif  // defined(USE_X11)

namespace webrtc {

// static
std::unique_ptr<DesktopCapturer> DesktopCapturer::CreateRawWindowCapturer(
    const DesktopCaptureOptions& options) {
#if defined(USE_PIPEWIRE)
  if (options.allow_pipewire() && DesktopCapturer::IsRunningUnderWayland()) {
    return std::unique_ptr<DesktopCapturer>(new WindowCapturerPipeWire());
  }
#endif  // defined(USE_PIPEWIRE)

#if defined(USE_X11)
  return WindowCapturerX11::CreateRawWindowCapturer(options);
#endif  // defined(USE_X11)

  return nullptr;
}

}  // namespace webrtc
