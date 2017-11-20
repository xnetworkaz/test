/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <vector>

#include "modules/rtp_rtcp/source/rtp_format_video_stereo.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::Le;
using ::testing::SizeIs;

const size_t kMaxPayloadSize = 1200;
uint8_t kTestPayload[kMaxPayloadSize];

constexpr RtpVideoCodecTypes kTestAssociatedCodecType = kRtpVideoVp9;
constexpr uint8_t kTestFrameIndex = 23;
constexpr uint8_t kTestFrameCount = 34;
constexpr uint8_t kTestPictureIndex = 123;

RTPVideoHeaderStereo GenerateTestStereoHeader() {
  RTPVideoHeaderStereo header;
  header.associated_codec_type = kTestAssociatedCodecType;
  header.frame_index = kTestFrameIndex;
  header.frame_count = kTestFrameCount;
  header.picture_index = kTestPictureIndex;
  return header;
}

std::vector<size_t> NextPacketFillPayloadSizes(
    RtpPacketizerStereo* packetizer) {
  RtpPacketToSend packet(nullptr);
  std::vector<size_t> result;
  while (packetizer->NextPacket(&packet)) {
    result.push_back(packet.payload_size());
  }
  return result;
}

}  // namespace

TEST(RtpPacketizerVideoStereo, SmallMaxPayloadSizeThrowsErrors) {
  const size_t kMaxPayloadLen = 7;
  const size_t kLastPacketReductionLen = 2;
  const size_t kPayloadSize = 68;
  RtpPacketizerStereo packetizer(GenerateTestStereoHeader(), kVideoFrameKey,
                                 kMaxPayloadLen, kLastPacketReductionLen);
  packetizer.SetPayloadData(kTestPayload, kPayloadSize, nullptr);
  RtpPacketToSend packet(nullptr);
  EXPECT_FALSE(packetizer.NextPacket(&packet));
}

TEST(RtpPacketizerVideoStereo, AllPacketsMayBeEqual_RespectsMaxPayloadSize) {
  const size_t kMaxPayloadLen = 34;
  const size_t kLastPacketReductionLen = 2;
  const size_t kPayloadSize = 68;
  RtpPacketizerStereo packetizer(GenerateTestStereoHeader(), kVideoFrameKey,
                                 kMaxPayloadLen, kLastPacketReductionLen);
  size_t num_packets =
      packetizer.SetPayloadData(kTestPayload, kPayloadSize, nullptr);
  std::vector<size_t> payload_sizes = NextPacketFillPayloadSizes(&packetizer);
  EXPECT_THAT(payload_sizes, SizeIs(num_packets));
  EXPECT_THAT(payload_sizes, Each(Le(kMaxPayloadLen)));
}

TEST(RtpPacketizerVideoStereo, PreservesTypeAndHeader) {
  const size_t kMaxPayloadLen = 34;
  const size_t kLastPacketReductionLen = 2;
  const size_t kPayloadSize = 68;
  const auto kFrameType = kVideoFrameKey;
  RtpPacketizerStereo packetizer(GenerateTestStereoHeader(), kFrameType,
                                 kMaxPayloadLen, kLastPacketReductionLen);
  packetizer.SetPayloadData(kTestPayload, kPayloadSize, nullptr);
  RtpPacketToSend packet(nullptr);
  std::vector<RtpPacketToSend> result;
  while (packetizer.NextPacket(&packet)) {
    result.push_back(packet);
    packet = RtpPacketToSend(nullptr);
  }
  const auto& sent_payload = result[0].payload();

  RtpDepacketizerStereo depacketizer;
  RtpDepacketizer::ParsedPayload parsed_payload;
  ASSERT_TRUE(depacketizer.Parse(&parsed_payload, sent_payload.data(),
                                 sent_payload.size()));
  EXPECT_EQ(kFrameType, parsed_payload.frame_type);
  EXPECT_TRUE(parsed_payload.type.Video.is_first_packet_in_frame);
  EXPECT_EQ(kRtpVideoStereo, parsed_payload.type.Video.codec);
  EXPECT_EQ(kTestAssociatedCodecType,
            parsed_payload.type.Video.codecHeader.stereo.associated_codec_type);
  EXPECT_EQ(kTestFrameIndex,
            parsed_payload.type.Video.codecHeader.stereo.frame_index);
  EXPECT_EQ(kTestFrameCount,
            parsed_payload.type.Video.codecHeader.stereo.frame_count);
  EXPECT_EQ(kTestPictureIndex,
            parsed_payload.type.Video.codecHeader.stereo.picture_index);
}

}  // namespace webrtc
