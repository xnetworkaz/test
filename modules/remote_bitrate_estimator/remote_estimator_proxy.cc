/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/remote_bitrate_estimator/remote_estimator_proxy.h"

#include <algorithm>
#include <limits>

#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_minmax.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

// TODO(sprang): Tune these!
const int RemoteEstimatorProxy::kBackWindowMs = 500;
const int RemoteEstimatorProxy::kMinSendIntervalMs = 50;
const int RemoteEstimatorProxy::kMaxSendIntervalMs = 250;
const int RemoteEstimatorProxy::kDefaultSendIntervalMs = 100;

// The maximum allowed value for a timestamp in milliseconds. This is lower
// than the numerical limit since we often convert to microseconds.
static constexpr int64_t kMaxTimeMs =
    std::numeric_limits<int64_t>::max() / 1000;

RemoteEstimatorProxy::RemoteEstimatorProxy(
    Clock* clock,
    TransportFeedbackSenderInterface* feedback_sender)
    : clock_(clock),
      feedback_sender_(feedback_sender),
      last_process_time_ms_(-1),
      media_ssrc_(0),
      feedback_sequence_(0),
      window_start_seq_(-1),
      send_interval_ms_(kDefaultSendIntervalMs),
      send_feedback_on_request_only_(false) {}

RemoteEstimatorProxy::~RemoteEstimatorProxy() {}

void RemoteEstimatorProxy::IncomingPacket(int64_t arrival_time_ms,
                                          size_t payload_size,
                                          const RTPHeader& header) {
  if (!header.extension.hasTransportSequenceNumber) {
    RTC_LOG(LS_WARNING)
        << "RemoteEstimatorProxy: Incoming packet "
           "is missing the transport sequence number extension!";
    return;
  }
  rtc::CritScope cs(&lock_);
  media_ssrc_ = header.ssrc;
  OnPacketArrival(header.extension.transportSequenceNumber, arrival_time_ms,
                  header.extension.feedback_request);
}

bool RemoteEstimatorProxy::LatestEstimate(std::vector<unsigned int>* ssrcs,
                                          unsigned int* bitrate_bps) const {
  return false;
}

int64_t RemoteEstimatorProxy::TimeUntilNextProcess() {
  int64_t time_until_next = 0;
  rtc::CritScope cs(&lock_);
  if (send_feedback_on_request_only_) {
    // Wait a day until next process.
    time_until_next = 24 * 60 * 60 * 1000;
  } else if (last_process_time_ms_ != -1) {
    int64_t now = clock_->TimeInMilliseconds();
    if (now - last_process_time_ms_ < send_interval_ms_)
      time_until_next = (last_process_time_ms_ + send_interval_ms_ - now);
  }
  return time_until_next;
}

void RemoteEstimatorProxy::Process() {
  rtc::CritScope cs(&lock_);
  if (send_feedback_on_request_only_) {
    return;
  }
  last_process_time_ms_ = clock_->TimeInMilliseconds();

  bool more_to_build = true;
  while (more_to_build) {
    rtcp::TransportFeedback feedback_packet;
    if (BuildFeedbackPacket(&feedback_packet)) {
      RTC_DCHECK(feedback_sender_ != nullptr);
      feedback_sender_->SendTransportFeedback(&feedback_packet);
    } else {
      more_to_build = false;
    }
  }
}

void RemoteEstimatorProxy::OnBitrateChanged(int bitrate_bps) {
  // TwccReportSize = Ipv4(20B) + UDP(8B) + SRTP(10B) +
  // AverageTwccReport(30B)
  // TwccReport size at 50ms interval is 24 byte.
  // TwccReport size at 250ms interval is 36 byte.
  // AverageTwccReport = (TwccReport(50ms) + TwccReport(250ms)) / 2
  constexpr int kTwccReportSize = 20 + 8 + 10 + 30;
  constexpr double kMinTwccRate =
      kTwccReportSize * 8.0 * 1000.0 / kMaxSendIntervalMs;
  constexpr double kMaxTwccRate =
      kTwccReportSize * 8.0 * 1000.0 / kMinSendIntervalMs;

  // Let TWCC reports occupy 5% of total bandwidth.
  rtc::CritScope cs(&lock_);
  send_interval_ms_ = static_cast<int>(
      0.5 + kTwccReportSize * 8.0 * 1000.0 /
                rtc::SafeClamp(0.05 * bitrate_bps, kMinTwccRate, kMaxTwccRate));
}

void RemoteEstimatorProxy::SetSendFeedbackOnRequestOnly(
    bool send_feedback_on_request_only) {
  rtc::CritScope cs(&lock_);
  send_feedback_on_request_only_ = send_feedback_on_request_only;
}

