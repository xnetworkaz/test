/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_FRAME_BUFFER2_H_
#define MODULES_VIDEO_CODING_FRAME_BUFFER2_H_

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "api/video/encoded_frame.h"
#include "modules/video_coding/include/video_coding_defines.h"
#include "modules/video_coding/inter_frame_delay.h"
#include "modules/video_coding/utility/decoded_frames_history.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/event.h"
#include "rtc_base/experiments/rtt_mult_experiment.h"
#include "rtc_base/numerics/sequence_number_util.h"
#include "rtc_base/task_queue.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class Clock;
class VCMReceiveStatisticsCallback;
class VCMJitterEstimator;
class VCMTiming;

namespace video_coding {

class FrameBuffer {
 public:
  enum ReturnReason { kFrameFound, kTimeout, kStopped };

  FrameBuffer(Clock* clock,
              VCMJitterEstimator* jitter_estimator,
              VCMTiming* timing,
              VCMReceiveStatisticsCallback* stats_callback);

  virtual ~FrameBuffer();

  // Insert a frame into the frame buffer. Returns the picture id
  // of the last continuous frame or -1 if there is no continuous frame.
  // TODO(philipel): Return a VideoLayerFrameId and not only the picture id.
  int64_t InsertFrame(std::unique_ptr<EncodedFrame> frame);

  // Get the next frame for decoding. Will return at latest after
  // |max_wait_time_ms|.
  //  - If a frame is available within |max_wait_time_ms| it will return
  //    kFrameFound and set |frame_out| to the resulting frame.
  //  - If no frame is available after |max_wait_time_ms| it will return
  //    kTimeout.
  //  - If the FrameBuffer is stopped then it will return kStopped.
  ReturnReason NextFrame(int64_t max_wait_time_ms,
                         std::unique_ptr<EncodedFrame>* frame_out,
                         bool keyframe_required = false);
  void NextFrame(
      int64_t max_wait_time_ms,
      bool keyframe_required,
      rtc::TaskQueue* callback_queue,
      std::function<void(std::unique_ptr<EncodedFrame>, ReturnReason)> handler);

  // Tells the FrameBuffer which protection mode that is in use. Affects
  // the frame timing.
  // TODO(philipel): Remove this when new timing calculations has been
  //                 implemented.
  void SetProtectionMode(VCMVideoProtection mode);

  // Start the frame buffer, has no effect if the frame buffer is started.
  // The frame buffer is started upon construction.
  void Start();

  // Stop the frame buffer, causing any sleeping thread in NextFrame to
  // return immediately.
  void Stop();

  // Updates the RTT for jitter buffer estimation.
  void UpdateRtt(int64_t rtt_ms);

  // Clears the FrameBuffer, removing all the buffered frames.
  void Clear();

 private:
  struct FrameInfo {
    FrameInfo();
    FrameInfo(FrameInfo&&);
    ~FrameInfo();

    // Which other frames that have direct unfulfilled dependencies
    // on this frame.
    absl::InlinedVector<VideoLayerFrameId, 8> dependent_frames;

    // A frame is continiuous if it has all its referenced/indirectly
    // referenced frames.
    //
    // How many unfulfilled frames this frame have until it becomes continuous.
    size_t num_missing_continuous = 0;

    // A frame is decodable if all its referenced frames have been decoded.
    //
    // How many unfulfilled frames this frame have until it becomes decodable.
    size_t num_missing_decodable = 0;

    // If this frame is continuous or not.
    bool continuous = false;

    // The actual EncodedFrame.
    std::unique_ptr<EncodedFrame> frame;
  };

  using FrameMap = std::map<VideoLayerFrameId, FrameInfo>;

  // Check that the references of |frame| are valid.
  bool ValidReferences(const EncodedFrame& frame) const;

  int64_t UpdateFramesToDecode(int64_t now_ms)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);
  EncodedFrame* GetFrameToDecode() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void StartWaitForNextFrameOnQueue() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);
  void CancelCallback() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Update all directly dependent and indirectly dependent frames and mark
  // them as continuous if all their references has been fulfilled.
  void PropagateContinuity(FrameMap::iterator start)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Marks the frame as decoded and updates all directly dependent frames.
  void PropagateDecodability(const FrameInfo& info)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Update the corresponding FrameInfo of |frame| and all FrameInfos that
  // |frame| references.
  // Return false if |frame| will never be decodable, true otherwise.
  bool UpdateFrameInfoWithIncomingFrame(const EncodedFrame& frame,
                                        FrameMap::iterator info)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void UpdateJitterDelay() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void UpdateTimingFrameInfo() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void ClearFramesAndHistory() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Checks if the superframe, which current frame belongs to, is complete.
  bool IsCompleteSuperFrame(const EncodedFrame& frame)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  bool HasBadRenderTiming(const EncodedFrame& frame, int64_t now_ms)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // The cleaner solution would be to have the NextFrame function return a
  // vector of frames, but until the decoding pipeline can support decoding
  // multiple frames at the same time we combine all frames to one frame and
  // return it. See bugs.webrtc.org/10064
  EncodedFrame* CombineAndDeleteFrames(
      const std::vector<EncodedFrame*>& frames) const;

  // Stores only undecoded frames.
  FrameMap frames_ RTC_GUARDED_BY(crit_);
  DecodedFramesHistory decoded_frames_history_ RTC_GUARDED_BY(crit_);

  rtc::CriticalSection crit_;
  Clock* const clock_;

  rtc::TaskQueue* callback_queue_ RTC_GUARDED_BY(crit_);
  RepeatingTaskHandle callback_task_ RTC_GUARDED_BY(crit_);
  std::function<void(std::unique_ptr<EncodedFrame>, ReturnReason)>
      frame_handler_ RTC_GUARDED_BY(crit_);
  int64_t latest_return_time_ms_ RTC_GUARDED_BY(crit_);
  bool keyframe_required_ RTC_GUARDED_BY(crit_);

  rtc::Event new_continuous_frame_event_;
  VCMJitterEstimator* const jitter_estimator_ RTC_GUARDED_BY(crit_);
  VCMTiming* const timing_ RTC_GUARDED_BY(crit_);
  VCMInterFrameDelay inter_frame_delay_ RTC_GUARDED_BY(crit_);
  absl::optional<VideoLayerFrameId> last_continuous_frame_
      RTC_GUARDED_BY(crit_);
  std::vector<FrameMap::iterator> frames_to_decode_ RTC_GUARDED_BY(crit_);
  bool stopped_ RTC_GUARDED_BY(crit_);
  VCMVideoProtection protection_mode_ RTC_GUARDED_BY(crit_);
  VCMReceiveStatisticsCallback* const stats_callback_;
  int64_t last_log_non_decoded_ms_ RTC_GUARDED_BY(crit_);

  const bool add_rtt_to_playout_delay_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(FrameBuffer);
};

}  // namespace video_coding
}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_FRAME_BUFFER2_H_
