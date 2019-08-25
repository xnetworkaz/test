/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_
#define MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>

#include "absl/types/optional.h"
#include "api/transport/webrtc_key_value_config.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

class RoundRobinPacketQueue {
 public:
  RoundRobinPacketQueue(Timestamp start_time,
                        const WebRtcKeyValueConfig* field_trials);
  ~RoundRobinPacketQueue();

  void Push(int priority,
            Timestamp enqueue_time,
            uint64_t enqueue_order,
            DataSize size,
            std::unique_ptr<RtpPacketToSend> packet);
  // Tries to pop the next packet. If not packet is available, then nullptr is
  // returned. If at least one packet is available, the criteria() function will
  // be called to validate is the highest priority packet can be popped. If the
  // function returnes false, a nullptr will be returned by TryPop(). If the
  // criteria function returns true, than instance will be moved out of the
  // queue and returned by the TryPop() call.
  std::unique_ptr<RtpPacketToSend> TryPop(
      rtc::FunctionView<bool(const RtpPacketToSend&)> criteria);

  bool Empty() const;
  size_t SizeInPackets() const;
  DataSize Size() const;

  Timestamp OldestEnqueueTime() const;
  TimeDelta AverageQueueTime() const;
  void UpdateQueueTime(Timestamp now);
  void SetPauseState(bool paused, Timestamp now);

 private:
  struct QueuedPacket {
   public:
    QueuedPacket(
        int priority,
        Timestamp enqueue_time,
        DataSize size,
        bool retransmission,
        uint64_t enqueue_order,
        std::multiset<Timestamp>::iterator enqueue_time_it,
        absl::optional<std::list<std::unique_ptr<RtpPacketToSend>>::iterator>
            packet_it);
    QueuedPacket(const QueuedPacket& rhs);
    ~QueuedPacket();

    bool operator<(const QueuedPacket& other) const;

    int priority() const { return priority_; }
    Timestamp enqueue_time() const { return enqueue_time_; }
    DataSize size() const { return size_; }
    bool is_retransmission() const { return retransmission_; }
    uint64_t enqueue_order() const { return enqueue_order_; }
    std::unique_ptr<RtpPacketToSend> ReleasePacket();

    // For internal use.
    absl::optional<std::list<std::unique_ptr<RtpPacketToSend>>::iterator>
    PacketIterator() const {
      return packet_it_;
    }
    std::multiset<Timestamp>::iterator EnqueueTimeIterator() const {
      return enqueue_time_it_;
    }
    void SubtractPauseTime(TimeDelta pause_time_sum);

    const RtpPacketToSend& get_packet() const {
      RTC_DCHECK(packet_it_.has_value());
      RTC_DCHECK((*packet_it_)->get() != nullptr);
      return ***packet_it_;
    }

    int priority_;
    Timestamp enqueue_time_;  // Absolute time of pacer queue entry.
    DataSize size_;
    bool retransmission_;
    uint64_t enqueue_order_;
    std::multiset<Timestamp>::iterator enqueue_time_it_;
    // Iterator into |rtp_packets_| where the memory for RtpPacket is owned,
    // if applicable.
    absl::optional<std::list<std::unique_ptr<RtpPacketToSend>>::iterator>
        packet_it_;
  };

  struct StreamPrioKey {
    StreamPrioKey(int priority, DataSize size)
        : priority(priority), size(size) {}

    bool operator<(const StreamPrioKey& other) const {
      if (priority != other.priority)
        return priority < other.priority;
      return size < other.size;
    }

    const int priority;
    const DataSize size;
  };

  struct Stream {
    Stream();
    Stream(const Stream&);

    virtual ~Stream();

    DataSize size;
    uint32_t ssrc;
    std::priority_queue<QueuedPacket> packet_queue;

    // Whenever a packet is inserted for this stream we check if |priority_it|
    // points to an element in |stream_priorities_|, and if it does it means
    // this stream has already been scheduled, and if the scheduled priority is
    // lower than the priority of the incoming packet we reschedule this stream
    // with the higher priority.
    std::multimap<StreamPrioKey, uint32_t>::iterator priority_it;
  };

  void Push(QueuedPacket packet, uint32_t ssrc);

  Stream* GetHighestPriorityStream();

  // Just used to verify correctness.
  bool IsSsrcScheduled(uint32_t ssrc) const;

  Timestamp time_last_updated_;

  bool paused_;
  size_t size_packets_;
  DataSize size_;
  DataSize max_size_;
  TimeDelta queue_time_sum_;
  TimeDelta pause_time_sum_;

  // A map of streams used to prioritize from which stream to send next. We use
  // a multimap instead of a priority_queue since the priority of a stream can
  // change as a new packet is inserted, and a multimap allows us to remove and
  // then reinsert a StreamPrioKey if the priority has increased.
  std::multimap<StreamPrioKey, uint32_t> stream_priorities_;

  // A map of SSRCs to Streams.
  std::map<uint32_t, Stream> streams_;

  // The enqueue time of every packet currently in the queue. Used to figure out
  // the age of the oldest packet in the queue.
  std::multiset<Timestamp> enqueue_times_;

  // List of RTP packets to be sent, not necessarily in the order they will be
  // sent. PacketInfo.packet_it will point to an entry in this list, or the
  // end iterator of this list if queue does not have direct ownership of the
  // packet.
  std::list<std::unique_ptr<RtpPacketToSend>> rtp_packets_;
};
}  // namespace webrtc

#endif  // MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_
