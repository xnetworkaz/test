/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>

#include "api/test/video/function_video_encoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "test/call_test.h"
#include "test/field_trial.h"
#include "test/frame_generator_capturer.h"

namespace webrtc {
namespace {
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kLowStartBps = 100000;
constexpr int kHighStartBps = 600000;
constexpr size_t kTimeoutMs = 10000;  // Some tests are expected to time out.

void SetEncoderSpecific(VideoEncoderConfig* encoder_config,
                        VideoCodecType type,
                        bool automatic_resize) {
  if (type == kVideoCodecVP8) {
    VideoCodecVP8 vp8 = VideoEncoder::GetDefaultVp8Settings();
    vp8.automaticResizeOn = automatic_resize;
    encoder_config->encoder_specific_settings = new rtc::RefCountedObject<
        VideoEncoderConfig::Vp8EncoderSpecificSettings>(vp8);
  } else if (type == kVideoCodecVP9) {
    VideoCodecVP9 vp9 = VideoEncoder::GetDefaultVp9Settings();
    vp9.automaticResizeOn = automatic_resize;
    encoder_config->encoder_specific_settings = new rtc::RefCountedObject<
        VideoEncoderConfig::Vp9EncoderSpecificSettings>(vp9);
  }
}
}  // namespace

class QualityScalingTest : public test::CallTest {
 protected:
  void RunTest(VideoEncoderFactory* encoder_factory,
               const std::string& payload_name,
               const std::vector<bool>& streams_active,
               int start_bps,
               bool automatic_resize,
               bool expect_adaptation);

  const std::string kPrefix = "WebRTC-Video-QualityScaling/Enabled-";
  const std::string kEnd = ",0,0,0.9995,0.9999,1/";
};

void QualityScalingTest::RunTest(VideoEncoderFactory* encoder_factory,
                                 const std::string& payload_name,
                                 const std::vector<bool>& streams_active,
                                 int start_bps,
                                 bool automatic_resize,
                                 bool expect_adaptation) {
  class ScalingObserver
      : public test::SendTest,
        public test::FrameGeneratorCapturer::SinkWantsObserver {
   public:
    ScalingObserver(VideoEncoderFactory* encoder_factory,
                    const std::string& payload_name,
                    const std::vector<bool>& streams_active,
                    int start_bps,
                    bool automatic_resize,
                    bool expect_adaptation)
        : SendTest(expect_adaptation ? kDefaultTimeoutMs : kTimeoutMs),
          encoder_factory_(encoder_factory),
          payload_name_(payload_name),
          streams_active_(streams_active),
          start_bps_(start_bps),
          automatic_resize_(automatic_resize),
          expect_adaptation_(expect_adaptation) {}

   private:
    void OnFrameGeneratorCapturerCreated(
        test::FrameGeneratorCapturer* frame_generator_capturer) override {
      frame_generator_capturer->SetSinkWantsObserver(this);
      // Set initial resolution.
      frame_generator_capturer->ChangeResolution(kWidth, kHeight);
    }

    // Called when FrameGeneratorCapturer::AddOrUpdateSink is called.
    void OnSinkWantsChanged(rtc::VideoSinkInterface<VideoFrame>* sink,
                            const rtc::VideoSinkWants& wants) override {
      if (wants.max_pixel_count < kWidth * kHeight)
        observation_complete_.Set();
    }
    void ModifySenderBitrateConfig(
        BitrateConstraints* bitrate_config) override {
      bitrate_config->start_bitrate_bps = start_bps_;
    }

    size_t GetNumVideoStreams() const override {
      return streams_active_.size();
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStream::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->encoder_settings.encoder_factory = encoder_factory_;
      send_config->rtp.payload_name = payload_name_;
      send_config->rtp.payload_type = kVideoSendPayloadType;
      const VideoCodecType codec_type = PayloadStringToCodecType(payload_name_);
      encoder_config->codec_type = codec_type;
      encoder_config->max_bitrate_bps =
          std::max(start_bps_, encoder_config->max_bitrate_bps);
      double scale_factor = 1.0;
      for (int i = streams_active_.size() - 1; i >= 0; --i) {
        VideoStream& stream = encoder_config->simulcast_layers[i];
        stream.active = streams_active_[i];
        stream.scale_resolution_down_by = scale_factor;
        scale_factor *= 2.0;
      }
      SetEncoderSpecific(encoder_config, codec_type, automatic_resize_);
    }

    void PerformTest() override {
      EXPECT_EQ(expect_adaptation_, Wait())
          << "Timed out while waiting for a scale down.";
    }

    VideoEncoderFactory* const encoder_factory_;
    const std::string payload_name_;
    const std::vector<bool> streams_active_;
    const int start_bps_;
    const bool automatic_resize_;
    const bool expect_adaptation_;
  } test(encoder_factory, payload_name, streams_active, start_bps,
         automatic_resize, expect_adaptation);

  RunBaseTest(&test);
}

TEST_F(QualityScalingTest, AdaptsDownForHighQp_Vp8) {
  // qp_low:1, qp_high:1 -> kHighQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,1,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true}, kHighStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/true);
}

TEST_F(QualityScalingTest, NoAdaptDownForHighQpWithResizeOff_Vp8) {
  // qp_low:1, qp_high:1 -> kHighQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,1,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true}, kHighStartBps,
          /*automatic_resize=*/false, /*expect_adaptation=*/false);
}

