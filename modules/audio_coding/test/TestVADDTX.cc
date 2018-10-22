/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/test/TestVADDTX.h"

#include <string>

#include "absl/strings/match.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/ilbc/audio_decoder_ilbc.h"
#include "api/audio_codecs/ilbc/audio_encoder_ilbc.h"
#include "api/audio_codecs/isac/audio_decoder_isac_float.h"
#include "api/audio_codecs/isac/audio_encoder_isac_float.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "modules/audio_coding/codecs/cng/audio_encoder_cng.h"
#include "modules/audio_coding/test/PCMFile.h"
#include "modules/audio_coding/test/utility.h"
#include "rtc_base/strings/string_builder.h"
#include "test/testsupport/fileutils.h"

namespace webrtc {

ActivityMonitor::ActivityMonitor() {
  ResetStatistics();
}

int32_t ActivityMonitor::InFrameType(FrameType frame_type) {
  counter_[frame_type]++;
  return 0;
}

void ActivityMonitor::PrintStatistics() {
  printf("\n");
  printf("kEmptyFrame       %u\n", counter_[kEmptyFrame]);
  printf("kAudioFrameSpeech %u\n", counter_[kAudioFrameSpeech]);
  printf("kAudioFrameCN     %u\n", counter_[kAudioFrameCN]);
  printf("kVideoFrameKey    %u\n", counter_[kVideoFrameKey]);
  printf("kVideoFrameDelta  %u\n", counter_[kVideoFrameDelta]);
  printf("\n\n");
}

void ActivityMonitor::ResetStatistics() {
  memset(counter_, 0, sizeof(counter_));
}

void ActivityMonitor::GetStatistics(uint32_t* counter) {
  memcpy(counter, counter_, sizeof(counter_));
}

TestVadDtx::TestVadDtx()
    : encoder_factory_(CreateAudioEncoderFactory<AudioEncoderIlbc,
                                                 AudioEncoderIsacFloat,
                                                 AudioEncoderOpus>()),
      decoder_factory_(CreateAudioDecoderFactory<AudioDecoderIlbc,
                                                 AudioDecoderIsacFloat,
                                                 AudioDecoderOpus>()),
      acm_send_(AudioCodingModule::Create(
          AudioCodingModule::Config(decoder_factory_))),
      acm_receive_(AudioCodingModule::Create(
          AudioCodingModule::Config(decoder_factory_))),
      channel_(new Channel),
      monitor_(new ActivityMonitor) {
  EXPECT_EQ(0, acm_send_->RegisterTransportCallback(channel_.get()));
  channel_->RegisterReceiverACM(acm_receive_.get());
  EXPECT_EQ(0, acm_send_->RegisterVADCallback(monitor_.get()));
}

bool TestVadDtx::RegisterCodec(const SdpAudioFormat& codec_format,
                               absl::optional<Vad::Aggressiveness> vad_mode) {
  constexpr int payload_type = 17, cn_payload_type = 117;
  bool added_comfort_noise = false;

  auto encoder = encoder_factory_->MakeAudioEncoder(payload_type, codec_format,
                                                    absl::nullopt);
  if (vad_mode.has_value() &&
      !absl::EqualsIgnoreCase(codec_format.name, "opus")) {
    AudioEncoderCng::Config config;
    config.speech_encoder = std::move(encoder);
    config.num_channels = 1;
    config.payload_type = cn_payload_type;
    config.vad_mode = vad_mode.value();
    encoder = absl::make_unique<AudioEncoderCng>(std::move(config));
    added_comfort_noise = true;
  }
  channel_->SetIsStereo(encoder->NumChannels() > 1);
  acm_send_->SetEncoder(std::move(encoder));

  EXPECT_EQ(true,
            acm_receive_->RegisterReceiveCodec(payload_type, codec_format));
  return added_comfort_noise;
}

// Encoding a file and see if the numbers that various packets occur follow
// the expectation.
void TestVadDtx::Run(std::string in_filename,
                     int frequency,
                     int channels,
                     std::string out_filename,
                     bool append,
                     const int* expects) {
  monitor_->ResetStatistics();

  PCMFile in_file;
  in_file.Open(in_filename, frequency, "rb");
  in_file.ReadStereo(channels > 1);
  // Set test length to 1000 ms (100 blocks of 10 ms each).
  in_file.SetNum10MsBlocksToRead(100);
  // Fast-forward both files 500 ms (50 blocks). The first second of the file is
  // silence, but we want to keep half of that to test silence periods.
  in_file.FastForward(50);

  PCMFile out_file;
  if (append) {
    out_file.Open(out_filename, kOutputFreqHz, "ab");
  } else {
    out_file.Open(out_filename, kOutputFreqHz, "wb");
  }

  uint16_t frame_size_samples = in_file.PayloadLength10Ms();
  AudioFrame audio_frame;
  while (!in_file.EndOfFile()) {
    in_file.Read10MsData(audio_frame);
    audio_frame.timestamp_ = time_stamp_;
    time_stamp_ += frame_size_samples;
    EXPECT_GE(acm_send_->Add10MsData(audio_frame), 0);
    bool muted;
    acm_receive_->PlayoutData10Ms(kOutputFreqHz, &audio_frame, &muted);
    ASSERT_FALSE(muted);
    out_file.Write10MsData(audio_frame);
  }

  in_file.Close();
  out_file.Close();

#ifdef PRINT_STAT
  monitor_->PrintStatistics();
#endif

  uint32_t stats[5];
  monitor_->GetStatistics(stats);
  monitor_->ResetStatistics();

  for (const auto& st : stats) {
    int i = &st - stats;  // Calculate the current position in stats.
    switch (expects[i]) {
      case 0: {
        EXPECT_EQ(0u, st) << "stats[" << i << "] error.";
        break;
      }
      case 1: {
        EXPECT_GT(st, 0u) << "stats[" << i << "] error.";
        break;
      }
    }
  }
}

// Following is the implementation of TestWebRtcVadDtx.
TestWebRtcVadDtx::TestWebRtcVadDtx() : output_file_num_(0) {}

void TestWebRtcVadDtx::Perform() {
  RunTestCases({"ISAC", 16000, 1});
  RunTestCases({"ISAC", 32000, 1});
  RunTestCases({"ILBC", 8000, 1});
  RunTestCases({"opus", 48000, 2});
}

// Test various configurations on VAD/DTX.
void TestWebRtcVadDtx::RunTestCases(const SdpAudioFormat& codec_format) {
  Test(/*new_outfile=*/true,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, absl::nullopt));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadAggressive));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadLowBitrate));

  Test(/*new_outfile=*/false, /*expect_dtx_enabled=*/RegisterCodec(
           codec_format, Vad::kVadVeryAggressive));

  Test(/*new_outfile=*/false,
       /*expect_dtx_enabled=*/RegisterCodec(codec_format, Vad::kVadNormal));
}

