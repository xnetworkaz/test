/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_STREAM_RECEIVER_FRAME_TRANSFORMER_DELEGATE_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_STREAM_RECEIVER_FRAME_TRANSFORMER_DELEGATE_H_

#include <memory>
#include <vector>

#include "api/frame_transformer_interface.h"
#include "api/metronome/metronome.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "modules/rtp_rtcp/source/frame_object.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

// Called back by RtpVideoStreamReceiverFrameTransformerDelegate on the network
// thread after transformation.
class RtpVideoFrameReceiver {
 public:
  virtual void ManageFrame(std::unique_ptr<RtpFrameObject> frame) = 0;

 protected:
  virtual ~RtpVideoFrameReceiver() = default;
};

// Delegates calls to FrameTransformerInterface to transform frames, and to
// RtpVideoStreamReceiver to manage transformed frames on the `network_thread_`.
class RtpVideoStreamReceiverFrameTransformerDelegate
    : public TransformedFrameCallback {
 public:
  RtpVideoStreamReceiverFrameTransformerDelegate(
      RtpVideoFrameReceiver* receiver,
      Clock* clock,
      rtc::scoped_refptr<FrameTransformerInterface> frame_transformer,
      rtc::Thread* network_thread,
      uint32_t ssrc,
      Metronome* metronome);

  void Init();
  void Reset();

  // Delegates the call to FrameTransformerInterface::TransformFrame.
  void TransformFrame(std::unique_ptr<RtpFrameObject> frame);

  // Implements TransformedFrameCallback. Can be called on any thread. Posts
  // the transformed frame to be managed on the `network_thread_`.
  void OnTransformedFrame(
      std::unique_ptr<TransformableFrameInterface> frame) override;

  // Delegates the call to RtpVideoFrameReceiver::ManageFrame on the
  // `network_thread_`.
  void ManageFrame(std::unique_ptr<TransformableFrameInterface> frame);

 protected:
  ~RtpVideoStreamReceiverFrameTransformerDelegate() override = default;

 private:
  void InvokeQueuedTransforms();

  RTC_NO_UNIQUE_ADDRESS SequenceChecker network_sequence_checker_;
  RtpVideoFrameReceiver* receiver_ RTC_GUARDED_BY(network_sequence_checker_);
  rtc::scoped_refptr<FrameTransformerInterface> frame_transformer_
      RTC_GUARDED_BY(network_sequence_checker_);
  rtc::Thread* const network_thread_;
  const uint32_t ssrc_;
  Clock* const clock_;
  Metronome* metronome_;
  bool tick_scheduled_ = false RTC_GUARDED_BY(network_sequence_checker_);
  std::vector<std::unique_ptr<RtpFrameObject>> queued_frames_
      RTC_GUARDED_BY(network_sequence_checker_);
  ScopedTaskSafetyDetached safety_;
};

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_STREAM_RECEIVER_FRAME_TRANSFORMER_DELEGATE_H_
