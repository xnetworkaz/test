/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_RECEIVE_STATISTICS_PROXY2_H_
#define VIDEO_RECEIVE_STATISTICS_PROXY2_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "api/task_queue/task_queue_base.h"
#include "call/video_receive_stream.h"
#include "modules/include/module_common_types.h"
#include "modules/video_coding/include/video_coding_defines.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/numerics/histogram_percentile_counter.h"
#include "rtc_base/numerics/moving_max_counter.h"
#include "rtc_base/numerics/sample_counter.h"
#include "rtc_base/rate_statistics.h"
#include "rtc_base/rate_tracker.h"
#include "rtc_base/synchronization/sequence_checker.h"
#include "rtc_base/task_utils/pending_task_safety_flag.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/thread_checker.h"
#include "video/quality_threshold.h"
#include "video/stats_counter.h"
#include "video/video_quality_observer2.h"

namespace webrtc {

class Clock;
struct CodecSpecificInfo;

namespace internal {

class ReceiveStatisticsProxy : public VCMReceiveStatisticsCallback,
                               public RtcpCnameCallback,
                               public RtcpPacketTypeCounterObserver,
                               public CallStatsObserver {
 public:
  ReceiveStatisticsProxy(const VideoReceiveStream::Config* config,
                         Clock* clock,
                         TaskQueueBase* worker_thread);
  ~ReceiveStatisticsProxy() override;

  VideoReceiveStream::Stats GetStats() const;

  void OnDecodedFrame(const VideoFrame& frame,
                      absl::optional<uint8_t> qp,
                      int32_t decode_time_ms,
                      VideoContentType content_type);
  void OnSyncOffsetUpdated(int64_t video_playout_ntp_ms,
                           int64_t sync_offset_ms,
                           double estimated_freq_khz);
  void OnRenderedFrame(const VideoFrame& frame);
  void OnIncomingPayloadType(int payload_type);
  void OnDecoderImplementationName(const char* implementation_name);

  void OnPreDecode(VideoCodecType codec_type, int qp);

  void OnUniqueFramesCounted(int num_unique_frames);

  // Indicates video stream has been paused (no incoming packets).
  void OnStreamInactive();

  // Overrides VCMReceiveStatisticsCallback.
  void OnCompleteFrame(bool is_keyframe,
                       size_t size_bytes,
                       VideoContentType content_type) override;
  void OnDroppedFrames(uint32_t frames_dropped) override;
  void OnFrameBufferTimingsUpdated(int max_decode_ms,
                                   int current_delay_ms,
                                   int target_delay_ms,
                                   int jitter_buffer_ms,
                                   int min_playout_delay_ms,
                                   int render_delay_ms) override;

  void OnTimingFrameInfoUpdated(const TimingFrameInfo& info) override;

  // Overrides RtcpCnameCallback.
  void OnCname(uint32_t ssrc, absl::string_view cname) override;

  // Overrides RtcpPacketTypeCounterObserver.
  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override;

  // Implements CallStatsObserver.
  void OnRttUpdate(int64_t avg_rtt_ms, int64_t max_rtt_ms) override;

  // Notification methods that are used to check our internal state and validate
  // threading assumptions. These are called by VideoReceiveStream.
  void DecoderThreadStarting();
  void DecoderThreadStopped();

  // Produce histograms. Must be called after DecoderThreadStopped(), typically
  // at the end of the call.
  void UpdateHistograms(absl::optional<int> fraction_lost,
                        const StreamDataCounters& rtp_stats,
                        const StreamDataCounters* rtx_stats);

 private:
  struct QpCounters {
    rtc::SampleCounter vp8;
  };

  struct ContentSpecificStats {
    ContentSpecificStats();
    ~ContentSpecificStats();

    void Add(const ContentSpecificStats& other);

    rtc::SampleCounter e2e_delay_counter;
    rtc::SampleCounter interframe_delay_counter;
    int64_t flow_duration_ms = 0;
    int64_t total_media_bytes = 0;
    rtc::SampleCounter received_width;
    rtc::SampleCounter received_height;
    rtc::SampleCounter qp_counter;
    FrameCounts frame_counts;
    rtc::HistogramPercentileCounter interframe_delay_percentiles;
  };

  void QualitySample() RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Removes info about old frames and then updates the framerate.
  void UpdateFramerate(int64_t now_ms) const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void UpdateDecodeTimeHistograms(int width,
                                  int height,
                                  int decode_time_ms) const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  absl::optional<int64_t> GetCurrentEstimatedPlayoutNtpTimestampMs(
      int64_t now_ms) const RTC_EXCLUSIVE_LOCKS_REQUIRED(crit_);

  Clock* const clock_;
  const int64_t start_ms_;
  const bool enable_decode_time_histograms_;

  rtc::CriticalSection crit_;
  int64_t last_sample_time_ RTC_GUARDED_BY(crit_);
  QualityThreshold fps_threshold_ RTC_GUARDED_BY(crit_);
  QualityThreshold qp_threshold_ RTC_GUARDED_BY(crit_);
  QualityThreshold variance_threshold_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter qp_sample_ RTC_GUARDED_BY(crit_);
  int num_bad_states_ RTC_GUARDED_BY(crit_);
  int num_certain_states_ RTC_GUARDED_BY(crit_);
  // Note: The |stats_.rtp_stats| member is not used or populated by this class.
  mutable VideoReceiveStream::Stats stats_ RTC_GUARDED_BY(crit_);
  RateStatistics decode_fps_estimator_ RTC_GUARDED_BY(crit_);
  RateStatistics renders_fps_estimator_ RTC_GUARDED_BY(crit_);
  rtc::RateTracker render_fps_tracker_ RTC_GUARDED_BY(crit_);
  rtc::RateTracker render_pixel_tracker_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter sync_offset_counter_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter decode_time_counter_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter jitter_buffer_delay_counter_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter target_delay_counter_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter current_delay_counter_ RTC_GUARDED_BY(crit_);
  rtc::SampleCounter delay_counter_ RTC_GUARDED_BY(crit_);
  std::unique_ptr<VideoQualityObserver> video_quality_observer_
      RTC_GUARDED_BY(crit_);
  mutable rtc::MovingMaxCounter<int> interframe_delay_max_moving_
      RTC_GUARDED_BY(crit_);
  std::map<VideoContentType, ContentSpecificStats> content_specific_stats_
      RTC_GUARDED_BY(crit_);
  MaxCounter freq_offset_counter_ RTC_GUARDED_BY(crit_);
  QpCounters qp_counters_ RTC_GUARDED_BY(decode_queue_);
  int64_t avg_rtt_ms_ RTC_GUARDED_BY(crit_);
  mutable std::map<int64_t, size_t> frame_window_ RTC_GUARDED_BY(&crit_);
  VideoContentType last_content_type_ RTC_GUARDED_BY(&crit_);
  VideoCodecType last_codec_type_ RTC_GUARDED_BY(&crit_);
  absl::optional<int64_t> first_frame_received_time_ms_ RTC_GUARDED_BY(&crit_);
  absl::optional<int64_t> first_decoded_frame_time_ms_ RTC_GUARDED_BY(&crit_);
  absl::optional<int64_t> last_decoded_frame_time_ms_ RTC_GUARDED_BY(&crit_);
  size_t num_delayed_frames_rendered_ RTC_GUARDED_BY(&crit_);
  int64_t sum_missed_render_deadline_ms_ RTC_GUARDED_BY(&crit_);
  // Mutable because calling Max() on MovingMaxCounter is not const. Yet it is
  // called from const GetStats().
  mutable rtc::MovingMaxCounter<TimingFrameInfo> timing_frame_info_counter_
      RTC_GUARDED_BY(&crit_);
  absl::optional<int> num_unique_frames_ RTC_GUARDED_BY(crit_);
  absl::optional<int64_t> last_estimated_playout_ntp_timestamp_ms_
      RTC_GUARDED_BY(&crit_);
  absl::optional<int64_t> last_estimated_playout_time_ms_
      RTC_GUARDED_BY(&crit_);

  // The thread on which this instance is constructed and some of its main
  // methods are invoked on such as GetStats().
  TaskQueueBase* const worker_thread_;

  PendingTaskSafetyFlag::Pointer task_safety_flag_{
      PendingTaskSafetyFlag::Create()};

  SequenceChecker decode_queue_;
  rtc::ThreadChecker main_thread_;
  SequenceChecker incoming_render_queue_;
};

}  // namespace internal
}  // namespace webrtc
#endif  // VIDEO_RECEIVE_STATISTICS_PROXY2_H_
