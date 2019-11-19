/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_depacketizer_av1.h"

#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;

// Signals number of the OBU (fragments) in the packet.
constexpr uint8_t kObuCountAny = 0b0000'0000;
constexpr uint8_t kObuCountOne = 0b0001'0000;
constexpr uint8_t kObuCountTwo = 0b0010'0000;

constexpr uint8_t kObuHeaderSequenceHeader = 0b0'0001'000;
constexpr uint8_t kObuHeaderTemporalDelimiter = 0b0'0010'000;
constexpr uint8_t kObuHeaderFrame = 0b0'0110'000;

constexpr uint8_t kObuHeaderHasSize = 0b0'0000'010;

TEST(RtpDepacketizerAv1Test, ParsePassFullRtpPayloadAsCodecPayload) {
  const uint8_t packet[] = {(uint8_t{1} << 7) | kObuCountOne, 1, 2, 3, 4};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_EQ(parsed.payload_length, sizeof(packet));
  EXPECT_TRUE(parsed.payload == packet);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsContinuationFlagAsNotBeginningOfFrame) {
  const uint8_t packet[] = {
      (uint8_t{1} << 7) | kObuCountOne,
      kObuHeaderFrame};  // Value doesn't matter since it is a
                         // continuation of the OBU from previous packet.
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_FALSE(parsed.video.is_first_packet_in_frame);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsNoContinuationFlagAsBeginningOfFrame) {
  const uint8_t packet[] = {(uint8_t{0} << 7) | kObuCountOne, kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.is_first_packet_in_frame);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsWillContinueFlagAsNotEndOfFrame) {
  const uint8_t packet[] = {(uint8_t{1} << 6) | kObuCountOne, kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_FALSE(parsed.video.is_last_packet_in_frame);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsNoWillContinueFlagAsEndOfFrame) {
  const uint8_t packet[] = {(uint8_t{0} << 6) | kObuCountOne, kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.is_last_packet_in_frame);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsStartOfSequenceHeaderAsKeyFrame) {
  const uint8_t packet[] = {kObuCountOne, kObuHeaderSequenceHeader};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.is_first_packet_in_frame);
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test, ParseTreatsNotStartOfFrameAsDeltaFrame) {
  const uint8_t packet[] = {
      (uint8_t{1} << 7) | kObuCountOne,
      // Byte that look like start of sequence header, but since it is not start
      // of an OBU, it is actually not a start of sequence header.
      kObuHeaderSequenceHeader};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_FALSE(parsed.video.is_first_packet_in_frame);
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameDelta);
}

TEST(RtpDepacketizerAv1Test,
     ParseTreatsStartOfFrameWithoutSequenceHeaderAsDeltaFrame) {
  const uint8_t packet[] = {kObuCountOne, kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.is_first_packet_in_frame);
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameDelta);
}

