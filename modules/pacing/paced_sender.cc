/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/pacing/paced_sender.h"

#include <algorithm>
#include <utility>

#include "absl/memory/memory.h"
#include "logging/rtc_event_log/rtc_event_log.h"
#include "modules/congestion_controller/goog_cc/alr_detector.h"
#include "modules/pacing/bitrate_prober.h"
#include "modules/pacing/interval_budget.h"
#include "modules/utility/include/process_thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {
namespace {
// Time limit in milliseconds between packet bursts.
const int64_t kDefaultMinPacketLimitMs = 5;
const int64_t kCongestedPacketIntervalMs = 500;
const int64_t kPausedProcessIntervalMs = kCongestedPacketIntervalMs;
const int64_t kMaxElapsedTimeMs = 2000;

// Upper cap on process interval, in case process has not been called in a long
// time.
const int64_t kMaxIntervalTimeMs = 30;

bool IsDisabled(const WebRtcKeyValueConfig& field_trials,
                absl::string_view key) {
  return field_trials.Lookup(key).find("Disabled") == 0;
}

bool IsEnabled(const WebRtcKeyValueConfig& field_trials,
               absl::string_view key) {
  return field_trials.Lookup(key).find("Enabled") == 0;
}

}  // namespace
const int64_t PacedSender::kMaxQueueLengthMs = 2000;
const float PacedSender::kDefaultPaceMultiplier = 2.5f;

PacedSender::PacedSender(Clock* clock,
                         PacketSender* packet_sender,
                         RtcEventLog* event_log,
                         const WebRtcKeyValueConfig* field_trials)
    : clock_(clock),
      packet_sender_(packet_sender),
      fallback_field_trials_(
          !field_trials ? absl::make_unique<FieldTrialBasedConfig>() : nullptr),
      field_trials_(field_trials ? field_trials : fallback_field_trials_.get()),
      alr_detector_(),
      drain_large_queues_(
          !IsDisabled(*field_trials_, "WebRTC-Pacer-DrainQueue")),
      send_padding_if_silent_(
          IsEnabled(*field_trials_, "WebRTC-Pacer-PadInSilence")),
      pace_audio_(!IsDisabled(*field_trials_, "WebRTC-Pacer-BlockAudio")),
      min_packet_limit_ms_("", kDefaultMinPacketLimitMs),
      last_timestamp_ms_(clock_->TimeInMilliseconds()),
      paused_(false),
      media_budget_(0),
      padding_budget_(0),
      prober_(*field_trials_),
      probing_send_failure_(false),
      estimated_bitrate_bps_(0),
      min_send_bitrate_kbps_(0u),
      max_padding_bitrate_kbps_(0u),
      pacing_bitrate_kbps_(0),
      time_last_process_us_(clock->TimeInMicroseconds()),
      last_send_time_us_(clock->TimeInMicroseconds()),
      first_sent_packet_ms_(-1),
      packets_(clock->TimeInMicroseconds()),
      packet_counter_(0),
      pacing_factor_(kDefaultPaceMultiplier),
      queue_time_limit(kMaxQueueLengthMs),
      account_for_audio_(false) {
  if (!drain_large_queues_) {
    RTC_LOG(LS_WARNING) << "Pacer queues will not be drained,"
                           "pushback experiment must be enabled.";
  }
  ParseFieldTrial({&min_packet_limit_ms_},
                  field_trials_->Lookup("WebRTC-Pacer-MinPacketLimitMs"));
  UpdateBudgetWithElapsedTime(min_packet_limit_ms_);
}

PacedSender::~PacedSender() {}

void PacedSender::CreateProbeCluster(int bitrate_bps, int cluster_id) {
  rtc::CritScope cs(&critsect_);
  prober_.CreateProbeCluster(bitrate_bps, TimeMilliseconds(), cluster_id);
}

void PacedSender::Pause() {
  {
    rtc::CritScope cs(&critsect_);
    if (!paused_)
      RTC_LOG(LS_INFO) << "PacedSender paused.";
    paused_ = true;
    packets_.SetPauseState(true, TimeMilliseconds());
  }
  rtc::CritScope cs(&process_thread_lock_);
  // Tell the process thread to call our TimeUntilNextProcess() method to get
  // a new (longer) estimate for when to call Process().
  if (process_thread_)
    process_thread_->WakeUp(this);
}

