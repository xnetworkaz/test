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
#include <vector>

#include "absl/memory/memory.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "modules/utility/include/process_thread.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {
const int64_t PacedSender::kMaxQueueLengthMs = 2000;
const float PacedSender::kDefaultPaceMultiplier = 2.5f;

PacedSender::PacedSender(Clock* clock,
                         PacketRouter* packet_router,
                         RtcEventLog* event_log,
                         const WebRtcKeyValueConfig* field_trials)
    : pacing_controller_(clock,
                         static_cast<PacingController::PacketSender*>(this),
                         event_log,
                         field_trials),
      packet_router_(packet_router),
      process_thread_(nullptr) {}

PacedSender::~PacedSender() {
  RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
}

void PacedSender::CreateProbeCluster(DataRate bitrate, int cluster_id) {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.CreateProbeCluster(bitrate, cluster_id);
}

void PacedSender::Pause() {
  // Called on a TQ from within
  // RtpTransportControllerSend::OnNetworkAvailability.
  // RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  {
    rtc::CritScope cs(&process_thread_lock_);
    RTC_DCHECK(process_thread_);  // Must have been set first.
  }

  {
    rtc::CritScope cs(&critsect_);
    pacing_controller_.Pause();
  }
  rtc::CritScope cs(&process_thread_lock_);
  // Tell the process thread to call our TimeUntilNextProcess() method to get
  // a new (longer) estimate for when to call Process().
  if (process_thread_)
    process_thread_->WakeUp(this);
}

void PacedSender::Resume() {
  // Called on a TQ from within
  // RtpTransportControllerSend::OnNetworkAvailability.
  // RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  {
    // TODO(tommi): Apparently this isn't always true. See test
    // ReferencingAndOwningPackets/PacedSenderTest.Pause/0
    // It could be that that test never sets a process thread though.
    // Consider requiring to set the process thread in the ctor and
    // make the process_thread_ variable const, no lock required.
    rtc::CritScope cs(&process_thread_lock_);
    RTC_DCHECK(process_thread_);  // Must have been set first.
  }

  {
    rtc::CritScope cs(&critsect_);
    pacing_controller_.Resume();
  }
  rtc::CritScope cs(&process_thread_lock_);
  // Tell the process thread to call our TimeUntilNextProcess() method to
  // refresh the estimate for when to call Process().
  if (process_thread_)
    process_thread_->WakeUp(this);
}

void PacedSender::SetCongestionWindow(DataSize congestion_window_size) {
  RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  rtc::CritScope cs(&critsect_);
  pacing_controller_.SetCongestionWindow(congestion_window_size);
}

void PacedSender::UpdateOutstandingData(DataSize outstanding_data) {
  // Called on a TQ from within
  // RtpTransportControllerSend::OnNetworkAvailability.
  // RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  rtc::CritScope cs(&critsect_);
  pacing_controller_.UpdateOutstandingData(outstanding_data);
}

void PacedSender::SetProbingEnabled(bool enabled) {
  RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  rtc::CritScope cs(&critsect_);
  pacing_controller_.SetProbingEnabled(enabled);
}

void PacedSender::SetPacingRates(DataRate pacing_rate, DataRate padding_rate) {
  // Called on a TQ from within
  // RtpTransportControllerSend::OnNetworkAvailability.
  // RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  rtc::CritScope cs(&critsect_);
  pacing_controller_.SetPacingRates(pacing_rate, padding_rate);
}

void PacedSender::InsertPacket(RtpPacketSender::Priority priority,
                               uint32_t ssrc,
                               uint16_t sequence_number,
                               int64_t capture_time_ms,
                               size_t bytes,
                               bool retransmission) {
  RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  rtc::CritScope cs(&critsect_);
  pacing_controller_.InsertPacket(priority, ssrc, sequence_number,
                                  capture_time_ms, bytes, retransmission);
}

