/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/android/src/jni/videodecoderwrapper.h"

#include "api/video/video_frame.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/utility/vp8_header_parser.h"
#include "modules/video_coding/utility/vp9_uncompressed_header_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/timeutils.h"
#include "sdk/android/generated_video_jni/jni/VideoDecoderWrapper_jni.h"
#include "sdk/android/generated_video_jni/jni/VideoDecoder_jni.h"
#include "sdk/android/src/jni/classreferenceholder.h"
#include "sdk/android/src/jni/encodedimage.h"
#include "sdk/android/src/jni/videocodecstatus.h"
#include "sdk/android/src/jni/videoframe.h"

namespace webrtc {
namespace jni {

VideoDecoderWrapper::VideoDecoderWrapper(JNIEnv* jni, jobject decoder)
    : decoder_(jni, decoder),
      integer_class_(jni, jni->FindClass("java/lang/Integer")) {

  integer_constructor_ = jni->GetMethodID(*integer_class_, "<init>", "(I)V");
  int_value_method_ = jni->GetMethodID(*integer_class_, "intValue", "()I");

  initialized_ = false;
  // QP parsing starts enabled and we disable it if the decoder provides frames.
  qp_parsing_enabled_ = true;

  implementation_name_ = JavaToStdString(
      jni, Java_VideoDecoder_getImplementationName(jni, decoder));
}

int32_t VideoDecoderWrapper::InitDecode(const VideoCodec* codec_settings,
                                        int32_t number_of_cores) {
  JNIEnv* jni = AttachCurrentThreadIfNeeded();
  ScopedLocalRefFrame local_ref_frame(jni);

  codec_settings_ = *codec_settings;
  number_of_cores_ = number_of_cores;
  return InitDecodeInternal(jni);
}

int32_t VideoDecoderWrapper::InitDecodeInternal(JNIEnv* jni) {
  jobject settings = Java_VideoDecoderWrapper_createSettings(
      jni, number_of_cores_, codec_settings_.width, codec_settings_.height);

  jobject callback = Java_VideoDecoderWrapper_createDecoderCallback(
      jni, jlongFromPointer(this));

  jobject ret =
      Java_VideoDecoder_initDecode(jni, *decoder_, settings, callback);
  if (Java_VideoCodecStatus_getNumber(jni, ret) == WEBRTC_VIDEO_CODEC_OK) {
    initialized_ = true;
  }

  // The decoder was reinitialized so re-enable the QP parsing in case it stops
  // providing QP values.
  qp_parsing_enabled_ = true;

  return HandleReturnCode(jni, ret);
}

int32_t VideoDecoderWrapper::Decode(
    const EncodedImage& input_image,
    bool missing_frames,
    const RTPFragmentationHeader* fragmentation,
    const CodecSpecificInfo* codec_specific_info,
    int64_t render_time_ms) {
  if (!initialized_) {
    // Most likely initializing the codec failed.
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }

  JNIEnv* jni = AttachCurrentThreadIfNeeded();
  ScopedLocalRefFrame local_ref_frame(jni);

  FrameExtraInfo frame_extra_info;
  frame_extra_info.capture_time_ns =
      input_image.capture_time_ms_ * rtc::kNumNanosecsPerMillisec;
  frame_extra_info.timestamp_rtp = input_image._timeStamp;
  frame_extra_info.qp =
      qp_parsing_enabled_ ? ParseQP(input_image) : rtc::nullopt;
  frame_extra_infos_.push_back(frame_extra_info);

  jobject jinput_image =
      ConvertEncodedImageToJavaEncodedImage(jni, input_image);
  jobject ret = Java_VideoDecoder_decode(jni, *decoder_, jinput_image, nullptr);
  return HandleReturnCode(jni, ret);
}

int32_t VideoDecoderWrapper::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t VideoDecoderWrapper::Release() {
  JNIEnv* jni = AttachCurrentThreadIfNeeded();
  ScopedLocalRefFrame local_ref_frame(jni);
  jobject ret = Java_VideoDecoder_release(jni, *decoder_);
  frame_extra_infos_.clear();
  initialized_ = false;
  return HandleReturnCode(jni, ret);
}

bool VideoDecoderWrapper::PrefersLateDecoding() const {
  JNIEnv* jni = AttachCurrentThreadIfNeeded();
  return Java_VideoDecoder_getPrefersLateDecoding(jni, *decoder_);
}

const char* VideoDecoderWrapper::ImplementationName() const {
  return implementation_name_.c_str();
}

void VideoDecoderWrapper::OnDecodedFrame(JNIEnv* jni,
                                         jobject jframe,
                                         jobject jdecode_time_ms,
                                         jobject jqp) {
  const uint64_t capture_time_ns = Java_VideoFrame_getTimestampNs(jni, jframe);

  FrameExtraInfo frame_extra_info;
  do {
    if (frame_extra_infos_.empty()) {
      RTC_LOG(LS_WARNING) << "Java decoder produced an unexpected frame.";
      return;
    }

    frame_extra_info = frame_extra_infos_.front();
    frame_extra_infos_.pop_front();
    // If the decoder might drop frames so iterate through the queue until we
    // find a matching timestamp.
  } while (frame_extra_info.capture_time_ns != capture_time_ns);

  VideoFrame frame =
      JavaToNativeFrame(jni, jframe, frame_extra_info.timestamp_rtp);

  rtc::Optional<int32_t> decoding_time_ms;
  if (jdecode_time_ms != nullptr) {
    decoding_time_ms = jni->CallIntMethod(jdecode_time_ms, int_value_method_);
  }

  rtc::Optional<uint8_t> qp;
  if (jqp != nullptr) {
    qp = jni->CallIntMethod(jqp, int_value_method_);
    // The decoder provides QP values itself, no need to parse the bitstream.
    qp_parsing_enabled_ = false;
  } else {
    qp = frame_extra_info.qp;
    // The decoder doesn't provide QP values, ensure bitstream parsing is
    // enabled.
    qp_parsing_enabled_ = true;
  }

  callback_->Decoded(frame, decoding_time_ms, qp);
}

jobject VideoDecoderWrapper::ConvertEncodedImageToJavaEncodedImage(
    JNIEnv* jni,
    const EncodedImage& image) {
  jobject buffer = jni->NewDirectByteBuffer(image._buffer, image._length);

  jobject frame_type = Java_EncodedImage_createFrameType(jni, image._frameType);

  jobject qp = nullptr;
  if (image.qp_ != -1) {
    qp = jni->NewObject(*integer_class_, integer_constructor_, image.qp_);
  }
  return Java_EncodedImage_Create(
      jni, buffer, image._encodedWidth, image._encodedHeight,
      image.capture_time_ms_ * rtc::kNumNanosecsPerMillisec, frame_type,
      image.rotation_, image._completeFrame, qp);
}

int32_t VideoDecoderWrapper::HandleReturnCode(JNIEnv* jni, jobject code) {
  int32_t value = Java_VideoCodecStatus_getNumber(jni, code);
  if (value < 0) {  // Any errors are represented by negative values.
    // Reset the codec.
    if (Release() == WEBRTC_VIDEO_CODEC_OK) {
      InitDecodeInternal(jni);
    }

    RTC_LOG(LS_WARNING) << "Falling back to software decoder.";
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  } else {
    return value;
  }
}

rtc::Optional<uint8_t> VideoDecoderWrapper::ParseQP(
    const EncodedImage& input_image) {
  if (input_image.qp_ != -1) {
    return input_image.qp_;
  }

  rtc::Optional<uint8_t> qp;
  switch (codec_settings_.codecType) {
    case kVideoCodecVP8: {
      int qp_int;
      if (vp8::GetQp(input_image._buffer, input_image._length, &qp_int)) {
        qp = qp_int;
      }
      break;
    }
    case kVideoCodecVP9: {
      int qp_int;
      if (vp9::GetQp(input_image._buffer, input_image._length, &qp_int)) {
        qp = qp_int;
      }
      break;
    }
    case kVideoCodecH264: {
      h264_bitstream_parser_.ParseBitstream(input_image._buffer,
                                            input_image._length);
      int qp_int;
      if (h264_bitstream_parser_.GetLastSliceQp(&qp_int)) {
        qp = qp_int;
      }
      break;
    }
    default:
      break;  // Default is to not provide QP.
  }
  return qp;
}

// TODO(magjed): Generate.
JNI_FUNCTION_DECLARATION(void,
                         VideoDecoderWrapper_onDecodedFrame,
                         JNIEnv* jni,
                         jclass,
                         jlong jnative_decoder,
                         jobject jframe,
                         jobject jdecode_time_ms,
                         jobject jqp) {
  VideoDecoderWrapper* native_decoder =
      reinterpret_cast<VideoDecoderWrapper*>(jnative_decoder);
  native_decoder->OnDecodedFrame(jni, jframe, jdecode_time_ms, jqp);
}

}  // namespace jni
}  // namespace webrtc
