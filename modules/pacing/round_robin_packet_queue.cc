/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/pacing/round_robin_packet_queue.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "rtc_base/checks.h"

namespace webrtc {

RoundRobinPacketQueue::Packet::Packet(RtpPacketSender::Priority priority,
                                      uint32_t ssrc,
                                      uint16_t seq_number,
                                      int64_t capture_time_ms,
                                      int64_t enqueue_time_ms,
                                      size_t length_in_bytes,
                                      bool retransmission,
                                      uint64_t enqueue_order)
    : priority(priority),
      ssrc(ssrc),
      sequence_number(seq_number),
      capture_time_ms(capture_time_ms),
      enqueue_time_ms(enqueue_time_ms),
      sum_paused_ms(0),
      bytes(length_in_bytes),
      retransmission(retransmission),
      enqueue_order(enqueue_order) {}

RoundRobinPacketQueue::Packet::Packet(const Packet& other) = default;

RoundRobinPacketQueue::Packet::~Packet() {}

bool RoundRobinPacketQueue::Packet::operator<(
    const RoundRobinPacketQueue::Packet& other) const {
  if (priority != other.priority)
    return priority > other.priority;
  if (retransmission != other.retransmission)
    return other.retransmission;

  return enqueue_order > other.enqueue_order;
}

RoundRobinPacketQueue::Stream::Stream() : bytes(0), ssrc(0) {}
RoundRobinPacketQueue::Stream::Stream(const Stream& stream) = default;
RoundRobinPacketQueue::Stream::~Stream() {}

RoundRobinPacketQueue::RoundRobinPacketQueue(int64_t start_time_us)
    : time_last_updated_ms_(start_time_us / 1000) {}

RoundRobinPacketQueue::~RoundRobinPacketQueue() {}

void RoundRobinPacketQueue::Push(const Packet& packet_to_insert) {
  Packet packet(packet_to_insert);

  auto stream_info_it = streams_.find(packet.ssrc);
  if (stream_info_it == streams_.end()) {
    stream_info_it = streams_.emplace(packet.ssrc, Stream()).first;
    stream_info_it->second.priority_it = stream_priorities_.end();
    stream_info_it->second.ssrc = packet.ssrc;
  }

  Stream* streams = &stream_info_it->second;

  if (streams->priority_it == stream_priorities_.end()) {
    // If the SSRC is not currently scheduled, add it to |stream_priorities_|.
    RTC_CHECK(!IsSsrcScheduled(streams->ssrc));
    streams->priority_it = stream_priorities_.emplace(
        StreamPrioKey(packet.priority, streams->bytes), packet.ssrc);
  } else if (packet.priority < streams->priority_it->first.priority) {
    // If the priority of this SSRC increased, remove the outdated StreamPrioKey
    // and insert a new one with the new priority. Note that
    // RtpPacketSender::Priority uses lower ordinal for higher priority.
    stream_priorities_.erase(streams->priority_it);
    streams->priority_it = stream_priorities_.emplace(
        StreamPrioKey(packet.priority, streams->bytes), packet.ssrc);
  }
  RTC_CHECK(streams->priority_it != stream_priorities_.end());

  packet.enqueue_time_it = enqueue_times_.insert(packet.enqueue_time_ms);

  // In order to figure out how much time a packet has spent in the queue while
  // not in a paused state, we subtract the total amount of time the queue has
  // been paused so far, and when the packet is poped we subtract the total
  // amount of time the queue has been paused at that moment. This way we
  // subtract the total amount of time the packet has spent in the queue while
  // in a paused state.
  UpdateQueueTime(packet.enqueue_time_ms);
  packet.enqueue_time_ms -= pause_time_sum_ms_;
  streams->packet_queue.push(packet);

  size_packets_ += 1;
  size_bytes_ += packet.bytes;
}

const RoundRobinPacketQueue::Packet& RoundRobinPacketQueue::BeginPop() {
  RTC_CHECK(!pop_packet_ && !pop_stream_);

  Stream* stream = GetHighestPriorityStream();
  pop_stream_.emplace(stream);
  pop_packet_.emplace(stream->packet_queue.top());
  stream->packet_queue.pop();

  return *pop_packet_;
}

void RoundRobinPacketQueue::CancelPop(const Packet& packet) {
  RTC_CHECK(pop_packet_ && pop_stream_);
  (*pop_stream_)->packet_queue.push(*pop_packet_);
  pop_packet_.reset();
  pop_stream_.reset();
}

void RoundRobinPacketQueue::FinalizePop(const Packet& packet) {
  if (!Empty()) {
    RTC_CHECK(pop_packet_ && pop_stream_);
    Stream* stream = *pop_stream_;
    stream_priorities_.erase(stream->priority_it);
    const Packet& packet = *pop_packet_;

    // Calculate the total amount of time spent by this packet in the queue
    // while in a non-paused state. Note that the |pause_time_sum_ms_| was
    // subtracted from |packet.enqueue_time_ms| when the packet was pushed, and
    // by subtracting it now we effectively remove the time spent in in the
    // queue while in a paused state.
    int64_t time_in_non_paused_state_ms =
        time_last_updated_ms_ - packet.enqueue_time_ms - pause_time_sum_ms_;
    queue_time_sum_ms_ -= time_in_non_paused_state_ms;

    RTC_CHECK(packet.enqueue_time_it != enqueue_times_.end());
    enqueue_times_.erase(packet.enqueue_time_it);

    // Update |bytes| of this stream. The general idea is that the stream that
    // has sent the least amount of bytes should have the highest priority.
    // The problem with that is if streams send with different rates, in which
    // case a "budget" will be built up for the stream sending at the lower
    // rate. To avoid building a too large budget we limit |bytes| to be within
    // kMaxLeading bytes of the stream that has sent the most amount of bytes.
    stream->bytes =
        std::max(stream->bytes + packet.bytes, max_bytes_ - kMaxLeadingBytes);
    max_bytes_ = std::max(max_bytes_, stream->bytes);

    size_bytes_ -= packet.bytes;
    size_packets_ -= 1;
    RTC_CHECK(size_packets_ > 0 || queue_time_sum_ms_ == 0);

    // If there are packets left to be sent, schedule the stream again.
    RTC_CHECK(!IsSsrcScheduled(stream->ssrc));
    if (stream->packet_queue.empty()) {
      stream->priority_it = stream_priorities_.end();
    } else {
      RtpPacketSender::Priority priority = stream->packet_queue.top().priority;
      stream->priority_it = stream_priorities_.emplace(
          StreamPrioKey(priority, stream->bytes), stream->ssrc);
    }

    pop_packet_.reset();
    pop_stream_.reset();
  }
}

bool RoundRobinPacketQueue::Empty() const {
  RTC_CHECK((!stream_priorities_.empty() && size_packets_ > 0) ||
            (stream_priorities_.empty() && size_packets_ == 0));
  return stream_priorities_.empty();
}

size_t RoundRobinPacketQueue::SizeInPackets() const {
  return size_packets_;
}

uint64_t RoundRobinPacketQueue::SizeInBytes() const {
  return size_bytes_;
}

int64_t RoundRobinPacketQueue::OldestEnqueueTimeMs() const {
  if (Empty())
    return 0;
  RTC_CHECK(!enqueue_times_.empty());
  return *enqueue_times_.begin();
}

void RoundRobinPacketQueue::UpdateQueueTime(int64_t timestamp_ms) {
  RTC_CHECK_GE(timestamp_ms, time_last_updated_ms_);
  if (timestamp_ms == time_last_updated_ms_)
    return;

  int64_t delta_ms = timestamp_ms - time_last_updated_ms_;

  if (paused_) {
    pause_time_sum_ms_ += delta_ms;
  } else {
    queue_time_sum_ms_ += delta_ms * size_packets_;
  }

  time_last_updated_ms_ = timestamp_ms;
}

void RoundRobinPacketQueue::SetPauseState(bool paused, int64_t timestamp_ms) {
  if (paused_ == paused)
    return;
  UpdateQueueTime(timestamp_ms);
  paused_ = paused;
}

int64_t RoundRobinPacketQueue::AverageQueueTimeMs() const {
  if (Empty())
    return 0;
  return queue_time_sum_ms_ / size_packets_;
}

RoundRobinPacketQueue::Stream*
RoundRobinPacketQueue::GetHighestPriorityStream() {
  RTC_CHECK(!stream_priorities_.empty());
  uint32_t ssrc = stream_priorities_.begin()->second;

  auto stream_info_it = streams_.find(ssrc);
  RTC_CHECK(stream_info_it != streams_.end());
  RTC_CHECK(stream_info_it->second.priority_it == stream_priorities_.begin());
  RTC_CHECK(!stream_info_it->second.packet_queue.empty());
  return &stream_info_it->second;
}

bool RoundRobinPacketQueue::IsSsrcScheduled(uint32_t ssrc) const {
  for (const auto& scheduled_stream : stream_priorities_) {
    if (scheduled_stream.second == ssrc)
      return true;
  }
  return false;
}

}  // namespace webrtc
