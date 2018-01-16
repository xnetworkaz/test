/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/test/videoprocessor_integrationtest.h"

#include <vector>

#include "modules/video_coding/codecs/test/test_config.h"
#include "rtc_base/ptr_util.h"
#include "test/testsupport/fileutils.h"

namespace webrtc {
namespace test {

namespace {

// Codec settings.
const bool kResilienceOn = true;
const int kCifWidth = 352;
const int kCifHeight = 288;
#if !defined(WEBRTC_IOS)
const int kNumFramesShort = 100;
#endif
const int kNumFramesLong = 300;

const std::nullptr_t kNoVisualizationParams = nullptr;

}  // namespace

class VideoProcessorIntegrationTestLibvpx
    : public VideoProcessorIntegrationTest {
 protected:
  VideoProcessorIntegrationTestLibvpx() {
    config_.filename = "foreman_cif";
    config_.input_filename = ResourcePath(config_.filename, "yuv");
    config_.output_filename =
        TempFilename(OutputPath(), "videoprocessor_integrationtest_libvpx");
    config_.num_frames = kNumFramesLong;
    // Only allow encoder/decoder to use single core, for predictability.
    config_.use_single_core = true;
    config_.hw_encoder = false;
    config_.hw_decoder = false;
    config_.encoded_frame_checker = &qp_frame_checker_;
  }

 private:
  // Verify that the QP parser returns the same QP as the encoder does.
  const class QpFrameChecker : public TestConfig::EncodedFrameChecker {
   public:
    void CheckEncodedFrame(webrtc::VideoCodecType codec,
                           const EncodedImage& encoded_frame) const override {
      int qp;
      if (codec == kVideoCodecVP8) {
        EXPECT_TRUE(
            vp8::GetQp(encoded_frame._buffer, encoded_frame._length, &qp));
      } else if (codec == kVideoCodecVP9) {
        EXPECT_TRUE(
            vp9::GetQp(encoded_frame._buffer, encoded_frame._length, &qp));
      } else {
        RTC_NOTREACHED();
      }
      EXPECT_EQ(encoded_frame.qp_, qp) << "Encoder QP != parsed bitstream QP.";
    }
  } qp_frame_checker_;
};

// Fails on iOS. See webrtc:4755.
#if !defined(WEBRTC_IOS)

#if !defined(RTC_DISABLE_VP9)
TEST_F(VideoProcessorIntegrationTestLibvpx, HighBitrateVP9) {
  config_.SetCodecSettings(kVideoCodecVP9, 1, false, false, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);
  config_.num_frames = kNumFramesShort;

  std::vector<RateProfile> rate_profiles = {{500, 30, kNumFramesShort}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.3, 0.1, 0, 1}};