void PacedSender::Resume() {
  {
    rtc::CritScope cs(&critsect_);
    if (paused_)
      RTC_LOG(LS_INFO) << "PacedSender resumed.";
    paused_ = false;
    packets_.SetPauseState(false, TimeMilliseconds());
  }
  rtc::CritScope cs(&process_thread_lock_);
  // Tell the process thread to call our TimeUntilNextProcess() method to
  // refresh the estimate for when to call Process().
  if (process_thread_)
    process_thread_->WakeUp(this);
}

void PacedSender::SetCongestionWindow(int64_t congestion_window_bytes) {
  rtc::CritScope cs(&critsect_);
  congestion_window_bytes_ = congestion_window_bytes;
}

void PacedSender::UpdateOutstandingData(int64_t outstanding_bytes) {
  rtc::CritScope cs(&critsect_);
  outstanding_bytes_ = outstanding_bytes;
}

bool PacedSender::Congested() const {
  if (congestion_window_bytes_ == kNoCongestionWindow)
    return false;
  return outstanding_bytes_ >= congestion_window_bytes_;
}

int64_t PacedSender::TimeMilliseconds() const {
  int64_t time_ms = clock_->TimeInMilliseconds();
  if (time_ms < last_timestamp_ms_) {
    RTC_LOG(LS_WARNING)
        << "Non-monotonic clock behavior observed. Previous timestamp: "
        << last_timestamp_ms_ << ", new timestamp: " << time_ms;
    RTC_DCHECK_GE(time_ms, last_timestamp_ms_);
    time_ms = last_timestamp_ms_;
  }
  last_timestamp_ms_ = time_ms;
  return time_ms;
}

void PacedSender::SetProbingEnabled(bool enabled) {
  rtc::CritScope cs(&critsect_);
  RTC_CHECK_EQ(0, packet_counter_);
  prober_.SetEnabled(enabled);
}

void PacedSender::SetEstimatedBitrate(uint32_t bitrate_bps) {
  if (bitrate_bps == 0)
    RTC_LOG(LS_ERROR) << "PacedSender is not designed to handle 0 bitrate.";
  rtc::CritScope cs(&critsect_);
  estimated_bitrate_bps_ = bitrate_bps;
  padding_budget_.set_target_rate_kbps(
      std::min(estimated_bitrate_bps_ / 1000, max_padding_bitrate_kbps_));
  pacing_bitrate_kbps_ =
      std::max(min_send_bitrate_kbps_, estimated_bitrate_bps_ / 1000) *
      pacing_factor_;
  if (!alr_detector_)
    alr_detector_ = absl::make_unique<AlrDetector>(field_trials_);
  alr_detector_->SetEstimatedBitrate(bitrate_bps);
}

void PacedSender::SetSendBitrateLimits(int min_send_bitrate_bps,
                                       int padding_bitrate) {
  rtc::CritScope cs(&critsect_);
  min_send_bitrate_kbps_ = min_send_bitrate_bps / 1000;
  pacing_bitrate_kbps_ =
      std::max(min_send_bitrate_kbps_, estimated_bitrate_bps_ / 1000) *
      pacing_factor_;
  max_padding_bitrate_kbps_ = padding_bitrate / 1000;
  padding_budget_.set_target_rate_kbps(
      std::min(estimated_bitrate_bps_ / 1000, max_padding_bitrate_kbps_));
}

void PacedSender::SetPacingRates(uint32_t pacing_rate_bps,
                                 uint32_t padding_rate_bps) {
  rtc::CritScope cs(&critsect_);
  RTC_DCHECK(pacing_rate_bps > 0);
  pacing_bitrate_kbps_ = pacing_rate_bps / 1000;
  padding_budget_.set_target_rate_kbps(padding_rate_bps / 1000);

  RTC_LOG(LS_VERBOSE) << "bwe:pacer_updated pacing_kbps="
                      << pacing_bitrate_kbps_
                      << " padding_budget_kbps=" << padding_rate_bps / 1000;
}

void PacedSender::InsertPacket(RtpPacketPacer::Priority priority,
                               uint32_t ssrc,
                               uint16_t sequence_number,
                               int64_t capture_time_ms,
                               size_t bytes,
                               bool retransmission) {
  rtc::CritScope cs(&critsect_);
  RTC_DCHECK(pacing_bitrate_kbps_ > 0)
      << "SetPacingRate must be called before InsertPacket.";

  int64_t now_ms = TimeMilliseconds();
  prober_.OnIncomingPacket(bytes);

  if (capture_time_ms < 0)
    capture_time_ms = now_ms;

  packets_.Push(priority, ssrc, sequence_number, capture_time_ms, now_ms, bytes,
                retransmission, packet_counter_++);
}

