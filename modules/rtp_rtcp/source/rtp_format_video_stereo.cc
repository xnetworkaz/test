/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <string>

#include "modules/include/module_common_types.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtp_format_video_stereo.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "rtc_base/logging.h"

namespace webrtc {

namespace {
constexpr size_t kStereoHeaderMarkerLength = 1;
// RTPVideoHeaderStereo consist of:
// - RtpVideoCodecTypes associated_codec_type cast as uint8_t
// - uint8_t frame_index
// - uint8_t frame_count
// - uint64_t picture_index
constexpr size_t kStereoHeaderLength =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint64_t);
constexpr int64_t kMinExpectedMaxPayloadLen = 2;
constexpr uint8_t kFirstPacketBit = 0x02;
}  // unnamed namespace

RtpPacketizerStereo::RtpPacketizerStereo(const RTPVideoHeaderStereo& header,
                                         FrameType frame_type,
                                         size_t max_payload_len,
                                         size_t last_packet_reduction_len)
    : header_(header),
      max_payload_len_(max_payload_len - kStereoHeaderMarkerLength -
                       kStereoHeaderLength),
      last_packet_reduction_len_(last_packet_reduction_len),
      packetizer_(frame_type,
                  max_payload_len_ > kMinExpectedMaxPayloadLen
                      ? max_payload_len_
                      : kMinExpectedMaxPayloadLen,
                  last_packet_reduction_len_) {}

RtpPacketizerStereo::~RtpPacketizerStereo() {}

size_t RtpPacketizerStereo::SetPayloadData(
    const uint8_t* payload_data,
    size_t payload_size,
    const RTPFragmentationHeader* fragmentation) {
  header_marker_ = kFirstPacketBit;
  return packetizer_.SetPayloadData(payload_data, payload_size, fragmentation);
}

bool RtpPacketizerStereo::NextPacket(RtpPacketToSend* packet) {
  if (max_payload_len_ <= 0) {
    RTC_LOG(LS_ERROR) << "Payload length not large enough.";
    return false;
  }
  RTC_DCHECK(packet);
  if (!packetizer_.NextPacket(packet))
    return false;

  const bool first_packet = header_marker_ == kFirstPacketBit;
  size_t header_length = first_packet
                             ? kStereoHeaderMarkerLength + kStereoHeaderLength
                             : kStereoHeaderMarkerLength;

  std::unique_ptr<RtpPacketToSend> copied_packet(new RtpPacketToSend(*packet));
  uint8_t* wrapped_payload =
      packet->AllocatePayload(header_length + packet->payload_size());
  RTC_DCHECK(wrapped_payload);
  wrapped_payload[0] = header_marker_;
  size_t offset = kStereoHeaderMarkerLength;
  header_marker_ &= ~kFirstPacketBit;
  if (first_packet) {
    ByteWriter<uint8_t>::WriteBigEndian(
        &wrapped_payload[offset],
        static_cast<uint8_t>(header_.associated_codec_type));
    offset += sizeof(uint8_t);
    ByteWriter<uint8_t>::WriteBigEndian(&wrapped_payload[offset],
                                        header_.frame_index);
    offset += sizeof(uint8_t);
    ByteWriter<uint8_t>::WriteBigEndian(&wrapped_payload[offset],
                                        header_.frame_count);
    offset += sizeof(uint8_t);
    ByteWriter<uint64_t>::WriteBigEndian(&wrapped_payload[offset],
                                         header_.picture_index);
    RTC_DCHECK_EQ(kStereoHeaderLength + kStereoHeaderMarkerLength,
                  offset + sizeof(uint64_t));
  }
  auto payload = copied_packet->payload();
  memcpy(&wrapped_payload[header_length], payload.data(), payload.size());
  return true;
}

std::string RtpPacketizerStereo::ToString() {
  return "RtpPacketizerStereo";
}

RtpDepacketizerStereo::~RtpDepacketizerStereo() {}

bool RtpDepacketizerStereo::Parse(ParsedPayload* parsed_payload,
                                  const uint8_t* payload_data,
                                  size_t payload_data_length) {
  RTC_DCHECK(parsed_payload != NULL);
  if (payload_data_length == 0) {
    RTC_LOG(LS_ERROR) << "Empty payload.";
    return false;
  }

  uint8_t marker_header = *payload_data++;
  --payload_data_length;
  const bool first_packet = (marker_header & kFirstPacketBit) != 0;

  if (first_packet) {
    if (payload_data_length <= kStereoHeaderLength) {
      RTC_LOG(LS_ERROR) << "Payload not large enough.";
      return false;
    }
    size_t offset = 0;
    parsed_payload->type.Video.codecHeader.stereo.associated_codec_type =
        static_cast<RtpVideoCodecTypes>(
            ByteReader<uint8_t>::ReadBigEndian(&payload_data[offset]));
    offset += sizeof(uint8_t);
    parsed_payload->type.Video.codecHeader.stereo.frame_index =
        ByteReader<uint8_t>::ReadBigEndian(&payload_data[offset]);
    offset += sizeof(uint8_t);
    parsed_payload->type.Video.codecHeader.stereo.frame_count =
        ByteReader<uint8_t>::ReadBigEndian(&payload_data[offset]);
    offset += sizeof(uint8_t);
    parsed_payload->type.Video.codecHeader.stereo.picture_index =
        ByteReader<uint64_t>::ReadBigEndian(&payload_data[offset]);
    RTC_DCHECK_EQ(kStereoHeaderLength, offset + sizeof(uint64_t));
    payload_data += kStereoHeaderLength;
    payload_data_length -= kStereoHeaderLength;
  }
  const bool rv =
      depacketizer_.Parse(parsed_payload, payload_data, payload_data_length);
  RTC_DCHECK(rv);
  RTC_DCHECK_EQ(parsed_payload->type.Video.is_first_packet_in_frame,
                first_packet);
  parsed_payload->type.Video.codec = kRtpVideoStereo;
  return rv;
}
}  // namespace webrtc