void RemoteEstimatorProxy::OnPacketArrival(
    uint16_t sequence_number,
    int64_t arrival_time,
    absl::optional<FeedbackRequest> feedback_request) {
  if (arrival_time < 0 || arrival_time > kMaxTimeMs) {
    RTC_LOG(LS_WARNING) << "Arrival time out of bounds: " << arrival_time;
    return;
  }

  // TODO(holmer): We should handle a backwards wrap here if the first
  // sequence number was small and the new sequence number is large. The
  // SequenceNumberUnwrapper doesn't do this, so we should replace this with
  // calls to IsNewerSequenceNumber instead.
  int64_t seq = unwrapper_.Unwrap(sequence_number);
  if (window_start_seq_ != -1 && seq > window_start_seq_ + 0xFFFF / 2) {
    RTC_LOG(LS_WARNING) << "Skipping this sequence number (" << sequence_number
                        << ") since it likely is reordered, but the unwrapper"
                           "failed to handle it. Feedback window starts at "
                        << window_start_seq_ << ".";
    return;
  }

  if (!send_feedback_on_request_only_) {
    if (packet_arrival_times_.lower_bound(window_start_seq_) ==
        packet_arrival_times_.end()) {
      // Start new feedback packet, cull old packets.
      for (auto it = packet_arrival_times_.begin();
           it != packet_arrival_times_.end() && it->first < seq &&
           arrival_time - it->second >= kBackWindowMs;) {
        auto delete_it = it;
        ++it;
        packet_arrival_times_.erase(delete_it);
      }
    }
  }

  if (window_start_seq_ == -1) {
    window_start_seq_ = sequence_number;
  } else if (seq < window_start_seq_) {
    window_start_seq_ = seq;
  }

  // We are only interested in the first time a packet is received.
  if (packet_arrival_times_.find(seq) != packet_arrival_times_.end())
    return;

  packet_arrival_times_[seq] = arrival_time;

  if (feedback_request) {
    // Send feedback packet immediately.
    SendFeedbackOnRequest(seq, *feedback_request);
  }
}

bool RemoteEstimatorProxy::BuildFeedbackPacket(
    rtcp::TransportFeedback* feedback_packet) {
  rtc::CritScope cs(&lock_);
  // |window_start_seq_| is the first sequence number to include in the current
  // feedback packet. Some older may still be in the map, in case a reordering
  // happens and we need to retransmit them.
  auto first_iterator = packet_arrival_times_.lower_bound(window_start_seq_);
  auto last_iterator = packet_arrival_times_.cend();
  return BuildFeedbackPacket(first_iterator, last_iterator, window_start_seq_,
                             /*include_timestamps=*/true, feedback_packet);
}

bool RemoteEstimatorProxy::BuildFeedbackPacket(
    std::map<int64_t, int64_t>::const_iterator begin_iterator,
    std::map<int64_t, int64_t>::const_iterator end_iterator,
    int64_t base_sequence_number,
    bool include_timestamps,
    rtcp::TransportFeedback* feedback_packet) {
  if (begin_iterator == packet_arrival_times_.cend()) {
    // Feedback for all packets already sent.
    return false;
  }

  // TODO(sprang): Measure receive times in microseconds and remove the
  // conversions below.
  feedback_packet->SetMediaSsrc(media_ssrc_);
  // Base sequence is the expected next (window_start_seq_). This is known, but
  // we might not have actually received it, so the base time shall be the time
  // of the first received packet in the feedback.
  feedback_packet->SetBase(static_cast<uint16_t>(base_sequence_number & 0xFFFF),
                           begin_iterator->second * 1000);
  feedback_packet->SetFeedbackSequenceNumber(feedback_sequence_++);
  feedback_packet->SetIncludeTimestamps(include_timestamps);
  for (auto it = begin_iterator; it != end_iterator; ++it) {
    if (!feedback_packet->AddReceivedPacket(
            static_cast<uint16_t>(it->first & 0xFFFF), it->second * 1000)) {
      // If we can't even add the first seq to the feedback packet, we won't be
      // able to build it at all.
      RTC_CHECK(begin_iterator != it);

      // Could not add timestamp, feedback packet might be full. Return and
      // try again with a fresh packet.
      break;
    }

    // Note: Don't erase items from packet_arrival_times_ after sending, in case
    // they need to be re-sent after a reordering. Removal will be handled
    // by OnPacketArrival once packets are too old.
    window_start_seq_ = it->first + 1;
  }

  return true;
}

void RemoteEstimatorProxy::SendFeedbackOnRequest(
    int64_t sequence_number,
    const FeedbackRequest& feedback_request) {
  rtcp::TransportFeedback feedback_packet;

  int64_t first_sequence_number =
      sequence_number - feedback_request.sequence_count;
  auto begin_iterator =
      packet_arrival_times_.lower_bound(first_sequence_number);
  auto end_iterator = packet_arrival_times_.upper_bound(sequence_number);

  BuildFeedbackPacket(begin_iterator, end_iterator, first_sequence_number,
                      feedback_request.include_timestamps, &feedback_packet);

  // Clear up to the first packet that is included in this feedback packet.
  packet_arrival_times_.erase(packet_arrival_times_.begin(), begin_iterator);

  RTC_DCHECK(feedback_sender_ != nullptr);
  feedback_sender_->SendTransportFeedback(&feedback_packet);
}

}  // namespace webrtc