void PacedSender::EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet,
                                PacketType type) {
  rtc::CritScope cs(&critsect_);
  RTC_DCHECK(pacing_bitrate_kbps_ > 0)
      << "SetPacingRate must be called before InsertPacket.";

  int64_t now_ms = TimeMilliseconds();
  prober_.OnIncomingPacket(packet->payload_size());

  if (packet->capture_time_ms() < 0) {
    packet->set_capture_time_ms(now_ms);
  }

  int priority;
  switch (type) {
    case PacketType::kAudio:
      // Audio is always prioritized over other packet types.
      priority = 0;
      break;
    case PacketType::kRetransmission:
      // Send retransmissions before new media.
      priority = 1;
      break;
    case PacketType::kVideo:
      // Video has "normal" priority, in the old speak.
      priority = 2;
      break;
    case PacketType::kRedundancy:
      // Redundancy is OK to drop, but is the contents is hopefully not useless.
      priority = 3;
      break;
    case PacketType::kPadding:
      // Packets that are in themselves likely useless, only sent to keep up
      // BWE.
      priority = 4;
      break;
  }

  packets_.Push(priority, now_ms, type == PacketType::kRetransmission,
                packet_counter_++, std::move(packet));
}

void PacedSender::SetAccountForAudioPackets(bool account_for_audio) {
  rtc::CritScope cs(&critsect_);
  account_for_audio_ = account_for_audio;
}

int64_t PacedSender::ExpectedQueueTimeMs() const {
  rtc::CritScope cs(&critsect_);
  RTC_DCHECK_GT(pacing_bitrate_kbps_, 0);
  return static_cast<int64_t>(packets_.SizeInBytes() * 8 /
                              pacing_bitrate_kbps_);
}

absl::optional<int64_t> PacedSender::GetApplicationLimitedRegionStartTime() {
  rtc::CritScope cs(&critsect_);
  if (!alr_detector_)
    alr_detector_ = absl::make_unique<AlrDetector>(field_trials_);
  return alr_detector_->GetApplicationLimitedRegionStartTime();
}

size_t PacedSender::QueueSizePackets() const {
  rtc::CritScope cs(&critsect_);
  return packets_.SizeInPackets();
}

int64_t PacedSender::QueueSizeBytes() const {
  rtc::CritScope cs(&critsect_);
  return packets_.SizeInBytes();
}

int64_t PacedSender::FirstSentPacketTimeMs() const {
  rtc::CritScope cs(&critsect_);
  return first_sent_packet_ms_;
}

int64_t PacedSender::QueueInMs() const {
  rtc::CritScope cs(&critsect_);

  int64_t oldest_packet = packets_.OldestEnqueueTimeMs();
  if (oldest_packet == 0)
    return 0;

  return TimeMilliseconds() - oldest_packet;
}

int64_t PacedSender::TimeUntilNextProcess() {
  rtc::CritScope cs(&critsect_);
  int64_t elapsed_time_us =
      clock_->TimeInMicroseconds() - time_last_process_us_;
  int64_t elapsed_time_ms = (elapsed_time_us + 500) / 1000;
  // When paused we wake up every 500 ms to send a padding packet to ensure
  // we won't get stuck in the paused state due to no feedback being received.
  if (paused_)
    return std::max<int64_t>(kPausedProcessIntervalMs - elapsed_time_ms, 0);

  if (prober_.IsProbing()) {
    int64_t ret = prober_.TimeUntilNextProbe(TimeMilliseconds());
    if (ret > 0 || (ret == 0 && !probing_send_failure_))
      return ret;
  }
  return std::max<int64_t>(min_packet_limit_ms_ - elapsed_time_ms, 0);
}

int64_t PacedSender::UpdateTimeAndGetElapsedMs(int64_t now_us) {
  int64_t elapsed_time_ms = (now_us - time_last_process_us_ + 500) / 1000;
  time_last_process_us_ = now_us;
  if (elapsed_time_ms > kMaxElapsedTimeMs) {
    RTC_LOG(LS_WARNING) << "Elapsed time (" << elapsed_time_ms
                        << " ms) longer than expected, limiting to "
                        << kMaxElapsedTimeMs << " ms";
    elapsed_time_ms = kMaxElapsedTimeMs;
  }
  return elapsed_time_ms;
}