  std::vector<QualityThresholds> quality_thresholds = {{37, 36, 0.94, 0.92}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

TEST_F(VideoProcessorIntegrationTestLibvpx, ChangeBitrateVP9) {
  config_.SetCodecSettings(kVideoCodecVP9, 1, false, false, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {
      {200, 30, 100},  // target_kbps, input_fps, frame_index_rate_update
      {700, 30, 200},
      {500, 30, kNumFramesLong}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.5, 0.1, 0, 1},
      {15, 2, 0, 0.2, 0.5, 0.1, 0, 0},
      {10, 1, 0, 0.3, 0.5, 0.1, 0, 0}};

  std::vector<QualityThresholds> quality_thresholds = {
      {35, 33, 0.90, 0.88}, {38, 35, 0.95, 0.91}, {36, 34, 0.93, 0.90}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

TEST_F(VideoProcessorIntegrationTestLibvpx, ChangeFramerateVP9) {
  config_.SetCodecSettings(kVideoCodecVP9, 1, false, false, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {
      {100, 24, 100},  // target_kbps, input_fps, frame_index_rate_update
      {100, 15, 200},
      {100, 10, kNumFramesLong}};

  // Framerate mismatch should be lower for lower framerate.
  std::vector<RateControlThresholds> rc_thresholds = {
      {10, 2, 20, 0.4, 0.5, 0.2, 0, 1},
      {8, 2, 5, 0.2, 0.5, 0.2, 0, 0},
      {5, 2, 0, 0.2, 0.5, 0.3, 0, 0}};

  // Quality should be higher for lower framerates for the same content.
  std::vector<QualityThresholds> quality_thresholds = {
      {33, 32, 0.89, 0.87}, {34, 32, 0.90, 0.87}, {34, 32, 0.90, 0.87}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

TEST_F(VideoProcessorIntegrationTestLibvpx, DenoiserOnVP9) {
  config_.SetCodecSettings(kVideoCodecVP9, 1, false, true, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);
  config_.num_frames = kNumFramesShort;

  std::vector<RateProfile> rate_profiles = {{500, 30, kNumFramesShort}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.3, 0.1, 0, 1}};

  std::vector<QualityThresholds> quality_thresholds = {{38, 36, 0.95, 0.94}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

TEST_F(VideoProcessorIntegrationTestLibvpx, VeryLowBitrateVP9) {
  config_.SetCodecSettings(kVideoCodecVP9, 1, false, false, true, true,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {{50, 30, kNumFramesLong}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {15, 3, 70, 0.8, 0.5, 0.3, 1, 1}};

  std::vector<QualityThresholds> quality_thresholds = {{28, 25, 0.80, 0.65}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

// TODO(marpan): Add temporal layer test for VP9, once changes are in
// vp9 wrapper for this.

#endif  // !defined(RTC_DISABLE_VP9)

TEST_F(VideoProcessorIntegrationTestLibvpx, HighBitrateVP8) {
  config_.SetCodecSettings(kVideoCodecVP8, 1, false, true, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);
  config_.num_frames = kNumFramesShort;

  std::vector<RateProfile> rate_profiles = {{500, 30, kNumFramesShort}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.2, 0.1, 0, 1}};

  std::vector<QualityThresholds> quality_thresholds = {{37, 35, 0.93, 0.91}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

#endif  // !defined(WEBRTC_IOS)

// The tests below are currently disabled for Android. For ARM, the encoder
// uses |cpu_speed| = 12, as opposed to default |cpu_speed| <= 6 for x86,
// which leads to significantly different quality. The quality and rate control
// settings in the tests below are defined for encoder speed setting
// |cpu_speed| <= ~6. A number of settings would need to be significantly
// modified for the |cpu_speed| = 12 case. For now, keep the tests below
// disabled on Android. Some quality parameter in the above test has been
// adjusted to also pass for |cpu_speed| <= 12.

// Too slow to finish before timeout on iOS. See webrtc:4755.
#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS)
#define MAYBE_ProcessNoLossChangeBitRateVP8 \
  DISABLED_ProcessNoLossChangeBitRateVP8
#else
#define MAYBE_ProcessNoLossChangeBitRateVP8 ProcessNoLossChangeBitRateVP8
#endif
TEST_F(VideoProcessorIntegrationTestLibvpx, MAYBE_ChangeBitrateVP8) {
  config_.SetCodecSettings(kVideoCodecVP8, 1, false, true, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {
      {200, 30, 100},  // target_kbps, input_fps, frame_index_rate_update
      {800, 30, 200},
      {500, 30, kNumFramesLong}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.2, 0.1, 0, 1},
      {15, 1, 0, 0.1, 0.2, 0.1, 0, 0},
      {15, 1, 0, 0.3, 0.2, 0.1, 0, 0}};

  std::vector<QualityThresholds> quality_thresholds = {
      {33, 32, 0.89, 0.88}, {38, 36, 0.94, 0.93}, {35, 34, 0.92, 0.91}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

// Too slow to finish before timeout on iOS. See webrtc:4755.
#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS)
#define MAYBE_ProcessNoLossChangeFrameRateFrameDropVP8 \
  DISABLED_ProcessNoLossChangeFrameRateFrameDropVP8
#else
#define MAYBE_ProcessNoLossChangeFrameRateFrameDropVP8 \
  ProcessNoLossChangeFrameRateFrameDropVP8
#endif
TEST_F(VideoProcessorIntegrationTestLibvpx, MAYBE_ChangeFramerateVP8) {
  config_.SetCodecSettings(kVideoCodecVP8, 1, false, true, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {
      {80, 24, 100},  // target_kbps, input_fps, frame_index_rate_update
      {80, 15, 200},
      {80, 10, kNumFramesLong}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {10, 2, 20, 0.4, 0.3, 0.1, 0, 1},
      {5, 2, 5, 0.3, 0.3, 0.1, 0, 0},
      {4, 2, 1, 0.2, 0.3, 0.2, 0, 0}};

  std::vector<QualityThresholds> quality_thresholds = {
      {31, 30, 0.87, 0.86}, {32, 31, 0.89, 0.86}, {32, 30, 0.87, 0.82}};

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

// Too slow to finish before timeout on iOS. See webrtc:4755.
#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS)
#define MAYBE_ProcessNoLossTemporalLayersVP8 \
  DISABLED_ProcessNoLossTemporalLayersVP8
#else
#define MAYBE_ProcessNoLossTemporalLayersVP8 ProcessNoLossTemporalLayersVP8
#endif
TEST_F(VideoProcessorIntegrationTestLibvpx, MAYBE_TemporalLayersVP8) {
  config_.SetCodecSettings(kVideoCodecVP8, 3, false, true, true, false,
                           kResilienceOn, kCifWidth, kCifHeight);

  std::vector<RateProfile> rate_profiles = {{200, 30, 150},
                                            {400, 30, kNumFramesLong}};

  std::vector<RateControlThresholds> rc_thresholds = {
      {5, 1, 0, 0.1, 0.2, 0.1, 0, 1}, {10, 2, 0, 0.1, 0.2, 0.1, 0, 1}};

  std::vector<QualityThresholds> quality_thresholds = {{32, 30, 0.88, 0.85},
                                                       {33, 30, 0.89, 0.83}};
  // Min SSIM drops because of high motion scene with complex backgound (trees).

  ProcessFramesAndMaybeVerify(rate_profiles, &rc_thresholds,
                              &quality_thresholds, nullptr,
                              kNoVisualizationParams);
}

}  // namespace test
}  // namespace webrtc
