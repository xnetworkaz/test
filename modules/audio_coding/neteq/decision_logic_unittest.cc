/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Unit tests for DecisionLogic class and derived classes.

#include "modules/audio_coding/neteq/decision_logic.h"

#include "api/neteq/neteq_controller.h"
#include "api/neteq/tick_timer.h"
#include "modules/audio_coding/neteq/buffer_level_filter.h"
#include "modules/audio_coding/neteq/delay_manager.h"
#include "modules/audio_coding/neteq/mock/mock_buffer_level_filter.h"
#include "modules/audio_coding/neteq/mock/mock_delay_manager.h"
#include "modules/audio_coding/neteq/packet_buffer.h"
#include "modules/audio_coding/neteq/statistics_calculator.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/mock_audio_decoder_factory.h"

namespace webrtc {

namespace {
constexpr int kSampleRate = 8000;
constexpr int kSamplesPerMs = kSampleRate / 1000;
constexpr int kOutputSizeSamples = kSamplesPerMs * 10;
constexpr int kMinTimescaleInterval = 5;

NetEqController::NetEqStatus CreateNetEqStatus(NetEq::Mode last_mode,
                                               int current_delay_ms) {
  NetEqController::NetEqStatus status;
  status.play_dtmf = false;
  status.last_mode = last_mode;
  status.target_timestamp = 1234;
  status.generated_noise_samples = 0;
  status.packet_buffer_info.num_samples = current_delay_ms * kSamplesPerMs;
  status.packet_buffer_info.span_samples = current_delay_ms * kSamplesPerMs;
  status.packet_buffer_info.span_samples_no_dtx =
      current_delay_ms * kSamplesPerMs;
  status.next_packet = {status.target_timestamp, false, false};
  return status;
}

}  // namespace

using ::testing::Return;

class DecisionLogicTest : public ::testing::Test {
 protected:
  DecisionLogicTest() {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqDecisionLogicSettings/"
        "estimate_dtx_delay:true,time_stretch_cn:true/");

    NetEqController::Config config;
    config.tick_timer = &tick_timer_;
    config.allow_time_stretching = true;
    std::unique_ptr<Histogram> histogram =
        std::make_unique<Histogram>(200, 12345, 2);
    mock_delay_manager_ = new MockDelayManager(200, 0, 12300, config.tick_timer,
                                               std::move(histogram));
    mock_buffer_level_filter_ = new MockBufferLevelFilter();
    std::unique_ptr<DelayManager> delay_manager(mock_delay_manager_);
    std::unique_ptr<BufferLevelFilter> buffer_level_filter(
        mock_buffer_level_filter_);
    decision_logic_ = std::make_unique<DecisionLogic>(
        config, std::move(delay_manager), std::move(buffer_level_filter));
    decision_logic_->SetSampleRate(kSampleRate, kOutputSizeSamples);
  }

  TickTimer tick_timer_;
  std::unique_ptr<DecisionLogic> decision_logic_;
  MockDelayManager* mock_delay_manager_;
  MockBufferLevelFilter* mock_buffer_level_filter_;
};

TEST_F(DecisionLogicTest, NormalOperation) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(100));
  EXPECT_CALL(*mock_buffer_level_filter_, filtered_current_level())
      .WillRepeatedly(Return(90 * kSamplesPerMs));

  bool reset_decoder = false;
  tick_timer_.Increment(kMinTimescaleInterval + 1);
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kNormal, 100), &reset_decoder),
            NetEq::Operation::kNormal);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, Accelerate) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(100));
  EXPECT_CALL(*mock_buffer_level_filter_, filtered_current_level())
      .WillRepeatedly(Return(110 * kSamplesPerMs));

  bool reset_decoder = false;
  tick_timer_.Increment(kMinTimescaleInterval + 1);
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kNormal, 100), &reset_decoder),
            NetEq::Operation::kAccelerate);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, FastAccelerate) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(100));
  EXPECT_CALL(*mock_buffer_level_filter_, filtered_current_level())
      .WillRepeatedly(Return(400 * kSamplesPerMs));

  bool reset_decoder = false;
  tick_timer_.Increment(kMinTimescaleInterval + 1);
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kNormal, 100), &reset_decoder),
            NetEq::Operation::kFastAccelerate);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, PreemptiveExpand) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(100));
  EXPECT_CALL(*mock_buffer_level_filter_, filtered_current_level())
      .WillRepeatedly(Return(50 * kSamplesPerMs));

  bool reset_decoder = false;
  tick_timer_.Increment(kMinTimescaleInterval + 1);
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kNormal, 100), &reset_decoder),
            NetEq::Operation::kPreemptiveExpand);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, DecelerationTargetLevelOffset) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(500));
  EXPECT_CALL(*mock_buffer_level_filter_, filtered_current_level())
      .WillRepeatedly(Return(400 * kSamplesPerMs));

  bool reset_decoder = false;
  tick_timer_.Increment(kMinTimescaleInterval + 1);
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kNormal, 400), &reset_decoder),
            NetEq::Operation::kPreemptiveExpand);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, PostponeDecodeAfterExpand) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(500));

  // Below 50% target delay threshold.
  bool reset_decoder = false;
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kExpand, 200), &reset_decoder),
            NetEq::Operation::kExpand);
  EXPECT_FALSE(reset_decoder);

  // Above 50% target delay threshold.
  EXPECT_EQ(decision_logic_->GetDecision(
                CreateNetEqStatus(NetEq::Mode::kExpand, 250), &reset_decoder),
            NetEq::Operation::kNormal);
  EXPECT_FALSE(reset_decoder);
}

TEST_F(DecisionLogicTest, TimeStrechComfortNoise) {
  EXPECT_CALL(*mock_delay_manager_, TargetDelayMs())
      .WillRepeatedly(Return(500));

  {
    bool reset_decoder = false;
    // Below target window.
    auto status = CreateNetEqStatus(NetEq::Mode::kCodecInternalCng, 400);
    status.generated_noise_samples = 400 * kSamplesPerMs;
    status.next_packet->timestamp =
        status.target_timestamp + 400 * kSamplesPerMs;
    EXPECT_EQ(decision_logic_->GetDecision(status, &reset_decoder),
              NetEq::Operation::kCodecInternalCng);
    EXPECT_FALSE(reset_decoder);
  }

  {
    bool reset_decoder = false;
    // Below target window.
    auto status = CreateNetEqStatus(NetEq::Mode::kCodecInternalCng, 600);
    status.generated_noise_samples = 200 * kSamplesPerMs;
    status.next_packet->timestamp =
        status.target_timestamp + 400 * kSamplesPerMs;
    EXPECT_EQ(decision_logic_->GetDecision(status, &reset_decoder),
              NetEq::Operation::kNormal);
    EXPECT_FALSE(reset_decoder);
  }
}

}  // namespace webrtc