bool PacedSender::ShouldSendKeepalive(int64_t now_us) const {
  if (send_padding_if_silent_ || paused_ || Congested()) {
    // We send a padding packet every 500 ms to ensure we won't get stuck in
    // congested state due to no feedback being received.
    int64_t elapsed_since_last_send_us = now_us - last_send_time_us_;
    if (elapsed_since_last_send_us >= kCongestedPacketIntervalMs * 1000) {
      // We can not send padding unless a normal packet has first been sent. If
      // we do, timestamps get messed up.
      if (packet_counter_ > 0) {
        return true;
      }
    }
  }
  return false;
}

void PacedSender::Process() {
  rtc::CritScope cs(&critsect_);
  int64_t now_us = clock_->TimeInMicroseconds();
  int64_t elapsed_time_ms = UpdateTimeAndGetElapsedMs(now_us);
  if (ShouldSendKeepalive(now_us)) {
    critsect_.Leave();
    size_t bytes_sent = packet_sender_->TimeToSendPadding(1, PacedPacketInfo());
    critsect_.Enter();
    OnPaddingSent(bytes_sent);
    if (alr_detector_)
      alr_detector_->OnBytesSent(bytes_sent, now_us / 1000);
  }

  if (paused_)
    return;

  if (elapsed_time_ms > 0) {
    int target_bitrate_kbps = pacing_bitrate_kbps_;
    size_t queue_size_bytes = packets_.SizeInBytes();
    if (queue_size_bytes > 0) {
      // Assuming equal size packets and input/output rate, the average packet
      // has avg_time_left_ms left to get queue_size_bytes out of the queue, if
      // time constraint shall be met. Determine bitrate needed for that.
      packets_.UpdateQueueTime(TimeMilliseconds());
      if (drain_large_queues_) {
        int64_t avg_time_left_ms = std::max<int64_t>(
            1, queue_time_limit - packets_.AverageQueueTimeMs());
        int min_bitrate_needed_kbps =
            static_cast<int>(queue_size_bytes * 8 / avg_time_left_ms);
        if (min_bitrate_needed_kbps > target_bitrate_kbps) {
          target_bitrate_kbps = min_bitrate_needed_kbps;
          RTC_LOG(LS_VERBOSE) << "bwe:large_pacing_queue pacing_rate_kbps="
                              << target_bitrate_kbps;
        }
      }
    }

    media_budget_.set_target_rate_kbps(target_bitrate_kbps);
    UpdateBudgetWithElapsedTime(elapsed_time_ms);
  }

  bool is_probing = prober_.IsProbing();
  PacedPacketInfo pacing_info;
  size_t bytes_sent = 0;
  size_t recommended_probe_size = 0;
  if (is_probing) {
    pacing_info = prober_.CurrentCluster();
    recommended_probe_size = prober_.RecommendedMinProbeSize();
  }
  // The paused state is checked in the loop since it leaves the critical
  // section allowing the paused state to be changed from other code.
  while (!packets_.Empty() && !paused_) {
    auto* packet = GetPendingPacket(pacing_info);
    if (packet == nullptr)
      break;

    std::unique_ptr<RtpPacketToSend> rtp_packet = packet->ReleasePacket();
    const bool owned_rtp_packet = rtp_packet != nullptr;

    critsect_.Leave();

    RtpPacketSendResult success;
    if (rtp_packet != nullptr) {
      pacing_info.is_retransmission = packet->IsRetransmission();
      packet_sender_->SendPacedPacket(std::move(rtp_packet), pacing_info);
      success = RtpPacketSendResult::kSuccess;
    } else {
      success = packet_sender_->TimeToSendPacket(
          packet->Ssrc(), packet->SequenceNumber(), packet->CaptureTimeMs(),
          packet->IsRetransmission(), pacing_info);
    }

    critsect_.Enter();
    if (success == RtpPacketSendResult::kSuccess ||
        success == RtpPacketSendResult::kPacketNotFound) {
      // Packet sent or invalid packet, remove it from queue.
      // TODO(webrtc:8052): Don't consume media budget on kInvalid.
      bytes_sent += packet->SizeInBytes();
      // Send succeeded, remove it from the queue.
      OnPacketSent(packet);
      if (is_probing && bytes_sent > recommended_probe_size)
        break;
    } else if (owned_rtp_packet) {
      // Send failed, but we can't put it back in the queue, remove it without
      // consuming budget.
      packets_.FinalizePop();
      break;
    } else {
      // Send failed, put it back into the queue.
      packets_.CancelPop();
      break;
    }
  }

  if (packets_.Empty() && !Congested()) {
    // We can not send padding unless a normal packet has first been sent. If we
    // do, timestamps get messed up.
    if (packet_counter_ > 0) {
      int padding_needed =
          static_cast<int>(is_probing ? (recommended_probe_size - bytes_sent)
                                      : padding_budget_.bytes_remaining());
      if (padding_needed > 0) {
        critsect_.Leave();
        size_t padding_sent =
            packet_sender_->TimeToSendPadding(padding_needed, pacing_info);
        critsect_.Enter();
        bytes_sent += padding_sent;
        OnPaddingSent(padding_sent);
      }
    }
  }
  if (is_probing) {
    probing_send_failure_ = bytes_sent == 0;
    if (!probing_send_failure_)
      prober_.ProbeSent(TimeMilliseconds(), bytes_sent);
  }
  if (alr_detector_)
    alr_detector_->OnBytesSent(bytes_sent, now_us / 1000);
}