void PacedSender::EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet) {
  rtc::CritScope cs(&critsect_);
  pacing_controller_.EnqueuePacket(std::move(packet));
}

void PacedSender::SetAccountForAudioPackets(bool account_for_audio) {
  rtc::CritScope cs(&critsect_);
  pacing_controller_.SetAccountForAudioPackets(account_for_audio);
}

TimeDelta PacedSender::ExpectedQueueTime() const {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.ExpectedQueueTime();
}

size_t PacedSender::QueueSizePackets() const {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.QueueSizePackets();
}

DataSize PacedSender::QueueSizeData() const {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.QueueSizeData();
}

absl::optional<Timestamp> PacedSender::FirstSentPacketTime() const {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.FirstSentPacketTime();
}

TimeDelta PacedSender::OldestPacketWaitTime() const {
  rtc::CritScope cs(&critsect_);
  return pacing_controller_.OldestPacketWaitTime();
}

int64_t PacedSender::TimeUntilNextProcess() {
  rtc::CritScope cs(&critsect_);

  // When paused we wake up every 500 ms to send a padding packet to ensure
  // we won't get stuck in the paused state due to no feedback being received.
  TimeDelta elapsed_time = pacing_controller_.TimeElapsedSinceLastProcess();
  if (pacing_controller_.IsPaused()) {
    return std::max(PacingController::kPausedProcessInterval - elapsed_time,
                    TimeDelta::Zero())
        .ms();
  }

  auto next_probe = pacing_controller_.TimeUntilNextProbe();
  if (next_probe) {
    return next_probe->ms();
  }

  const TimeDelta min_packet_limit = TimeDelta::ms(5);
  return std::max(min_packet_limit - elapsed_time, TimeDelta::Zero()).ms();
}

void PacedSender::Process() {
  rtc::CritScope cs(&critsect_);
  pacing_controller_.ProcessPackets();
}

void PacedSender::ProcessThreadAttached(ProcessThread* process_thread) {
  RTC_DCHECK_RUN_ON(&construction_sequence_checker_);
  RTC_LOG(LS_INFO) << "ProcessThreadAttached 0x" << process_thread;
  rtc::CritScope cs(&process_thread_lock_);
  process_thread_ = process_thread;
}

void PacedSender::SetQueueTimeLimit(TimeDelta limit) {
  rtc::CritScope cs(&critsect_);
  pacing_controller_.SetQueueTimeLimit(limit);
}

void PacedSender::SendRtpPacket(std::unique_ptr<RtpPacketToSend> packet,
                                const PacedPacketInfo& cluster_info) {
  critsect_.Leave();
  packet_router_->SendPacket(std::move(packet), cluster_info);
  critsect_.Enter();
}

std::vector<std::unique_ptr<RtpPacketToSend>> PacedSender::GeneratePadding(
    DataSize size) {
  std::vector<std::unique_ptr<RtpPacketToSend>> padding_packets;
  critsect_.Leave();
  padding_packets = packet_router_->GeneratePadding(size.bytes());
  critsect_.Enter();
  return padding_packets;
}

RtpPacketSendResult PacedSender::TimeToSendPacket(
    uint32_t ssrc,
    uint16_t sequence_number,
    int64_t capture_timestamp,
    bool retransmission,
    const PacedPacketInfo& packet_info) {
  RtpPacketSendResult result;
  critsect_.Leave();
  result = packet_router_->TimeToSendPacket(
      ssrc, sequence_number, capture_timestamp, retransmission, packet_info);
  critsect_.Enter();
  return result;
}

DataSize PacedSender::TimeToSendPadding(DataSize size,
                                        const PacedPacketInfo& pacing_info) {
  size_t padding_bytes_sent;
  critsect_.Leave();
  padding_bytes_sent =
      packet_router_->TimeToSendPadding(size.bytes(), pacing_info);
  critsect_.Enter();
  return DataSize::bytes(padding_bytes_sent);
}

}  // namespace webrtc
