/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/test/videoprocessor.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "api/video/i420_buffer.h"
#include "common_types.h"  // NOLINT(build/include)
#include "common_video/h264/h264_common.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/video_coding/codecs/vp8/simulcast_rate_allocator.h"
#include "modules/video_coding/include/video_codec_initializer.h"
#include "modules/video_coding/utility/default_video_bitrate_allocator.h"
#include "rtc_base/checks.h"
#include "rtc_base/timeutils.h"
#include "test/gtest.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace webrtc {
namespace test {

namespace {

const int kMsToRtpTimestamp = kVideoPayloadTypeFrequency / 1000;
const int kMaxBufferedInputFrames = 10;

std::unique_ptr<VideoBitrateAllocator> CreateBitrateAllocator(
    TestConfig* config) {
  std::unique_ptr<TemporalLayersFactory> tl_factory;
  if (config->codec_settings.codecType == VideoCodecType::kVideoCodecVP8) {
    tl_factory.reset(new TemporalLayersFactory());
    config->codec_settings.VP8()->tl_factory = tl_factory.get();
  }
  return std::unique_ptr<VideoBitrateAllocator>(
      VideoCodecInitializer::CreateBitrateAllocator(config->codec_settings,
                                                    std::move(tl_factory)));
}

size_t GetMaxNaluSizeBytes(const EncodedImage& encoded_frame,
                           const TestConfig& config) {
  if (config.codec_settings.codecType != kVideoCodecH264)
    return 0;

  std::vector<webrtc::H264::NaluIndex> nalu_indices =
      webrtc::H264::FindNaluIndices(encoded_frame._buffer,
                                    encoded_frame._length);

  RTC_CHECK(!nalu_indices.empty());

  size_t max_size = 0;
  for (const webrtc::H264::NaluIndex& index : nalu_indices)
    max_size = std::max(max_size, index.payload_size);

  return max_size;
}

void GetLayerIndices(const CodecSpecificInfo& codec_specific,
                     size_t* simulcast_svc_idx,
                     size_t* temporal_idx) {
  if (codec_specific.codecType == kVideoCodecVP8) {
    *simulcast_svc_idx = codec_specific.codecSpecific.VP8.simulcastIdx;
    *temporal_idx = codec_specific.codecSpecific.VP8.temporalIdx;
  } else if (codec_specific.codecType == kVideoCodecVP9) {
    *simulcast_svc_idx = codec_specific.codecSpecific.VP9.spatial_idx;
    *temporal_idx = codec_specific.codecSpecific.VP9.temporal_idx;
  }
  if (*simulcast_svc_idx == kNoSpatialIdx) {
    *simulcast_svc_idx = 0;
  }
  if (*temporal_idx == kNoTemporalIdx) {
    *temporal_idx = 0;
  }
}

int GetElapsedTimeMicroseconds(int64_t start_ns, int64_t stop_ns) {
  int64_t diff_us = (stop_ns - start_ns) / rtc::kNumNanosecsPerMicrosec;
  RTC_DCHECK_GE(diff_us, std::numeric_limits<int>::min());
  RTC_DCHECK_LE(diff_us, std::numeric_limits<int>::max());
  return static_cast<int>(diff_us);
}

void ExtractI420BufferWithSize(const VideoFrame& image,
                               int width,
                               int height,
                               rtc::Buffer* buffer) {
  if (image.width() != width || image.height() != height) {
    EXPECT_DOUBLE_EQ(static_cast<double>(width) / height,
                     static_cast<double>(image.width()) / image.height());
    // Same aspect ratio, no cropping needed.
    rtc::scoped_refptr<I420Buffer> scaled(I420Buffer::Create(width, height));
    scaled->ScaleFrom(*image.video_frame_buffer()->ToI420());

    size_t length =
        CalcBufferSize(VideoType::kI420, scaled->width(), scaled->height());
    buffer->SetSize(length);
    RTC_CHECK_NE(ExtractBuffer(scaled, length, buffer->data()), -1);
    return;
  }

  // No resize.
  size_t length =
      CalcBufferSize(VideoType::kI420, image.width(), image.height());
  buffer->SetSize(length);
  RTC_CHECK_NE(ExtractBuffer(image, length, buffer->data()), -1);
}

void CalculateFrameQuality(const VideoFrame& ref_frame,
                           const VideoFrame& dec_frame,
                           FrameStatistics* frame_stat) {
  if (ref_frame.width() == dec_frame.width() ||
      ref_frame.height() == dec_frame.height()) {
    frame_stat->psnr = I420PSNR(&ref_frame, &dec_frame);
    frame_stat->ssim = I420SSIM(&ref_frame, &dec_frame);
  } else {
    RTC_CHECK_GE(ref_frame.width(), dec_frame.width());
    RTC_CHECK_GE(ref_frame.height(), dec_frame.height());
    // Downscale reference frame. Use bilinear interpolation since it is used
    // to get lowres inputs for encoder at simulcasting.
    // TODO(ssilkin): Sync with VP9 SVC which uses 8-taps polyphase.
    rtc::scoped_refptr<I420Buffer> scaled_buffer =
        I420Buffer::Create(dec_frame.width(), dec_frame.height());
    const I420BufferInterface& ref_buffer =
        *ref_frame.video_frame_buffer()->ToI420();
    I420Scale(ref_buffer.DataY(), ref_buffer.StrideY(), ref_buffer.DataU(),
              ref_buffer.StrideU(), ref_buffer.DataV(), ref_buffer.StrideV(),
              ref_buffer.width(), ref_buffer.height(),
              scaled_buffer->MutableDataY(), scaled_buffer->StrideY(),
              scaled_buffer->MutableDataU(), scaled_buffer->StrideU(),
              scaled_buffer->MutableDataV(), scaled_buffer->StrideV(),
              scaled_buffer->width(), scaled_buffer->height(),
              libyuv::kFilterBox);
    frame_stat->psnr =
        I420PSNR(*scaled_buffer, *dec_frame.video_frame_buffer()->ToI420());
    frame_stat->ssim =
        I420SSIM(*scaled_buffer, *dec_frame.video_frame_buffer()->ToI420());
  }
}

}  // namespace

VideoProcessor::VideoProcessor(webrtc::VideoEncoder* encoder,
                               VideoDecoderList* decoders,
                               FrameReader* input_frame_reader,
                               const TestConfig& config,
                               Stats* stats,
                               IvfFileWriterList* encoded_frame_writers,
                               FrameWriterList* decoded_frame_writers)
    : config_(config),
      num_simulcast_or_spatial_layers_(
          std::max(config_.NumberOfSimulcastStreams(),
                   config_.NumberOfSpatialLayers())),
      stats_(stats),
      encoder_(encoder),
      decoders_(decoders),
      bitrate_allocator_(CreateBitrateAllocator(&config_)),
      framerate_fps_(0),
      encode_callback_(this),
      decode_callback_(this),
      input_frame_reader_(input_frame_reader),
      merged_encoded_frames_(num_simulcast_or_spatial_layers_),
      encoded_frame_writers_(encoded_frame_writers),
      decoded_frame_writers_(decoded_frame_writers),
      last_inputed_frame_num_(0),
      last_inputed_timestamp_(0),
      first_encoded_frame_(num_simulcast_or_spatial_layers_, true),
      last_encoded_frame_num_(num_simulcast_or_spatial_layers_),
      first_decoded_frame_(num_simulcast_or_spatial_layers_, true),
      last_decoded_frame_num_(num_simulcast_or_spatial_layers_) {
  // Sanity checks.
  RTC_CHECK(rtc::TaskQueue::Current())
      << "VideoProcessor must be run on a task queue.";
  RTC_CHECK(encoder);
  RTC_CHECK(decoders);
  RTC_CHECK_EQ(decoders->size(), num_simulcast_or_spatial_layers_);
  RTC_CHECK(input_frame_reader);
  RTC_CHECK(stats);
  RTC_CHECK(!encoded_frame_writers ||
            encoded_frame_writers->size() == num_simulcast_or_spatial_layers_);
  RTC_CHECK(!decoded_frame_writers ||
            decoded_frame_writers->size() == num_simulcast_or_spatial_layers_);

  // Setup required callbacks for the encoder and decoder and initialize them.
  RTC_CHECK_EQ(encoder_->RegisterEncodeCompleteCallback(&encode_callback_),
               WEBRTC_VIDEO_CODEC_OK);

  // Initialize codecs so that they are ready to receive frames.
  RTC_CHECK_EQ(encoder_->InitEncode(&config_.codec_settings,
                                    static_cast<int>(config_.NumberOfCores()),
                                    config_.max_payload_size_bytes),
               WEBRTC_VIDEO_CODEC_OK);
  for (auto& decoder : *decoders_) {
    RTC_CHECK_EQ(decoder->InitDecode(&config_.codec_settings,
                                     static_cast<int>(config_.NumberOfCores())),
                 WEBRTC_VIDEO_CODEC_OK);
    RTC_CHECK_EQ(decoder->RegisterDecodeCompleteCallback(&decode_callback_),
                 WEBRTC_VIDEO_CODEC_OK);
  }
}

VideoProcessor::~VideoProcessor() {
  RTC_DCHECK_CALLED_SEQUENTIALLY(&sequence_checker_);

  // Explicitly reset codecs, in case they don't do that themselves when they
  // go out of scope.
  RTC_CHECK_EQ(encoder_->Release(), WEBRTC_VIDEO_CODEC_OK);
  encoder_->RegisterEncodeCompleteCallback(nullptr);
  for (auto& decoder : *decoders_) {
    RTC_CHECK_EQ(decoder->Release(), WEBRTC_VIDEO_CODEC_OK);
    decoder->RegisterDecodeCompleteCallback(nullptr);
  }

  for (size_t simulcast_svc_idx = 0;
       simulcast_svc_idx < num_simulcast_or_spatial_layers_;
       ++simulcast_svc_idx) {
    uint8_t* buffer = merged_encoded_frames_.at(simulcast_svc_idx)._buffer;
    if (buffer) {
      delete[] buffer;
    }
  }
}

void VideoProcessor::ProcessFrame() {
  RTC_DCHECK_CALLED_SEQUENTIALLY(&sequence_checker_);
  const size_t frame_number = last_inputed_frame_num_++;

  // Get input frame and store for future quality calculation.
  rtc::scoped_refptr<I420BufferInterface> buffer =
      input_frame_reader_->ReadFrame();
  RTC_CHECK(buffer) << "Tried to read too many frames from the file.";
  const size_t timestamp =
      last_inputed_timestamp_ + kVideoPayloadTypeFrequency / framerate_fps_;
  VideoFrame input_frame(buffer, static_cast<uint32_t>(timestamp),
                         static_cast<int64_t>(timestamp / kMsToRtpTimestamp),
                         webrtc::kVideoRotation_0);
  input_frames_.emplace(frame_number, input_frame);
  last_inputed_timestamp_ = timestamp;

  // Create frame statistics object for all simulcast/spatial layers.
  for (size_t simulcast_svc_idx = 0;
       simulcast_svc_idx < num_simulcast_or_spatial_layers_;
       ++simulcast_svc_idx) {
    stats_->AddFrame(timestamp, simulcast_svc_idx);
  }

  // For the highest measurement accuracy of the encode time, the start/stop
  // time recordings should wrap the Encode call as tightly as possible.
  const int64_t encode_start_ns = rtc::TimeNanos();
  for (size_t simulcast_svc_idx = 0;
       simulcast_svc_idx < num_simulcast_or_spatial_layers_;
       ++simulcast_svc_idx) {
    FrameStatistics* frame_stat =
        stats_->GetFrame(frame_number, simulcast_svc_idx);
    frame_stat->encode_start_ns = encode_start_ns;
  }

  // Encode.
  const std::vector<FrameType> frame_types =
      config_.FrameTypeForFrame(frame_number);
  const int encode_return_code =
      encoder_->Encode(input_frame, nullptr, &frame_types);
  for (size_t simulcast_svc_idx = 0;
       simulcast_svc_idx < num_simulcast_or_spatial_layers_;
       ++simulcast_svc_idx) {
    FrameStatistics* frame_stat =
        stats_->GetFrame(frame_number, simulcast_svc_idx);
    frame_stat->encode_return_code = encode_return_code;
  }
}

void VideoProcessor::SetRates(size_t bitrate_kbps, size_t framerate_fps) {
  RTC_DCHECK_CALLED_SEQUENTIALLY(&sequence_checker_);
  framerate_fps_ = static_cast<uint32_t>(framerate_fps);
  bitrate_allocation_ = bitrate_allocator_->GetAllocation(
      static_cast<uint32_t>(bitrate_kbps * 1000), framerate_fps_);
  const int set_rates_result =
      encoder_->SetRateAllocation(bitrate_allocation_, framerate_fps_);
  RTC_DCHECK_GE(set_rates_result, 0)
      << "Failed to update encoder with new rate " << bitrate_kbps << ".";
}

void VideoProcessor::FrameEncoded(
    const webrtc::EncodedImage& encoded_image,
    const webrtc::CodecSpecificInfo& codec_specific) {
  RTC_DCHECK_CALLED_SEQUENTIALLY(&sequence_checker_);

  // For the highest measurement accuracy of the encode time, the start/stop
  // time recordings should wrap the Encode call as tightly as possible.
  const int64_t encode_stop_ns = rtc::TimeNanos();

  const VideoCodecType codec_type = codec_specific.codecType;
  if (config_.encoded_frame_checker) {
    config_.encoded_frame_checker->CheckEncodedFrame(codec_type, encoded_image);
  }

  // Layer metadata.
  size_t simulcast_svc_idx = 0;
  size_t temporal_idx = 0;
  GetLayerIndices(codec_specific, &simulcast_svc_idx, &temporal_idx);
  const size_t frame_wxh =
      encoded_image._encodedWidth * encoded_image._encodedHeight;
  frame_wxh_to_simulcast_svc_idx_[frame_wxh] = simulcast_svc_idx;

  FrameStatistics* frame_stat = stats_->GetFrameWithTimestamp(
      encoded_image._timeStamp, simulcast_svc_idx);
  const size_t frame_number = frame_stat->frame_number;

  // Ensure that the encode order is monotonically increasing, within this
  // simulcast/spatial layer.
  RTC_CHECK(first_encoded_frame_[simulcast_svc_idx] ||
            last_encoded_frame_num_[simulcast_svc_idx] < frame_number);

  // Ensure SVC spatial layers are delivered in ascending order.
  if (!first_encoded_frame_[simulcast_svc_idx] &&
      config_.NumberOfSpatialLayers() > 1) {
    for (size_t i = 0; i < simulcast_svc_idx; ++i) {
      RTC_CHECK_EQ(last_encoded_frame_num_[i], frame_number);
    }
    for (size_t i = simulcast_svc_idx + 1; i < num_simulcast_or_spatial_layers_;
         ++i) {
      RTC_CHECK_GT(frame_number, last_encoded_frame_num_[i]);
    }
  }
  first_encoded_frame_[simulcast_svc_idx] = false;
  last_encoded_frame_num_[simulcast_svc_idx] = frame_number;

  // Update frame statistics.
  frame_stat->encoding_successful = true;
  frame_stat->encode_time_us =
      GetElapsedTimeMicroseconds(frame_stat->encode_start_ns, encode_stop_ns);
  if (codec_type == kVideoCodecVP9) {
    const CodecSpecificInfoVP9& vp9_info = codec_specific.codecSpecific.VP9;
    frame_stat->inter_layer_predicted = vp9_info.inter_layer_predicted;

    // TODO(ssilkin): Implement bitrate allocation for VP9 SVC. For now set
    // target for base layers equal to total target to avoid devision by zero
    // at analysis.
    frame_stat->target_bitrate_kbps = bitrate_allocation_.get_sum_kbps();
  } else {
    frame_stat->target_bitrate_kbps =
        (bitrate_allocation_.GetBitrate(simulcast_svc_idx, temporal_idx) +
         500) /
        1000;
  }
  frame_stat->encoded_frame_size_bytes = encoded_image._length;
  frame_stat->frame_type = encoded_image._frameType;
  frame_stat->temporal_layer_idx = temporal_idx;
  frame_stat->simulcast_svc_idx = simulcast_svc_idx;
  frame_stat->max_nalu_size_bytes = GetMaxNaluSizeBytes(encoded_image, config_);
  frame_stat->qp = encoded_image.qp_;

  // Decode.
  const webrtc::EncodedImage* encoded_image_for_decode = &encoded_image;
  if (config_.NumberOfSpatialLayers() > 1) {
    encoded_image_for_decode = MergeAndStoreEncodedImageForSvcDecoding(
        encoded_image, codec_type, frame_number, simulcast_svc_idx);
  }
  frame_stat->decode_start_ns = rtc::TimeNanos();
  frame_stat->decode_return_code =
      decoders_->at(simulcast_svc_idx)
          ->Decode(*encoded_image_for_decode, false, nullptr);

  if (encoded_frame_writers_) {
    RTC_CHECK(
        encoded_frame_writers_->at(simulcast_svc_idx)
            ->WriteFrame(encoded_image, config_.codec_settings.codecType));
  }
}

void VideoProcessor::FrameDecoded(const VideoFrame& decoded_frame) {
  RTC_DCHECK_CALLED_SEQUENTIALLY(&sequence_checker_);

  // For the highest measurement accuracy of the decode time, the start/stop
  // time recordings should wrap the Decode call as tightly as possible.
  const int64_t decode_stop_ns = rtc::TimeNanos();

  // Layer metadata.
  const size_t simulcast_svc_idx =
      frame_wxh_to_simulcast_svc_idx_.at(decoded_frame.size());
  FrameStatistics* frame_stat = stats_->GetFrameWithTimestamp(
      decoded_frame.timestamp(), simulcast_svc_idx);
  const size_t frame_number = frame_stat->frame_number;

  // Ensure that the decode order is monotonically increasing, within this
  // simulcast/spatial layer.
  RTC_CHECK(first_decoded_frame_[simulcast_svc_idx] ||
            last_decoded_frame_num_[simulcast_svc_idx] < frame_number);
  first_decoded_frame_[simulcast_svc_idx] = false;
  last_decoded_frame_num_[simulcast_svc_idx] = frame_number;

  // Update frame statistics.
  frame_stat->decoding_successful = true;
  frame_stat->decode_time_us =
      GetElapsedTimeMicroseconds(frame_stat->decode_start_ns, decode_stop_ns);
  frame_stat->decoded_width = decoded_frame.width();
  frame_stat->decoded_height = decoded_frame.height();

  // Skip quality metrics calculation to not affect CPU usage.
  if (!config_.measure_cpu) {
    const auto reference_frame = input_frames_.find(frame_number);
    RTC_CHECK(reference_frame != input_frames_.cend())
        << "The codecs are either buffering too much, dropping too much, or "
           "being too slow relative the input frame rate.";
    CalculateFrameQuality(reference_frame->second, decoded_frame, frame_stat);
  }

  // Erase all buffered input frames that we have moved past for all
  // simulcast/spatial layers. Never buffer more than |kMaxBufferedInputFrames|
  // frames, to protect against long runs of consecutive frame drops for a
  // particular layer.
  const auto min_last_decoded_frame_num = std::min_element(
      last_decoded_frame_num_.cbegin(), last_decoded_frame_num_.cend());
  const size_t min_buffered_frame_num =
      std::max(0, static_cast<int>(frame_number) - kMaxBufferedInputFrames + 1);
  RTC_CHECK(min_last_decoded_frame_num != last_decoded_frame_num_.cend());
  const auto input_frames_erase_before = input_frames_.lower_bound(
      std::max(*min_last_decoded_frame_num, min_buffered_frame_num));
  input_frames_.erase(input_frames_.cbegin(), input_frames_erase_before);

  if (decoded_frame_writers_) {
    ExtractI420BufferWithSize(decoded_frame, config_.codec_settings.width,
                              config_.codec_settings.height, &tmp_i420_buffer_);
    RTC_CHECK_EQ(tmp_i420_buffer_.size(),
                 decoded_frame_writers_->at(simulcast_svc_idx)->FrameLength());
    RTC_CHECK(decoded_frame_writers_->at(simulcast_svc_idx)
                  ->WriteFrame(tmp_i420_buffer_.data()));
  }
}

const webrtc::EncodedImage*
VideoProcessor::MergeAndStoreEncodedImageForSvcDecoding(
    const EncodedImage& encoded_image,
    const VideoCodecType codec,
    size_t frame_number,
    size_t simulcast_svc_idx) {
  // Should only be called for SVC.
  RTC_CHECK_GT(config_.NumberOfSpatialLayers(), 1);

  EncodedImage base_image;
  RTC_CHECK_EQ(base_image._length, 0);

  // Each SVC layer is decoded with dedicated decoder. Add data of base layers
  // to current coded frame buffer.
  if (simulcast_svc_idx > 0) {
    base_image = merged_encoded_frames_.at(simulcast_svc_idx - 1);
    RTC_CHECK_EQ(base_image._timeStamp, encoded_image._timeStamp);
  }
  const size_t payload_size_bytes = base_image._length + encoded_image._length;
  const size_t buffer_size_bytes =
      payload_size_bytes + EncodedImage::GetBufferPaddingBytes(codec);

  uint8_t* copied_buffer = new uint8_t[buffer_size_bytes];
  RTC_CHECK(copied_buffer);

  if (base_image._length) {
    RTC_CHECK(base_image._buffer);
    memcpy(copied_buffer, base_image._buffer, base_image._length);
  }
  memcpy(copied_buffer + base_image._length, encoded_image._buffer,
         encoded_image._length);

  EncodedImage copied_image = encoded_image;
  copied_image = encoded_image;
  copied_image._buffer = copied_buffer;
  copied_image._length = payload_size_bytes;
  copied_image._size = buffer_size_bytes;

  // Replace previous EncodedImage for this spatial layer.
  uint8_t* old_buffer = merged_encoded_frames_.at(simulcast_svc_idx)._buffer;
  if (old_buffer) {
    delete[] old_buffer;
  }
  merged_encoded_frames_.at(simulcast_svc_idx) = copied_image;

  return &merged_encoded_frames_.at(simulcast_svc_idx);
}

}  // namespace test
}  // namespace webrtc