void PacedSender::ProcessThreadAttached(ProcessThread* process_thread) {
  RTC_LOG(LS_INFO) << "ProcessThreadAttached 0x" << process_thread;
  rtc::CritScope cs(&process_thread_lock_);
  process_thread_ = process_thread;
}

RoundRobinPacketQueue::PacketInfo* PacedSender::GetPendingPacket(
    const PacedPacketInfo& pacing_info) {
  // Since we need to release the lock in order to send, we first pop the
  // element from the priority queue but keep it in storage, so that we can
  // reinsert it if send fails.
  RoundRobinPacketQueue::PacketInfo* packet = packets_.BeginPop();
  bool audio_packet = packet->Priority() == kHighPriority;
  bool apply_pacing = !audio_packet || pace_audio_;
  if (apply_pacing && (Congested() || (media_budget_.bytes_remaining() == 0 &&
                                       pacing_info.probe_cluster_id ==
                                           PacedPacketInfo::kNotAProbe))) {
    packets_.CancelPop();
    return nullptr;
  }
  return packet;
}

void PacedSender::OnPacketSent(RoundRobinPacketQueue::PacketInfo* packet) {
  if (first_sent_packet_ms_ == -1)
    first_sent_packet_ms_ = TimeMilliseconds();
  bool audio_packet = packet->Priority() == kHighPriority;
  if (!audio_packet || account_for_audio_) {
    // Update media bytes sent.
    UpdateBudgetWithBytesSent(packet->SizeInBytes());
    last_send_time_us_ = clock_->TimeInMicroseconds();
  }
  // Send succeeded, remove it from the queue.
  packets_.FinalizePop();
}

void PacedSender::OnPaddingSent(size_t bytes_sent) {
  if (bytes_sent > 0) {
    UpdateBudgetWithBytesSent(bytes_sent);
  }
  last_send_time_us_ = clock_->TimeInMicroseconds();
}

void PacedSender::UpdateBudgetWithElapsedTime(int64_t delta_time_ms) {
  delta_time_ms = std::min(kMaxIntervalTimeMs, delta_time_ms);
  media_budget_.IncreaseBudget(delta_time_ms);
  padding_budget_.IncreaseBudget(delta_time_ms);
}

void PacedSender::UpdateBudgetWithBytesSent(size_t bytes_sent) {
  outstanding_bytes_ += bytes_sent;
  media_budget_.UseBudget(bytes_sent);
  padding_budget_.UseBudget(bytes_sent);
}

void PacedSender::SetPacingFactor(float pacing_factor) {
  rtc::CritScope cs(&critsect_);
  pacing_factor_ = pacing_factor;
  // Make sure new padding factor is applied immediately, otherwise we need to
  // wait for the send bitrate estimate to be updated before this takes effect.
  SetEstimatedBitrate(estimated_bitrate_bps_);
}

void PacedSender::SetQueueTimeLimit(int limit_ms) {
  rtc::CritScope cs(&critsect_);
  queue_time_limit = limit_ms;
}

}  // namespace webrtc
