/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtcp_packet/transport_layer_feedback.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "api/units/time_delta.h"
#include "modules/rtp_rtcp/source/rtcp_packet/common_header.h"
#include "modules/rtp_rtcp/source/rtcp_packet/rtpfb.h"
#include "rtc_base/buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/ecn_marking.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace rtcp {

using ::testing::SizeIs;

// PacketInfo is equal after serializing-deserializing if members are equal
// except for arrival time offset that may differ because of conversion back and
// forth to CompactNtp.
bool PacketInfoEqual(const TransportLayerFeedback::PacketInfo& a,
                     const TransportLayerFeedback::PacketInfo& b) {
  bool equal = a.ssrc == b.ssrc && a.sequence_number == b.sequence_number &&
               a.arrival_time_offset.ms() == b.arrival_time_offset.ms() &&
               a.ecn == b.ecn;
  RTC_LOG_IF(LS_INFO, !equal)
      << " Not equal got ssrc: " << a.ssrc << ", seq  " << a.sequence_number
      << " arrival_time_offset: " << a.arrival_time_offset.ms()
      << " ecn: " << a.ecn << " expected ssrc:" << b.ssrc << ", seq  "
      << b.sequence_number << " arrival_time_offset"
      << b.arrival_time_offset.ms() << " ecn: " << b.ecn;
  return equal;
}

MATCHER_P(PacketInfoEqual, expected_vector, "") {
  if (expected_vector.size() != arg.size()) {
    RTC_LOG(LS_INFO) << " Wrong size, expected: " << expected_vector.size()
                     << " got: " << arg.size();
    return false;
  }
  for (size_t i = 0; i < expected_vector.size(); ++i) {
    if (!PacketInfoEqual(arg[i], expected_vector[i])) {
      return false;
    }
  }
  return true;
}

TEST(TransportLayerFeedbackTest, VerifyBlockLengthNoPackets) {
  TransportLayerFeedback fb({}, /*compact_ntp_timestamp=*/1);
  EXPECT_EQ(fb.BlockLength(),
            /*common header */ 4u /*sender ssrc*/ + 4u + /*timestamp*/ 4u);
}

TEST(TransportLayerFeedbackTest, VerifyBlockLengthTwoSsrcOnePacketEach) {
  std::vector<TransportLayerFeedback::PacketInfo> packets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1)},
      {.ssrc = 2,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(3)},
  };

  TransportLayerFeedback fb(std::move(packets), /*compact_ntp_timestamp=*/1);
  EXPECT_EQ(fb.BlockLength(),
            /*common header */ 4u + /*sender ssrc*/
                4u +
                /*timestamp*/ 4u +
                /*per ssrc header*/ 2 * 8u +
                /* padded packet info per ssrc*/ 2 * 4u);
}

TEST(TransportLayerFeedbackTest, VerifyBlockLengthTwoSsrcTwoPacketsEach) {
  std::vector<TransportLayerFeedback::PacketInfo> packets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1)},
      {.ssrc = 1,
       .sequence_number = 2,
       .arrival_time_offset = TimeDelta::Millis(2)},
      {.ssrc = 2,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(3)},
      {.ssrc = 2,
       .sequence_number = 2,
       .arrival_time_offset = TimeDelta::Millis(4)}};

  TransportLayerFeedback fb(std::move(packets), /*compact_ntp_timestamp=*/1);
  EXPECT_EQ(fb.BlockLength(),
            /*common header */ 4u + /*sender ssrc*/
                4u +
                /*timestamp*/ 4u +
                /*per ssrc header*/ 2 * 8u +
                /* padded packet info per ssrc*/ 2 * 4u);
}

TEST(TransportLayerFeedbackTest, CreateReturnsTrueForBasicPacket) {
  std::vector<TransportLayerFeedback::PacketInfo> packets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1)},
      {.ssrc = 2,
       .sequence_number = 2,
       .arrival_time_offset = TimeDelta::Millis(2)}};

  TransportLayerFeedback fb(std::move(packets), /*compact_ntp_timestamp=*/1);

  rtc::Buffer buf(fb.BlockLength());
  size_t position = 0;
  rtc::FunctionView<void(rtc::ArrayView<const uint8_t> packet)> callback;
  EXPECT_TRUE(fb.Create(buf.data(), &position, buf.capacity(), callback));
}

