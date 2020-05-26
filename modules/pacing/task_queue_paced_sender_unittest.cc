/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/pacing/task_queue_paced_sender.h"

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "modules/pacing/packet_router.h"
#include "modules/utility/include/mock/mock_process_thread.h"
#include "test/field_trial.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::SaveArg;

namespace webrtc {
namespace {
constexpr uint32_t kAudioSsrc = 12345;
constexpr uint32_t kVideoSsrc = 234565;
constexpr uint32_t kVideoRtxSsrc = 34567;
constexpr uint32_t kFlexFecSsrc = 45678;
constexpr size_t kDefaultPacketSize = 1234;

class MockPacketRouter : public PacketRouter {
 public:
  MOCK_METHOD(void,
              SendPacket,
              (std::unique_ptr<RtpPacketToSend> packet,
               const PacedPacketInfo& cluster_info),
              (override));
  MOCK_METHOD(std::vector<std::unique_ptr<RtpPacketToSend>>,
              GeneratePadding,
              (DataSize target_size),
              (override));
};
}  // namespace

namespace test {

  std::unique_ptr<RtpPacketToSend> BuildRtpPacket(RtpPacketMediaType type) {
    auto packet = std::make_unique<RtpPacketToSend>(nullptr);
    packet->set_packet_type(type);
    switch (type) {
      case RtpPacketMediaType::kAudio:
        packet->SetSsrc(kAudioSsrc);
        break;
      case RtpPacketMediaType::kVideo:
        packet->SetSsrc(kVideoSsrc);
        break;
      case RtpPacketMediaType::kRetransmission:
      case RtpPacketMediaType::kPadding:
        packet->SetSsrc(kVideoRtxSsrc);
        break;
      case RtpPacketMediaType::kForwardErrorCorrection:
        packet->SetSsrc(kFlexFecSsrc);
        break;
    }

    packet->SetPayloadSize(kDefaultPacketSize);
    return packet;
  }

  std::vector<std::unique_ptr<RtpPacketToSend>> GeneratePackets(
      RtpPacketMediaType type,
      size_t num_packets) {
    std::vector<std::unique_ptr<RtpPacketToSend>> packets;
    for (size_t i = 0; i < num_packets; ++i) {
      packets.push_back(BuildRtpPacket(type));
    }
    return packets;
  }

  TEST(TaskQueuePacedSenderTest, PacesPackets) {
    GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
    MockPacketRouter packet_router;
    TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router,
                               /*event_log=*/nullptr,
                               /*field_trials=*/nullptr,
                               time_controller.GetTaskQueueFactory(),
                               PacingController::kMinSleepTime);

    // Insert a number of packets, covering one second.
    static constexpr size_t kPacketsToSend = 42;
    pacer.SetPacingRates(
        DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsToSend),
        DataRate::Zero());
    pacer.EnqueuePackets(
        GeneratePackets(RtpPacketMediaType::kVideo, kPacketsToSend));