TEST(RtpDepacketizerAv1Test, ParseFindsSequenceHeaderBehindFragmentSize1) {
  const uint8_t packet[] = {kObuCountAny,
                            1,  // size of the next fragment
                            kObuHeaderSequenceHeader};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test, ParseFindsSequenceHeaderBehindFragmentSize2) {
  const uint8_t packet[] = {kObuCountTwo,
                            2,  // size of the next fragment
                            kObuHeaderSequenceHeader,
                            42,  // SH payload.
                            kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test,
     ParseFindsSequenceHeaderBehindMultiByteFragmentSize) {
  const uint8_t packet[] = {kObuCountTwo,
                            0b1000'0101,  // leb128 encoded value of 5
                            0b1000'0000,  // using 3 bytes
                            0b0000'0000,  // to encode the value.
                            kObuHeaderSequenceHeader,
                            8,  // 4 bytes of SH payload.
                            0,
                            0,
                            0,
                            kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test, ParseFindsSequenceHeaderBehindTemporalDelimiter) {
  const uint8_t packet[] = {kObuCountTwo,
                            1,  // size of the next fragment
                            kObuHeaderTemporalDelimiter,
                            kObuHeaderSequenceHeader,
                            8,  // 4 bytes of SH payload.
                            0,
                            0,
                            0};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test,
     ParseFindsSequenceHeaderBehindTemporalDelimiterAndSize) {
  const uint8_t packet[] = {kObuCountAny,
                            1,  // size of the next fragment
                            kObuHeaderTemporalDelimiter,
                            5,  // size of the next fragment
                            kObuHeaderSequenceHeader,
                            8,  // 4 bytes of SH payload.
                            0,
                            0,
                            0,
                            1,  // size of the next fragment
                            kObuHeaderFrame};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameKey);
}

TEST(RtpDepacketizerAv1Test, ParseSkipsEmptyFragments) {
  static_assert(kObuHeaderSequenceHeader == 8, "");
  const uint8_t packet[] = {kObuCountAny,
                            0,  // size of the next fragment
                            8,  // size of the next fragment that look like SH
                            kObuHeaderFrame,
                            1,
                            2,
                            3,
                            4,
                            5,
                            6,
                            7};
  RtpDepacketizerAv1 depacketizer;
  RtpDepacketizer::ParsedPayload parsed;
  ASSERT_TRUE(depacketizer.Parse(&parsed, packet, sizeof(packet)));
  EXPECT_TRUE(parsed.video.frame_type == VideoFrameType::kVideoFrameDelta);
}

TEST(RtpDepacketizerAv1Test, AssembleFrameSetsOBUPayloadSizeWhenAbsent) {
  const uint8_t payload1[] = {0b00'01'0000,  // aggregation header
                              0b0'0110'000,  // /  Frame
                              20, 30, 40};   // \  OBU
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_TRUE(frame_view[0] & kObuHeaderHasSize);
  EXPECT_EQ(frame_view[1], 3);
}

TEST(RtpDepacketizerAv1Test, AssembleFrameSetsOBUPayloadSizeWhenPresent) {
  const uint8_t payload1[] = {0b00'01'0000,  // aggregation header
                              0b0'0110'010,  // /  Frame OBU header
                              3,             // obu_size
                              20,
                              30,
                              40};  // \  obu_payload
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_TRUE(frame_view[0] & kObuHeaderHasSize);
  EXPECT_EQ(frame_view[1], 3);
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameSetsOBUPayloadSizeAfterExtensionWhenAbsent) {
  const uint8_t payload1[] = {0b00'01'0000,           // aggregation header
                              0b0'0110'100,           // /  Frame
                              0b010'01'000,           // | extension_header
                              20,           30, 40};  // \  OBU
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_TRUE(frame_view[0] & kObuHeaderHasSize);
  EXPECT_EQ(frame_view[2], 3);
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameSetsOBUPayloadSizeAfterExtensionWhenPresent) {
  const uint8_t payload1[] = {0b00'01'0000,  // aggregation header
                              0b0'0110'110,  // /  Frame OBU header
                              0b010'01'000,  // | extension_header
                              3,             // | obu_size
                              20,
                              30,
                              40};  // \  obu_payload
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_TRUE(frame_view[0] & kObuHeaderHasSize);
  EXPECT_EQ(frame_view[2], 3);
}

TEST(RtpDepacketizerAv1Test, AssembleFrameFromOnePacketWithOneObu) {
  const uint8_t payload1[] = {0b00'01'0000,  // aggregation header
                              0b0'0110'000,  // /  Frame
                              20};           // \  OBU
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0110'010, 1, 20));
}

TEST(RtpDepacketizerAv1Test, AssembleFrameFromOnePacketWithTwoObus) {
  const uint8_t payload1[] = {0b00'10'0000,  // aggregation header
                              2,             // /  Sequence
                              0b0'0001'000,  // |  Header
                              10,            // \  OBU
                              0b0'0110'000,  // /  Frame
                              20};           // \  OBU
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0001'010, 1, 10,    // Sequence Header OBU
                          0b0'0110'010, 1, 20));  // Frame OBU
}

TEST(RtpDepacketizerAv1Test, AssembleFrameFromTwoPacketsWithOneObu) {
  const uint8_t payload1[] = {0b01'01'0000,  // aggregation header
                              0b0'0110'000, 20, 30};
  const uint8_t payload2[] = {0b10'01'0000,  // aggregation header
                              40};
  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0110'010, 3, 20, 30, 40));
}

TEST(RtpDepacketizerAv1Test, AssembleFrameFromTwoPacketsWithTwoObu) {
  const uint8_t payload1[] = {0b01'10'0000,  // aggregation header
                              2,             // /  Sequence
                              0b0'0001'000,  // |  Header
                              10,            // \  OBU
                              0b0'0110'000,  //
                              20,
                              30};           //
  const uint8_t payload2[] = {0b10'01'0000,  // aggregation header
                              40};           //
  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0001'010, 1, 10,            // SH
                          0b0'0110'010, 3, 20, 30, 40));  // Frame
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameFromTwoPacketsWithManyObusSomeWithExtensions) {
  const uint8_t payload1[] = {0b01'00'0000,  // aggregation header
                              2,             // /
                              0b0'0001'000,  // |  Sequence Header
                              10,            // \  OBU
                              2,             // /
                              0b0'0101'000,  // |  Metadata OBU
                              20,            // \  without extension
                              4,             // /
                              0b0'0101'100,  // |  Metadata OBU
                              0b001'10'000,  // |  with extension
                              20,            // |
                              30,            // \  metadata payload
                              5,             // /
                              0b0'0110'100,  // |  Frame OBU
                              0b001'10'000,  // |  with extension
                              40,            // |
                              50,            // |
                              60};           // |
  const uint8_t payload2[] = {0b10'01'0000,  // aggregation header
                              70, 80, 90};   // \  tail of the frame OBU

  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(  // Sequence header OBU
                  0b0'0001'010, 1, 10,
                  // Metadata OBU without extension
                  0b0'0101'010, 1, 20,
                  // Metadata OBU with extenion
                  0b0'0101'110, 0b001'10'000, 2, 20, 30,
                  // Frame OBU with extension
                  0b0'0110'110, 0b001'10'000, 6, 40, 50, 60, 70, 80, 90));
}