TEST_F(QualityScalingTest, NoAdaptDownForNormalQp_Vp8) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true}, kHighStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/false);
}

TEST_F(QualityScalingTest, AdaptsDownForLowStartBitrate) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true}, kLowStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/true);
}

TEST_F(QualityScalingTest, NoAdaptDownForLowStartBitrate_Simulcast) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true, true}, kLowStartBps,
          /*automatic_resize=*/false, /*expect_adaptation=*/false);
}

TEST_F(QualityScalingTest,
       AdaptsDownForLowStartBitrate_SimulcastOneActiveHighRes) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {false, false, true}, kLowStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/true);
}

TEST_F(QualityScalingTest,
       NoAdaptDownForLowStartBitrate_SimulcastOneActiveLowRes) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true, false, false}, kLowStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/false);
}

TEST_F(QualityScalingTest, NoAdaptDownForLowStartBitrateWithScalingOff) {
  // qp_low:1, qp_high:127 -> kNormalQp
  test::ScopedFieldTrials field_trials(kPrefix + "1,127,0,0,0,0" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP8Encoder::Create(); });
  RunTest(&encoder_factory, "VP8", {true}, kLowStartBps,
          /*automatic_resize=*/false, /*expect_adaptation=*/false);
}

TEST_F(QualityScalingTest, NoAdaptDownForHighQp_Vp9) {
  // qp_low:1, qp_high:1 -> kHighQp
  test::ScopedFieldTrials field_trials(kPrefix + "0,0,1,1,0,0" + kEnd +
                                       "WebRTC-VP9QualityScaler/Disabled/");

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return VP9Encoder::Create(); });
  RunTest(&encoder_factory, "VP9", {true}, kHighStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/false);
}

#if defined(WEBRTC_USE_H264)
TEST_F(QualityScalingTest, AdaptsDownForHighQp_H264) {
  // qp_low:1, qp_high:1 -> kHighQp
  test::ScopedFieldTrials field_trials(kPrefix + "0,0,0,0,1,1" + kEnd);

  test::FunctionVideoEncoderFactory encoder_factory(
      []() { return H264Encoder::Create(cricket::VideoCodec("H264")); });
  RunTest(&encoder_factory, "H264", {true}, kHighStartBps,
          /*automatic_resize=*/true, /*expect_adaptation=*/true);
}
#endif  // defined(WEBRTC_USE_H264)

}  // namespace webrtc
