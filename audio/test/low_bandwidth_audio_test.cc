/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/test/simulated_network.h"
#include "audio/test/audio_end_to_end_test.h"
#include "rtc_base/flags.h"
#include "system_wrappers/include/sleep.h"
#include "test/testsupport/file_utils.h"

WEBRTC_DECLARE_int(sample_rate_hz);
WEBRTC_DECLARE_bool(quick);

namespace webrtc {
namespace test {
namespace {

std::string FileSampleRateSuffix() {
  return std::to_string(FLAG_sample_rate_hz / 1000);
}

class AudioQualityTest : public AudioEndToEndTest {
 public:
  AudioQualityTest() = default;

 private:
  std::string AudioInputFile() const {
    return test::ResourcePath(
        "voice_engine/audio_tiny" + FileSampleRateSuffix(), "wav");
  }

  std::string AudioOutputFile() const {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    return webrtc::test::OutputPath() + "LowBandwidth_" + test_info->name() +
           "_" + FileSampleRateSuffix() + ".wav";
  }

  std::unique_ptr<TestAudioDeviceModule::Capturer> CreateCapturer() override {
    return TestAudioDeviceModule::CreateWavFileReader(AudioInputFile());
  }

  std::unique_ptr<TestAudioDeviceModule::Renderer> CreateRenderer() override {
    return TestAudioDeviceModule::CreateBoundedWavFileWriter(
        AudioOutputFile(), FLAG_sample_rate_hz);
  }

  void PerformTest() override {
    if (FLAG_quick) {
      // Let the recording run for a small amount of time to check if it works.
      SleepMs(1000);
    } else {
      AudioEndToEndTest::PerformTest();
    }
  }

  void OnStreamsStopped() override {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();

    // Output information about the input and output audio files so that further
    // processing can be done by an external process.
    printf("TEST %s %s %s\n", test_info->name(), AudioInputFile().c_str(),
           AudioOutputFile().c_str());
  }
};

class Mobile2GNetworkTest : public AudioQualityTest {
  void ModifyAudioConfigs(
      AudioSendStream::Config* send_config,
      std::vector<AudioReceiveStream::Config>* receive_configs) override {
    send_config->send_codec_spec = AudioSendStream::Config::SendCodecSpec(
        test::CallTest::kAudioSendPayloadType,
        {"OPUS",
         48000,
         2,
         {{"maxaveragebitrate", "6000"}, {"ptime", "60"}, {"stereo", "1"}}});
  }

  BuiltInNetworkBehaviorConfig GetNetworkPipeConfig() const override {
    BuiltInNetworkBehaviorConfig pipe_config;
    pipe_config.link_capacity_kbps = 12;
    pipe_config.queue_length_packets = 1500;
    pipe_config.queue_delay_ms = 400;
    return pipe_config;
  }
};
}  // namespace

using LowBandwidthAudioTest = CallTest;

TEST_F(LowBandwidthAudioTest, GoodNetworkHighBitrate) {
  AudioQualityTest test;
  RunBaseTest(&test);
}

TEST_F(LowBandwidthAudioTest, Mobile2GNetwork) {
  Mobile2GNetworkTest test;
  RunBaseTest(&test);
}
}  // namespace test
}  // namespace webrtc