TEST(RtpDepacketizerAv1Test, AssembleFrameWithOneObuFromManyPackets) {
  const uint8_t payload1[] = {0b01'01'0000,  // aggregation header
                              0b0'0110'000, 11, 12};
  const uint8_t payload2[] = {0b11'01'0000,  // aggregation header
                              13, 14};
  const uint8_t payload3[] = {0b11'01'0000,  // aggregation header
                              15, 16, 17};
  const uint8_t payload4[] = {0b10'01'0000,  // aggregation header
                              18};

  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2, payload3,
                                              payload4};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0110'010, 8, 11, 12, 13, 14, 15, 16, 17, 18));
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameFromManyPacketsWithSomeObuBorderAligned) {
  const uint8_t payload1[] = {0b01'10'0000,  // aggregation header
                              3,             // size of the 1st fragment
                              0b0'0011'000,  // Frame header OBU
                              11,
                              12,
                              0b0'0100'000,  // Tile group OBU
                              21,
                              22,
                              23};
  const uint8_t payload2[] = {0b10'01'0000,  // aggregation header
                              24, 25, 26, 27};
  // payload2 ends an OBU, payload3 starts a new one.
  const uint8_t payload3[] = {0b01'10'0000,  // aggregation header
                              3,             // size of the 1st fragment
                              0b0'0111'000,  // Redundant frame header OBU
                              11,
                              12,
                              0b0'0100'000,  // Tile group OBU
                              31,
                              32};
  const uint8_t payload4[] = {0b10'01'0000,  // aggregation header
                              33, 34, 35, 36};
  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2, payload3,
                                              payload4};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_THAT(rtc::ArrayView<const uint8_t>(*frame),
              ElementsAre(0b0'0011'010, 2, 11, 12,  // Frame header
                          0b0'0100'010, 7, 21, 22, 23, 24, 25, 26, 27,  //
                          0b0'0111'010, 2, 11, 12,                      //
                          0b0'0100'010, 6, 31, 32, 33, 34, 35, 36));
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameFromOnePacketsOneObuPayloadSize127Bytes) {
  uint8_t payload1[4 + 127];
  memset(payload1, 0, sizeof(payload1));
  payload1[0] = 0b00'00'0000;  // aggregation header
  payload1[1] = 0x80;          // leb128 encoded size of 128 bytes
  payload1[2] = 0x01;          // in two bytes
  payload1[3] = 0b0'0110'000;  // obu_header with size and extension bits unset.
  payload1[4 + 42] = 0x42;
  rtc::ArrayView<const uint8_t> payloads[] = {payload1};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->size(), 2 + 127u);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_EQ(frame_view[0], 0b0'0110'010);  // obu_header with size bit set.
  EXPECT_EQ(frame_view[1], 127);  // obu payload size, 1 byte enough to encode.
  // Check 'random' byte from the payload is at the same 'random' offset.
  EXPECT_EQ(frame_view[2 + 42], 0x42);
}

TEST(RtpDepacketizerAv1Test,
     AssembleFrameFromTwoPacketsOneObuPayloadSize128Bytes) {
  uint8_t payload1[3 + 32];
  memset(payload1, 0, sizeof(payload1));
  payload1[0] = 0b01'00'0000;  // aggregation header
  payload1[1] = 33;            // leb128 encoded size of 33 bytes in one byte
  payload1[2] = 0b0'0110'000;  // obu_header with size and extension bits unset.
  payload1[3 + 10] = 0x10;
  uint8_t payload2[2 + 96];
  memset(payload2, 0, sizeof(payload2));
  payload2[0] = 0b10'00'0000;  // aggregation header
  payload2[1] = 96;            // leb128 encoded size of 96 bytes in one byte
  payload2[2 + 20] = 0x20;

  rtc::ArrayView<const uint8_t> payloads[] = {payload1, payload2};
  auto frame = RtpDepacketizerAv1::AssembleFrame(payloads);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame->size(), 3 + 128u);
  rtc::ArrayView<const uint8_t> frame_view(*frame);
  EXPECT_EQ(frame_view[0], 0b0'0110'010);  // obu_header with size bit set.
  EXPECT_EQ(frame_view[1], 0x80);          // obu payload size of 128 bytes.
  EXPECT_EQ(frame_view[2], 0x01);          // encoded in two byes
  // Check two 'random' byte from the payload is at the same 'random' offset.
  EXPECT_EQ(frame_view[3 + 10], 0x10);
  EXPECT_EQ(frame_view[3 + 32 + 20], 0x20);
}

}  // namespace
}  // namespace webrtc
