/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/desktop_capture/capture_result_desktop_capturer_wrapper.h"

#include <memory>
#include <utility>

#include "webrtc/rtc_base/checks.h"

namespace webrtc {

CaptureResultDesktopCapturerWrapper::CaptureResultDesktopCapturerWrapper(
    std::unique_ptr<DesktopCapturer> base_capturer,
    ResultObserver* observer)
    : DesktopCapturerWrapper(std::move(base_capturer)),
      observer_(),
      raw_observer_(observer) {
  RTC_DCHECK(raw_observer_);
}

CaptureResultDesktopCapturerWrapper::CaptureResultDesktopCapturerWrapper(
    std::unique_ptr<DesktopCapturer> base_capturer,
    std::unique_ptr<ResultObserver> observer)
    : DesktopCapturerWrapper(std::move(base_capturer)),
      observer_(std::move(observer)),
      raw_observer_(observer_.get()) {
  RTC_DCHECK(raw_observer_);
}

CaptureResultDesktopCapturerWrapper::
~CaptureResultDesktopCapturerWrapper() = default;

void CaptureResultDesktopCapturerWrapper::Start(Callback* callback) {
  if ((callback_ == nullptr) != (callback == nullptr)) {
    if (callback) {
      base_capturer_->Start(this);
    } else {
      base_capturer_->Start(nullptr);
    }
  }
  callback_ = callback;
}

void CaptureResultDesktopCapturerWrapper::OnCaptureResult(
    Result result,
    std::unique_ptr<DesktopFrame> frame) {
  result = raw_observer_->Observe(result, &frame);
  callback_->OnCaptureResult(result, std::move(frame));
}

}  // namespace webrtc
