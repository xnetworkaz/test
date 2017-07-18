/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_DESKTOP_CAPTURE_DESKTOP_AND_CURSOR_COMPOSER_H_
#define WEBRTC_MODULES_DESKTOP_CAPTURE_DESKTOP_AND_CURSOR_COMPOSER_H_

#include <memory>

#include "webrtc/modules/desktop_capture/desktop_capture_options.h"
#include "webrtc/modules/desktop_capture/desktop_capturer.h"
#include "webrtc/modules/desktop_capture/mouse_cursor_monitor.h"
#include "webrtc/rtc_base/constructormagic.h"

namespace webrtc {

// A wrapper for DesktopCapturer that also captures mouse using specified
// MouseCursorMonitor and renders it on the generated streams.
class DesktopAndCursorComposer : public DesktopCapturer,
                                 public DesktopCapturer::Callback,
                                 public MouseCursorMonitor::Callback {
 public:
  // Creates a new blender that captures mouse cursor using |mouse_monitor| and
  // renders it into the frames generated by |desktop_capturer|. If
  // |mouse_monitor| is NULL the frames are passed unmodified. Takes ownership
  // of both arguments.
  // Deprecated: use the constructor below.
  DesktopAndCursorComposer(DesktopCapturer* desktop_capturer,
                           MouseCursorMonitor* mouse_monitor);

  // Creates a new blender that captures mouse cursor using
  // MouseCursorMonitor::Create(options) and renders it into the frames
  // generated by |desktop_capturer|.
  DesktopAndCursorComposer(std::unique_ptr<DesktopCapturer> desktop_capturer,
                           const DesktopCaptureOptions& options);

  ~DesktopAndCursorComposer() override;

  // DesktopCapturer interface.
  void Start(DesktopCapturer::Callback* callback) override;
  void SetSharedMemoryFactory(
      std::unique_ptr<SharedMemoryFactory> shared_memory_factory) override;
  void CaptureFrame() override;
  void SetExcludedWindow(WindowId window) override;

 private:
  // DesktopCapturer::Callback interface.
  void OnCaptureResult(DesktopCapturer::Result result,
                       std::unique_ptr<DesktopFrame> frame) override;

  // MouseCursorMonitor::Callback interface.
  void OnMouseCursor(MouseCursor* cursor) override;
  void OnMouseCursorPosition(MouseCursorMonitor::CursorState state,
                             const DesktopVector& position) override;

  const std::unique_ptr<DesktopCapturer> desktop_capturer_;
  const std::unique_ptr<MouseCursorMonitor> mouse_monitor_;

  DesktopCapturer::Callback* callback_;

  std::unique_ptr<MouseCursor> cursor_;
  MouseCursorMonitor::CursorState cursor_state_;
  DesktopVector cursor_position_;

  RTC_DISALLOW_COPY_AND_ASSIGN(DesktopAndCursorComposer);
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_DESKTOP_CAPTURE_DESKTOP_AND_CURSOR_COMPOSER_H_
