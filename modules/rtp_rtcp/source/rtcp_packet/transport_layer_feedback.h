/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_TRANSPORT_LAYER_FEEDBACK_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_TRANSPORT_LAYER_FEEDBACK_H_

#include <cstdint>
#include <map>
#include <vector>

#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/source/rtcp_packet/common_header.h"
#include "modules/rtp_rtcp/source/rtcp_packet/rtpfb.h"
#include "rtc_base/network/ecn_marking.h"

namespace webrtc {
namespace rtcp {

// Congestion control feedback message as specified in
// https://www.rfc-editor.org/rfc/rfc8888.html
class TransportLayerFeedback : public rtcp::Rtpfb {
 public:
  struct PacketInfo {
    uint16_t sequence_number = 0;
    //  Time offset from  compact_ntp_timestamp.
    TimeDelta arrival_time_offset = TimeDelta::Zero();
    rtc::EcnMarking ecn = rtc::EcnMarking::kNotEct;
  };

  static constexpr uint8_t kFeedbackMessageType = 11;

  // `Packets` MUST be sorted in sequence_number order per SSRC.
  // `Packets` MUST not include duplicate sequence numbers.
  TransportLayerFeedback(
      std::map<uint32_t /*ssrc*/, std::vector<PacketInfo>> packets,
      uint32_t compact_ntp_timestamp);
  TransportLayerFeedback() = default;

  bool Parse(const rtcp::CommonHeader& packet);

  std::map<uint32_t /*ssrc*/, std::vector<PacketInfo>> packets() const {
    return packets_;
  }

  uint32_t compact_ntp() const { return compact_ntp_timestamp_; }

  // Serialize the packet.
  bool Create(uint8_t* packet,
              size_t* position,
              size_t max_length,
              PacketReadyCallback callback) const override;
  size_t BlockLength() const override;

 private:
  std::map<uint32_t /*ssrc*/, std::vector<PacketInfo>> packets_;
  uint32_t compact_ntp_timestamp_ = 0;
};

}  // namespace rtcp
}  // namespace webrtc

#endif  //  MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_TRANSPORT_LAYER_FEEDBACK_H_
