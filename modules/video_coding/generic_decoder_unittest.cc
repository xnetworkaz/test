/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/generic_decoder.h"

#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video_codecs/video_decoder.h"
#include "common_video/test/utilities.h"
#include "modules/video_coding/timing/timing.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"
#include "system_wrappers/include/clock.h"
#include "test/fake_decoder.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"

namespace webrtc {
namespace video_coding {

class ReceiveCallback : public VCMReceiveCallback {
 public:
  int32_t FrameToRender(VideoFrame& videoFrame,  // NOLINT
                        absl::optional<uint8_t> qp,
                        TimeDelta decode_time,
                        VideoContentType content_type) override {
    {
      MutexLock lock(&lock_);
      last_frame_ = videoFrame;
    }
    received_frame_event_.Set();
    return 0;
  }

  absl::optional<VideoFrame> GetLastFrame() {
    MutexLock lock(&lock_);
    return last_frame_;
  }

  absl::optional<VideoFrame> WaitForFrame(int64_t wait_ms) {
    if (received_frame_event_.Wait(wait_ms)) {
      MutexLock lock(&lock_);
      return last_frame_;
    } else {
      return absl::nullopt;
    }
  }

 private:
  Mutex lock_;
  rtc::Event received_frame_event_;
  absl::optional<VideoFrame> last_frame_ RTC_GUARDED_BY(lock_);
};

class GenericDecoderTest : public ::testing::Test {
 protected:
  GenericDecoderTest()
      : clock_(0),
        timing_(&clock_, field_trials_),
        task_queue_factory_(CreateDefaultTaskQueueFactory()),
        decoder_(task_queue_factory_.get()),
        vcm_callback_(&timing_, &clock_, field_trials_),
        generic_decoder_(&decoder_) {}

  void SetUp() override {
    generic_decoder_.RegisterDecodeCompleteCallback(&vcm_callback_);
    vcm_callback_.SetUserReceiveCallback(&user_callback_);
    VideoDecoder::Settings settings;
    settings.set_codec_type(kVideoCodecVP8);
    settings.set_max_render_resolution({10, 10});
    settings.set_number_of_cores(4);
    generic_decoder_.Configure(settings);
  }

  test::ScopedKeyValueConfig field_trials_;
  SimulatedClock clock_;
  VCMTiming timing_;
  std::unique_ptr<TaskQueueFactory> task_queue_factory_;
  webrtc::test::FakeDecoder decoder_;
  VCMDecodedFrameCallback vcm_callback_;
  VCMGenericDecoder generic_decoder_;
  ReceiveCallback user_callback_;
};

TEST_F(GenericDecoderTest, PassesPacketInfos) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  VCMEncodedFrame encoded_frame;
  encoded_frame.SetPacketInfos(packet_infos);
  generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

TEST_F(GenericDecoderTest, PassesPacketInfosForDelayedDecoders) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  decoder_.SetDelayedDecoding(100);

  {
    // Ensure the original frame is destroyed before the decoding is completed.
    VCMEncodedFrame encoded_frame;
    encoded_frame.SetPacketInfos(packet_infos);
    generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  }

  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(200);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

TEST_F(GenericDecoderTest, MaxCompositionDelayNotSetByDefault) {
  VCMEncodedFrame encoded_frame;
  generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_THAT(
      decoded_frame->render_parameters().max_composition_delay_in_frames,
      testing::Eq(absl::nullopt));
}

TEST_F(GenericDecoderTest, MaxCompositionDelayActivatedByPlayoutDelay) {
  VCMEncodedFrame encoded_frame;
  // VideoReceiveStream2 would set MaxCompositionDelayInFrames if playout delay
  // is specified as X,Y, where X=0, Y>0.
  constexpr int kMaxCompositionDelayInFrames = 3;  // ~50 ms at 60 fps.
  timing_.SetMaxCompositionDelayInFrames(
      absl::make_optional(kMaxCompositionDelayInFrames));
  generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_THAT(
      decoded_frame->render_parameters().max_composition_delay_in_frames,
      testing::Optional(kMaxCompositionDelayInFrames));
}

TEST_F(GenericDecoderTest, IsLowLatencyStreamFalseByDefault) {
  VCMEncodedFrame encoded_frame;
  generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_FALSE(decoded_frame->render_parameters().use_low_latency_rendering);
}

TEST_F(GenericDecoderTest, IsLowLatencyStreamActivatedByPlayoutDelay) {
  VCMEncodedFrame encoded_frame;
  const VideoPlayoutDelay kPlayoutDelay = {0, 50};
  timing_.set_min_playout_delay(TimeDelta::Millis(kPlayoutDelay.min_ms));
  timing_.set_max_playout_delay(TimeDelta::Millis(kPlayoutDelay.max_ms));
  generic_decoder_.Decode(encoded_frame, clock_.CurrentTime());
  absl::optional<VideoFrame> decoded_frame = user_callback_.WaitForFrame(10);
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_TRUE(decoded_frame->render_parameters().use_low_latency_rendering);
}

}  // namespace video_coding
}  // namespace webrtc
