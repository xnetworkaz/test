/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_PACING_PRIORITIZED_PACKET_QUEUE_H_
#define MODULES_PACING_PRIORITIZED_PACKET_QUEUE_H_

#include <stddef.h>

#include <deque>
#include <list>
#include <memory>
#include <unordered_map>

#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"

namespace webrtc {

class PrioritizedPacketQueue {
 public:
  explicit PrioritizedPacketQueue(Timestamp creation_time);
  PrioritizedPacketQueue(const PrioritizedPacketQueue&) = delete;
  PrioritizedPacketQueue& operator=(const PrioritizedPacketQueue&) = delete;

  // Add a packet to the queue. The enqueue time is used for queue time stats
  // and to report the leading packet enqueue time per packet type.
  void Push(Timestamp enqueue_time, std::unique_ptr<RtpPacketToSend> packet);

  // Remove the next packet from the queue. Packets a prioritized first
  // according to packet type, in the following order:
  // - audio, retransmissions, video / fec, padding
  // For each packet type, we use one FIFO-queue per SSRC and emit from
  // those queues in a round-robin fashion.
  std::unique_ptr<RtpPacketToSend> Pop();

  // Number of packets in the queue.
  int SizeInPackets() const;

  // Sum of all payload bytes in the queue, where the payload is calculated
  // as `packet->payload_size() + packet->padding_size()`.
  DataSize SizeInPayloadBytes() const;

  // Convenience method for `SizeInPackets() == 0`.
  bool Empty() const;

  // Total packets in the queue per media type (RtpPacketMediaType values are
  // used as lookup index).
  const std::array<int, kNumMediaTypes>& SizeInPacketsPerRtpPacketMediaType()
      const;

  // The enqueue time of the next packet this queue will return via the Pop()
  // method, for the given packet type. If queue has no packets, of that type,
  // returns Timestamp::MinusInfinity().
  Timestamp LeadingPacketEnqueueTime(RtpPacketMediaType type) const;

  // Enqueue time of the oldest packet in the queue,
  // Timestamp::MinusInfinity() if queue is empty.
  Timestamp OldestEnqueueTime() const;

  // Average queue time for the packets currently in the queue.
  // The queuing time is calculated from Push() to the last UpdateQueueTime()
  // call - with any time spent in a paused state subtracted.
  // Returns TimeDelta::Zero() for an empty queue.
  TimeDelta AverageQueueTime() const;

  // Called during packet processing or when pause stats changes. Since the
  // AverageQueueTime() method does not look at the wall time, this method
  // needs to be called before querying queue time.
  void UpdateAverageQueueTime(Timestamp now);

  // Set the pause state, while `paused` is true queuing time is not counted.
  void SetPauseState(bool paused, Timestamp now);

  // Checks if the queue for the given SSRC has original (retransmissions not
  // counted) video packets containing keyframe data.
  bool HasKeyframePackets(uint32_t ssrc) const;

  // Remove any pending media and retransmissions for the given stream.
  void FlushVideoStream(uint32_t media_ssrc, absl::optional<uint32_t> rtx_ssrx);

 private:
  static constexpr int kNumPriorityLevels = 4;

  class QueuedPacket {
   public:
    DataSize PacketSize() const;

    std::unique_ptr<RtpPacketToSend> packet;
    Timestamp enqueue_time;
    std::list<Timestamp>::iterator enqueue_time_iterator;
  };

  // Class containing packets for an RTP stream.
  // For each priority level, packets are simply stored in a fifo queue.
  class StreamQueue {
   public:
    explicit StreamQueue(Timestamp creation_time);
    StreamQueue(StreamQueue&&) = default;
    StreamQueue& operator=(StreamQueue&&) = default;

    StreamQueue(const StreamQueue&) = delete;
    StreamQueue& operator=(const StreamQueue&) = delete;

    // Enqueue packet at the given priority level. Returns true if the packet
    // count for that priority level went from zero to non-zero.
    bool EnqueuePacket(QueuedPacket packet, int priority_level);

    QueuedPacket DequePacket(int priority_level);

    bool HasPacketsAtPrio(int priority_level) const;
    bool IsEmpty() const;
    Timestamp LeadingPacketEnqueueTime(int priority_level) const;
    Timestamp last_enqueue_time() const { return last_enqueue_time_; }
    bool has_keyframe_packets() const { return keyframe_packets_ > 0; }

   private:
    std::deque<QueuedPacket> packets_[kNumPriorityLevels];
    Timestamp last_enqueue_time_;
    int keyframe_packets_;
  };

  // Remove packet from any stats it's included in.
  void OnRemovedPacket(const QueuedPacket& packet);

  // Remove all packets for the given prio level and SSRC.
  void FlushStream(uint32_t ssrc, int prio_level);

  // Cumulative sum, over all packets, of time spent in the queue.
  TimeDelta queue_time_sum_;
  // Cumulative sum of time the queue has spent in a paused state.
  TimeDelta pause_time_sum_;
  // Total number of packets stored in this queue.
  int size_packets_;
  // Total number of packets stored in this queue per RtpPacketMediaType.
  std::array<int, kNumMediaTypes> size_packets_per_media_type_;
  // Sum of payload sizes for all packts stored in this queue.
  DataSize size_payload_;
  // The last time queue/pause time sums were updated.
  Timestamp last_update_time_;
  bool paused_;

  // Last time `streams_` was culled for inactive streams.
  Timestamp last_culling_time_;

  // Map from SSRC to packet queues for the associated RTP stream.
  std::unordered_map<uint32_t, std::unique_ptr<StreamQueue>> streams_;

  // For each priority level, a queue of StreamQueues which have at least one
  // packet pending for that prio level.
  std::deque<StreamQueue*> streams_by_prio_[kNumPriorityLevels];

  // The first index into `stream_by_prio_` that is non-empty.
  int top_active_prio_level_;

  // Ordered list of enqueue times. Additions are always increasing and added to
  // the end. QueuedPacket instances have a iterators into this list for fast
  // removal.
  std::list<Timestamp> enqueue_times_;
};

}  // namespace webrtc

#endif  // MODULES_PACING_PRIORITIZED_PACKET_QUEUE_H_
