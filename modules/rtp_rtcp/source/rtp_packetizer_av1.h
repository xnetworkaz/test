/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_AV1_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_AV1_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "api/array_view.h"
#include "modules/rtp_rtcp/source/rtp_format.h"

namespace webrtc {

class RtpPacketizerAv1 : public RtpPacketizer {
 public:
  RtpPacketizerAv1(rtc::ArrayView<const uint8_t> payload,
                   PayloadSizeLimits limits);
  ~RtpPacketizerAv1() override = default;

  size_t NumPackets() const override { return packets_.size() - packet_index_; }
  bool NextPacket(RtpPacketToSend* packet) override;

 private:
  struct Obu {
    uint8_t header;
    uint8_t extension_header;  // undefined if (header & kXbit) == 0
    rtc::ArrayView<const uint8_t> payload;
    int size;  // size of the header and payload combined.
  };
  struct Packet {
    explicit Packet(int first_obu_index)
        : first_obu(first_obu_index), last_obu(first_obu_index) {}
    // Indexes into obus_ vector of the first and last obus that should put into
    // the packet.
    int first_obu;
    int last_obu;
    int first_obu_offset = 0;
    int last_obu_size;
    // Total size consumed by the packet.
    int packet_size = 0;
  };

  static int NumObus(const Packet& packet) {
    return packet.last_obu - packet.first_obu + 1;
  }
  // Parses the payload into serie of OBUs.
  static std::vector<Obu> ParseObus(rtc::ArrayView<const uint8_t> payload);
  // Returns size to store last obu element size of the packet.
  // Returns 0 if packet is empty or size of the last obu element size is
  // already reserved.
  static int ExtraSizeForLastObu(const Packet& packet);
  static std::vector<Packet> Packetize(rtc::ArrayView<const Obu> obus,
                                       PayloadSizeLimits limits);
  uint8_t AggregationHeader(const Packet& next_packet) const;

  const std::vector<Obu> obus_;
  std::vector<Packet> packets_;
  size_t packet_index_ = 0;
};

}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_AV1_H_
