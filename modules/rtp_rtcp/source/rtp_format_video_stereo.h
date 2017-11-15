/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_VIDEO_STEREO_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_VIDEO_STEREO_H_

#include <string>

#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_format_video_generic.h"
#include "rtc_base/constructormagic.h"

namespace webrtc {
namespace RtpFormatVideoStereo {
static const uint8_t kFirstPacketBit = 0x02;
}  // namespace RtpFormatVideoStereo

class RtpPacketizerStereo : public RtpPacketizer {
 public:
  RtpPacketizerStereo(const RTPVideoHeaderStereo& header,
                      FrameType frame_type,
                      size_t max_payload_len,
                      size_t last_packet_reduction_len);

  virtual ~RtpPacketizerStereo();

  size_t SetPayloadData(const uint8_t* payload_data,
                        size_t payload_size,
                        const RTPFragmentationHeader* fragmentation) override;

  // Get the next payload with generic payload header.
  // Write payload and set marker bit of the |packet|.
  // Returns true on success, false otherwise.
  bool NextPacket(RtpPacketToSend* packet) override;

  ProtectionType GetProtectionType();

  StorageType GetStorageType(uint32_t retransmission_settings);

  std::string ToString() override;

 private:
  const RTPVideoHeaderStereo header_;
  const size_t max_payload_len_;
  const size_t last_packet_reduction_len_;
  uint8_t header_marker_;
  RtpPacketizerGeneric packetizer_;

  RTC_DISALLOW_COPY_AND_ASSIGN(RtpPacketizerStereo);
};

class RtpDepacketizerStereo : public RtpDepacketizer {
 public:
  virtual ~RtpDepacketizerStereo() {}

  bool Parse(ParsedPayload* parsed_payload,
             const uint8_t* payload_data,
             size_t payload_data_length) override;

 private:
  RtpDepacketizerGeneric depacketizer_;
};
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_VIDEO_STEREO_H_
