/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_PC_E2E_ANALYZER_VIDEO_QUALITY_ANALYZING_VIDEO_ENCODER_H_
#define TEST_PC_E2E_ANALYZER_VIDEO_QUALITY_ANALYZING_VIDEO_ENCODER_H_

#include <list>
#include <memory>
#include <utility>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "rtc_base/critical_section.h"
#include "test/pc/e2e/analyzer/video/encoded_image_data_injector.h"
#include "test/pc/e2e/analyzer/video/id_generator.h"
#include "test/pc/e2e/api/video_quality_analyzer_interface.h"

namespace webrtc {
namespace test {

// QualityAnalyzingVideoEncoder is used to wrap origin video encoder and inject
// VideoQualityAnalyzerInterface before and after encoder.
//
// QualityAnalyzingVideoEncoder propagates all calls to the origin encoder.
// It registers its own EncodedImageCallback in the origin encoder and will
// store user specified callback inside itself.
//
// When Encode(...) will be invoked, quality encoder first calls video quality
// analyzer with original frame, then encodes frame with original encoder.
//
// When origin encoder encodes the image it will call quality encoder's special
// callback, where video analyzer will be called again and then frame id will be
// injected into EncodedImage with passed EncodedImageDataInjector. Then new
// EncodedImage will be passed to origin callback, provided by user.
//
// Quality encoder registers its own callback in origin encoder at the same
// time, when user registers his callback in quality encoder.
class QualityAnalyzingVideoEncoder : public VideoEncoder,
                                     public EncodedImageCallback {
 public:
  // Creates analyzing encoder. |id| is unique coding entity id, that will
  // be used to distinguish all encoders and decoders inside
  // EncodedImageDataInjector and EncodedImageIdExtracor.
  QualityAnalyzingVideoEncoder(
      int id,
      std::unique_ptr<VideoEncoder> delegate,
      std::map<std::string, absl::optional<int>> stream_required_spatial_index,
      EncodedImageDataInjector* injector,
      VideoQualityAnalyzerInterface* analyzer);
  ~QualityAnalyzingVideoEncoder() override;

  // Methods of VideoEncoder interface.
  int32_t InitEncode(const VideoCodec* codec_settings,
                     int32_t number_of_cores,
                     size_t max_payload_size) override;
  int32_t RegisterEncodeCompleteCallback(
      EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(const VideoFrame& frame,
                 const std::vector<FrameType>* frame_types) override;
  int32_t SetRates(uint32_t bitrate, uint32_t framerate) override;
  int32_t SetRateAllocation(const VideoBitrateAllocation& allocation,
                            uint32_t framerate) override;
  EncoderInfo GetEncoderInfo() const override;

  // Methods of EncodedImageCallback interface.
  EncodedImageCallback::Result OnEncodedImage(
      const EncodedImage& encoded_image,
      const CodecSpecificInfo* codec_specific_info,
      const RTPFragmentationHeader* fragmentation) override;
  void OnDroppedFrame(DropReason reason) override;

 private:
  enum SimulcastMode {
    // In this mode encoder assumes not more than 1 encoded image per video
    // frame
    kNormal,

    // Next modes are to test video conference behavior. For conference sender
    // will send multiple spatial layers/simulcast streams for single video
    // track and there is some Selective Forwarding Unit (SFU), that forwards
    // only best one, that will pass through downlink to the receiver.
    //
    // Here this behavior will be partly emulated. Sender will send all spatial
    // layers/simulcast streams and then some of them will be filtered out on
    // the receiver side. During test setup user can specify which spatial
    // layer/simulcast stream is required, what will simulated which spatial
    // layer/simulcast stream will be chosen by SFU in the real world. Then
    // sender will mark encoded images for all spatial layers above required or
    // all simulcast streams except required as to be discarded and on receiver
    // side they will be discarded in quality analyzing decoder and won't be
    // passed into delegate decoder.
    //
    // If the sender for some reasons won't send specified spatial layer, then
    // receiver still will fall back on lower spatial layers. But for simulcast
    // streams if required one won't be sent, receiver will assume all frames
    // in that period as dropped and will experience video freeze.
    //
    // Test based on this simulation will be used to evaluate video quality
    // of concrete spatial layers/simulcast streams and also check distribution
    // of bandwidth between spatial layers/simulcast streams by BWE.

    // In this mode encoder assumes that for each frame simulcast encoded
    // images will be produced. So all simulcast streams except required will
    // be marked as to be discarded in decoder and won't reach video quality
    // analyzer.
    kSimulcast,
    // In this mode encoder assumes that for each frame encoded images for
    // different spatial layers will be produced. So all spatial layers above
    // required will be marked to be discarded in decoder and won't reach
    // video quality analyzer.
    kSVC,
    // In this mode encoder assumes that for each frame encoded images for
    // different spatial layers will be produced. Compared to kSVC mode
    // spatial layers that are above required will be marked to be discarded
    // only for key frames and for regular frames all except required spatial
    // layer will be marked as to be discarded in decoder and won't reach video
    // quality analyzer.
    kKSVC
  };

  bool ShouldDiscard(uint16_t frame_id, const EncodedImage& encoded_image)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  const int id_;
  std::unique_ptr<VideoEncoder> delegate_;
  std::map<std::string, absl::optional<int>> stream_required_spatial_index_;
  EncodedImageDataInjector* const injector_;
  VideoQualityAnalyzerInterface* const analyzer_;

  // VideoEncoder interface assumes async delivery of encoded images.
  // This lock is used to protect shared state, that have to be propagated
  // from received VideoFrame to resulted EncodedImage.
  rtc::CriticalSection lock_;

  SimulcastMode mode_ RTC_GUARDED_BY(lock_);
  EncodedImageCallback* delegate_callback_ RTC_GUARDED_BY(lock_);
  std::list<std::pair<uint32_t, uint16_t>> timestamp_to_frame_id_list_
      RTC_GUARDED_BY(lock_);
};

// Produces QualityAnalyzingVideoEncoder, which hold decoders, produced by
// specified factory as delegates. Forwards all other calls to specified
// factory.
class QualityAnalyzingVideoEncoderFactory : public VideoEncoderFactory {
 public:
  QualityAnalyzingVideoEncoderFactory(
      std::unique_ptr<VideoEncoderFactory> delegate,
      std::map<std::string, absl::optional<int>> stream_required_spatial_index,
      IdGenerator<int>* id_generator,
      EncodedImageDataInjector* injector,
      VideoQualityAnalyzerInterface* analyzer);
  ~QualityAnalyzingVideoEncoderFactory() override;

  // Methods of VideoEncoderFactory interface.
  std::vector<SdpVideoFormat> GetSupportedFormats() const override;
  VideoEncoderFactory::CodecInfo QueryVideoEncoder(
      const SdpVideoFormat& format) const override;
  std::unique_ptr<VideoEncoder> CreateVideoEncoder(
      const SdpVideoFormat& format) override;

 private:
  std::unique_ptr<VideoEncoderFactory> delegate_;
  std::map<std::string, absl::optional<int>> stream_required_spatial_index_;
  IdGenerator<int>* const id_generator_;
  EncodedImageDataInjector* const injector_;
  VideoQualityAnalyzerInterface* const analyzer_;
};

}  // namespace test
}  // namespace webrtc

#endif  // TEST_PC_E2E_ANALYZER_VIDEO_QUALITY_ANALYZING_VIDEO_ENCODER_H_
