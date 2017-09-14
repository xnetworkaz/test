/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_DESKTOP_CAPTURE_CAPTURE_RESULT_DESKTOP_CAPTURER_WRAPPER_H_
#define WEBRTC_MODULES_DESKTOP_CAPTURE_CAPTURE_RESULT_DESKTOP_CAPTURER_WRAPPER_H_

#include <memory>

#include "webrtc/modules/desktop_capture/desktop_capturer.h"
#include "webrtc/modules/desktop_capture/desktop_capturer_wrapper.h"

namespace webrtc {

// A DesktopCapturerWrapper implementation to capture the result of
// |base_capturer|. Derived classes are expected to provide a ResultObserver
// implementation to observe the DesktopFrame returned by |base_capturer_|.
class CaptureResultDesktopCapturerWrapper
    : public DesktopCapturerWrapper,
      public DesktopCapturer::Callback {
 public:
  using Callback = DesktopCapturer::Callback;

  // Provides a way to let derived classes or consumers to modify the result
  // returned by |base_capturer_|.
  class ResultObserver {
   public:
    ResultObserver();
    virtual ~ResultObserver();

    virtual Result Observe(Result result,
                           std::unique_ptr<DesktopFrame>* frame) = 0;
  };

  // |observer| must outlive this instance.
  CaptureResultDesktopCapturerWrapper(
      std::unique_ptr<DesktopCapturer> base_capturer,
      ResultObserver* observer);

  CaptureResultDesktopCapturerWrapper(
      std::unique_ptr<DesktopCapturer> base_capturer,
      std::unique_ptr<ResultObserver> observer);

  ~CaptureResultDesktopCapturerWrapper() override;

  // DesktopCapturer implementations.
  void Start(Callback* callback) final;

 private:
  // DesktopCapturer::Callback implementation.
  void OnCaptureResult(Result result,
                       std::unique_ptr<DesktopFrame> frame) override;

  // Maintains the lifetime of the input ResultObserver. It should not be used.
  const std::unique_ptr<ResultObserver> observer_;
  ResultObserver* const raw_observer_;
  Callback* callback_ = nullptr;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_DESKTOP_CAPTURE_CAPTURE_RESULT_DESKTOP_CAPTURER_WRAPPER_H_
