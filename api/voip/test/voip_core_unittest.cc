//
//  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
//
//  Use of this source code is governed by a BSD-style license
//  that can be found in the LICENSE file in the root of the source
//  tree. An additional intellectual property rights grant can be found
//  in the file PATENTS.  All contributing project authors may
//  be found in the AUTHORS file in the root of the source tree.
//

#include "api/voip/voip_core.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "rtc_base/logging.h"
#include "test/gtest.h"
#include "test/mock_transport.h"

namespace webrtc {

using ::testing::NiceMock;
using ::testing::Return;

namespace {

constexpr int kPcmuPayload = 0;
const SdpAudioFormat kPcmuFormat = {"pcmu", 8000, 1};

}  // namespace

class VoipCoreTest : public ::testing::Test {
 public:
  VoipCoreTest() {
    encoder_factory_ = CreateBuiltinAudioEncoderFactory();
    decoder_factory_ = CreateBuiltinAudioDecoderFactory();
    audio_device_ = test::MockAudioDeviceModule::CreateNice();
  }

  void SetUp() override {
    std::unique_ptr<AudioProcessing> audio_processing(
        new rtc::RefCountedObject<test::MockAudioProcessing>());
    voip_core_ = std::make_unique<VoipCore>();
    voip_core_->Init(CreateDefaultTaskQueueFactory(),
                     std::move(audio_processing), audio_device_,
                     encoder_factory_, decoder_factory_);
  }

  std::unique_ptr<VoipCore> voip_core_;
  NiceMock<MockTransport> transport_;
  rtc::scoped_refptr<test::MockAudioDeviceModule> audio_device_;
  rtc::scoped_refptr<AudioEncoderFactory> encoder_factory_;
  rtc::scoped_refptr<AudioDecoderFactory> decoder_factory_;
};

TEST_F(VoipCoreTest, BasicVoipCoreOperation) {
  // Program mock as non-operational and ready to start
  EXPECT_CALL(*audio_device_, Recording()).WillOnce(Return(false));
  EXPECT_CALL(*audio_device_, Playing()).WillOnce(Return(false));
  EXPECT_CALL(*audio_device_, InitRecording()).WillOnce(Return(0));
  EXPECT_CALL(*audio_device_, InitPlayout()).WillOnce(Return(0));
  EXPECT_CALL(*audio_device_, StartRecording()).WillOnce(Return(0));
  EXPECT_CALL(*audio_device_, StartPlayout()).WillOnce(Return(0));

  int channel = voip_core_->CreateChannel();
  ASSERT_NE(channel, -1);

  EXPECT_TRUE(voip_core_->RegisterTransport(channel, &transport_));
  EXPECT_TRUE(voip_core_->SetSendCodec(channel, kPcmuPayload, kPcmuFormat));
  EXPECT_TRUE(
      voip_core_->SetReceiveCodecs(channel, {{kPcmuPayload, kPcmuFormat}}));

  EXPECT_TRUE(voip_core_->StartSend(channel));
  EXPECT_TRUE(voip_core_->StartPlayout(channel));

  // Program mock as operational that is ready to be stopped
  EXPECT_CALL(*audio_device_, Recording()).WillOnce(Return(true));
  EXPECT_CALL(*audio_device_, Playing()).WillOnce(Return(true));
  EXPECT_CALL(*audio_device_, StopRecording()).WillOnce(Return(0));
  EXPECT_CALL(*audio_device_, StopPlayout()).WillOnce(Return(0));

  EXPECT_TRUE(voip_core_->StopSend(channel));
  EXPECT_TRUE(voip_core_->StopPlayout(channel));

  EXPECT_TRUE(voip_core_->DeRegisterTransport(channel));
  EXPECT_TRUE(voip_core_->ReleaseChannel(channel));

  // Try to invoke on released channel
  EXPECT_FALSE(voip_core_->RegisterTransport(channel, &transport_));
}

}  // namespace webrtc
