/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_PSFB_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_PSFB_H_

#include <stddef.h>
#include <stdint.h>

#include "modules/rtp_rtcp/source/rtcp_packet.h"

namespace webrtc {
namespace rtcp {

// PSFB: Payload-specific feedback message.
// RFC 4585, Section 6.3.
class Psfb : public RtcpPacket {
 public:
  static constexpr uint8_t kPacketType = 206;
  static constexpr uint8_t kAfbMessageType = 15;

  Psfb() = default;
  ~Psfb() override = default;

  void SetMediaSsrc(uint32_t ssrc) { media_ssrc_ = ssrc; }

  uint32_t media_ssrc() const { return media_ssrc_; }

 protected:
  static constexpr size_t kCommonFeedbackLength = 8;
  void ParseCommonFeedback(const uint8_t* payload);
  void CreateCommonFeedback(uint8_t* payload) const;

 private:
  uint32_t media_ssrc_ = 0;
};

}  // namespace rtcp
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTCP_PACKET_PSFB_H_