TEST(TransportLayerFeedbackTest, CanCreateAndParseFeedbackWithTwoSsrc) {
  const std::vector<TransportLayerFeedback::PacketInfo> kPackets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1)},
      {
          .ssrc = 2,
          .sequence_number = 1,
          .arrival_time_offset = TimeDelta::Millis(3),
      },
  };

  uint32_t kCompactNtp = 1234;
  TransportLayerFeedback fb(kPackets, kCompactNtp);

  rtc::Buffer buffer(fb.BlockLength());
  size_t position = 0;
  rtc::FunctionView<void(rtc::ArrayView<const uint8_t> packet)> callback;
  ASSERT_TRUE(fb.Create(buffer.data(), &position, buffer.capacity(), callback));

  TransportLayerFeedback parsed_fb;

  CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  EXPECT_EQ(header.fmt(), TransportLayerFeedback::kFeedbackMessageType);
  EXPECT_EQ(header.type(), Rtpfb::kPacketType);
  EXPECT_TRUE(parsed_fb.Parse(header));

  EXPECT_EQ(parsed_fb.compact_ntp(), kCompactNtp);
  EXPECT_THAT(parsed_fb.packets(), PacketInfoEqual(kPackets));
}

TEST(TransportLayerFeedbackTest, CanCreateAndParsePacketWithEcnCe) {
  const std::vector<TransportLayerFeedback::PacketInfo> kPackets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1),
       .ecn = rtc::EcnMarking::kCe}};

  uint32_t kCompactNtp = 1234;
  TransportLayerFeedback fb(kPackets, kCompactNtp);

  rtc::Buffer buffer(fb.BlockLength());
  size_t position = 0;
  rtc::FunctionView<void(rtc::ArrayView<const uint8_t> packet)> callback;
  ASSERT_TRUE(fb.Create(buffer.data(), &position, buffer.capacity(), callback));

  TransportLayerFeedback parsed_fb;

  CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  EXPECT_TRUE(parsed_fb.Parse(header));
  EXPECT_THAT(parsed_fb.packets(), PacketInfoEqual(kPackets));
}

TEST(TransportLayerFeedbackTest, CanCreateAndParsePacketWithEct1) {
  const std::vector<TransportLayerFeedback::PacketInfo> kPackets = {
      {.ssrc = 1,
       .sequence_number = 1,
       .arrival_time_offset = TimeDelta::Millis(1),
       .ecn = rtc::EcnMarking::kEct1}};

  uint32_t kCompactNtp = 1234;
  TransportLayerFeedback fb(kPackets, kCompactNtp);

  rtc::Buffer buffer(fb.BlockLength());
  size_t position = 0;
  rtc::FunctionView<void(rtc::ArrayView<const uint8_t> packet)> callback;
  ASSERT_TRUE(fb.Create(buffer.data(), &position, buffer.capacity(), callback));

  TransportLayerFeedback parsed_fb;

  CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  EXPECT_TRUE(parsed_fb.Parse(header));
  EXPECT_THAT(parsed_fb.packets(), PacketInfoEqual(kPackets));
}

TEST(TransportLayerFeedbackTest, CanCreateAndParseWithMissingPackets) {
  const std::vector<TransportLayerFeedback::PacketInfo> kPackets = {
      {
          .ssrc = 1,
          .sequence_number = 0xFFFE,
          .arrival_time_offset = TimeDelta::Millis(1),
      },
      {
          .ssrc = 1,
          .sequence_number = 1,
          .arrival_time_offset = TimeDelta::Millis(1),
      }};

  uint32_t kCompactNtp = 1234;
  TransportLayerFeedback fb(kPackets, kCompactNtp);

  rtc::Buffer buffer(fb.BlockLength());
  size_t position = 0;
  rtc::FunctionView<void(rtc::ArrayView<const uint8_t> packet)> callback;
  ASSERT_TRUE(fb.Create(buffer.data(), &position, buffer.capacity(), callback));

  TransportLayerFeedback parsed_fb;

  CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  EXPECT_TRUE(parsed_fb.Parse(header));
  EXPECT_THAT(parsed_fb.packets(), PacketInfoEqual(kPackets));
}

}  // namespace rtcp
}  // namespace webrtc