    // Expect all of them to be sent.
    size_t packets_sent = 0;
    Timestamp end_time = Timestamp::PlusInfinity();
    EXPECT_CALL(packet_router, SendPacket)
        .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> packet,
                            const PacedPacketInfo& cluster_info) {
          ++packets_sent;
          if (packets_sent == kPacketsToSend) {
            end_time = time_controller.GetClock()->CurrentTime();
          }
        });

    const Timestamp start_time = time_controller.GetClock()->CurrentTime();

    // Packets should be sent over a period of close to 1s. Expect a little
    // lower than this since initial probing is a bit quicker.
    time_controller.AdvanceTime(TimeDelta::Seconds(1));
    EXPECT_EQ(packets_sent, kPacketsToSend);
    ASSERT_TRUE(end_time.IsFinite());
    EXPECT_NEAR((end_time - start_time).ms<double>(), 1000.0, 50.0);
  }

  TEST(TaskQueuePacedSenderTest, ReschedulesProcessOnRateChange) {
    GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
    MockPacketRouter packet_router;
    TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router,
                               /*event_log=*/nullptr,
                               /*field_trials=*/nullptr,
                               time_controller.GetTaskQueueFactory(),
                               PacingController::kMinSleepTime);

    // Insert a number of packets to be sent 200ms apart.
    const size_t kPacketsPerSecond = 5;
    const DataRate kPacingRate =
        DataRate::BitsPerSec(kDefaultPacketSize * 8 * kPacketsPerSecond);
    pacer.SetPacingRates(kPacingRate, DataRate::Zero());

    // Send some initial packets to be rid of any probes.
    EXPECT_CALL(packet_router, SendPacket).Times(kPacketsPerSecond);
    pacer.EnqueuePackets(
        GeneratePackets(RtpPacketMediaType::kVideo, kPacketsPerSecond));
    time_controller.AdvanceTime(TimeDelta::Seconds(1));

    // Insert three packets, and record send time of each of them.
    // After the second packet is sent, double the send rate so we can
    // check the third packets is sent after half the wait time.
    Timestamp first_packet_time = Timestamp::MinusInfinity();
    Timestamp second_packet_time = Timestamp::MinusInfinity();
    Timestamp third_packet_time = Timestamp::MinusInfinity();

    EXPECT_CALL(packet_router, SendPacket)
        .Times(3)
        .WillRepeatedly([&](std::unique_ptr<RtpPacketToSend> packet,
                            const PacedPacketInfo& cluster_info) {
          if (first_packet_time.IsInfinite()) {
            first_packet_time = time_controller.GetClock()->CurrentTime();
          } else if (second_packet_time.IsInfinite()) {
            second_packet_time = time_controller.GetClock()->CurrentTime();
            pacer.SetPacingRates(2 * kPacingRate, DataRate::Zero());
          } else {
            third_packet_time = time_controller.GetClock()->CurrentTime();
          }
        });

    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 3));
    time_controller.AdvanceTime(TimeDelta::Millis(500));
    ASSERT_TRUE(third_packet_time.IsFinite());
    EXPECT_NEAR((second_packet_time - first_packet_time).ms<double>(), 200.0,
                1.0);
    EXPECT_NEAR((third_packet_time - second_packet_time).ms<double>(), 100.0,
                1.0);
  }

  TEST(TaskQueuePacedSenderTest, SendsAudioImmediately) {
    GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
    MockPacketRouter packet_router;
    TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router,
                               /*event_log=*/nullptr,
                               /*field_trials=*/nullptr,
                               time_controller.GetTaskQueueFactory(),
                               PacingController::kMinSleepTime);

    const DataRate kPacingDataRate = DataRate::KilobitsPerSec(125);
    const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
    const TimeDelta kPacketPacingTime = kPacketSize / kPacingDataRate;

    pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());

    // Add some initial video packets, only one should be sent.
    EXPECT_CALL(packet_router, SendPacket);
    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
    time_controller.AdvanceTime(TimeDelta::Zero());
    ::testing::Mock::VerifyAndClearExpectations(&packet_router);

    // Advance time, but still before next packet should be sent.
    time_controller.AdvanceTime(kPacketPacingTime / 2);

    // Insert an audio packet, it should be sent immediately.
    EXPECT_CALL(packet_router, SendPacket);
    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kAudio, 1));
    time_controller.AdvanceTime(TimeDelta::Zero());
    ::testing::Mock::VerifyAndClearExpectations(&packet_router);
  }

  TEST(TaskQueuePacedSenderTest, SleepsDuringCoalscingWindow) {
    const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
    GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
    MockPacketRouter packet_router;
    TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router,
                               /*event_log=*/nullptr,
                               /*field_trials=*/nullptr,
                               time_controller.GetTaskQueueFactory(),
                               kCoalescingWindow);

    // Set rates so one packet adds one ms of buffer level.
    const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
    const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
    const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;

    pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());

    // Add 10 packets. The first should be sent immediately since the buffers
    // are clear.
    EXPECT_CALL(packet_router, SendPacket);
    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
    time_controller.AdvanceTime(TimeDelta::Zero());
    ::testing::Mock::VerifyAndClearExpectations(&packet_router);

    // Advance time to 1ms before the coalescing window ends. No packets should
    // be sent.
    EXPECT_CALL(packet_router, SendPacket).Times(0);
    time_controller.AdvanceTime(kCoalescingWindow - TimeDelta::Millis(1));

    // Advance time to where coalescing window ends. All packets that should
    // have been sent up til now will be sent.
    EXPECT_CALL(packet_router, SendPacket).Times(5);
    time_controller.AdvanceTime(TimeDelta::Millis(1));
    ::testing::Mock::VerifyAndClearExpectations(&packet_router);
  }

  TEST(TaskQueuePacedSenderTest, ProbingOverridesCoalescingWindow) {
    const TimeDelta kCoalescingWindow = TimeDelta::Millis(5);
    GlobalSimulatedTimeController time_controller(Timestamp::Millis(1234));
    MockPacketRouter packet_router;
    TaskQueuePacedSender pacer(time_controller.GetClock(), &packet_router,
                               /*event_log=*/nullptr,
                               /*field_trials=*/nullptr,
                               time_controller.GetTaskQueueFactory(),
                               kCoalescingWindow);

    // Set rates so one packet adds one ms of buffer level.
    const DataSize kPacketSize = DataSize::Bytes(kDefaultPacketSize);
    const TimeDelta kPacketPacingTime = TimeDelta::Millis(1);
    const DataRate kPacingDataRate = kPacketSize / kPacketPacingTime;

    pacer.SetPacingRates(kPacingDataRate, DataRate::Zero());

    // Add 10 packets. The first should be sent immediately since the buffers
    // are clear. This will also trigger the probe to start.
    EXPECT_CALL(packet_router, SendPacket).Times(AtLeast(1));
    pacer.CreateProbeCluster(kPacingDataRate * 2, 17);
    pacer.EnqueuePackets(GeneratePackets(RtpPacketMediaType::kVideo, 10));
    time_controller.AdvanceTime(TimeDelta::Zero());
    ::testing::Mock::VerifyAndClearExpectations(&packet_router);

    // Advance time to 1ms before the coalescing window ends. Packets should be
    // flying.
    EXPECT_CALL(packet_router, SendPacket).Times(AtLeast(1));
    time_controller.AdvanceTime(kCoalescingWindow - TimeDelta::Millis(1));
  }

}  // namespace test
}  // namespace webrtc