// Set the expectation and run the test.
void TestWebRtcVadDtx::Test(bool new_outfile, bool expect_dtx_enabled) {
  int expects[] = {-1, 1, expect_dtx_enabled, 0, 0};
  if (new_outfile) {
    output_file_num_++;
  }
  rtc::StringBuilder out_filename;
  out_filename << webrtc::test::OutputPath() << "testWebRtcVadDtx_outFile_"
               << output_file_num_ << ".pcm";
  Run(webrtc::test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename.str(), !new_outfile, expects);
}

// Following is the implementation of TestOpusDtx.
void TestOpusDtx::Perform() {
  // If we set other codec than Opus, DTX cannot be switched on.
  RegisterCodec({"ISAC", 16000, 1}, absl::nullopt);
  EXPECT_EQ(-1, acm_send_->EnableOpusDtx());
  EXPECT_EQ(0, acm_send_->DisableOpusDtx());

  int expects[] = {0, 1, 0, 0, 0};

  // Register Opus as send codec
  std::string out_filename =
      webrtc::test::OutputPath() + "testOpusDtx_outFile_mono.pcm";
  RegisterCodec({"opus", 48000, 2}, absl::nullopt);
  EXPECT_EQ(0, acm_send_->DisableOpusDtx());

  Run(webrtc::test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename, false, expects);

  EXPECT_EQ(0, acm_send_->EnableOpusDtx());
  expects[kEmptyFrame] = 1;
  expects[kAudioFrameCN] = 1;
  Run(webrtc::test::ResourcePath("audio_coding/testfile32kHz", "pcm"), 32000, 1,
      out_filename, true, expects);

  // Register stereo Opus as send codec
  out_filename = webrtc::test::OutputPath() + "testOpusDtx_outFile_stereo.pcm";
  RegisterCodec({"opus", 48000, 2, {{"stereo", "1"}}}, absl::nullopt);
  EXPECT_EQ(0, acm_send_->DisableOpusDtx());
  expects[kEmptyFrame] = 0;
  expects[kAudioFrameCN] = 0;
  Run(webrtc::test::ResourcePath("audio_coding/teststereo32kHz", "pcm"), 32000,
      2, out_filename, false, expects);

  EXPECT_EQ(0, acm_send_->EnableOpusDtx());

  expects[kEmptyFrame] = 1;
  expects[kAudioFrameCN] = 1;
  Run(webrtc::test::ResourcePath("audio_coding/teststereo32kHz", "pcm"), 32000,
      2, out_filename, true, expects);
}

}  // namespace webrtc
