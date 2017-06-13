/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/sdk/android/src/jni/videodecoderfactorywrapper.h"

#include "webrtc/api/video_codecs/video_decoder.h"
#include "webrtc/base/logging.h"
#include "webrtc/sdk/android/src/jni/videodecoderwrapper.h"

namespace webrtc_jni {

VideoDecoderFactoryWrapper::VideoDecoderFactoryWrapper(JNIEnv* jni,
                                                       jobject decoder_factory)
    : decoder_factory_(jni, decoder_factory) {
  jclass decoder_factory_class = jni->GetObjectClass(*decoder_factory_);
  create_decoder_method_ = jni->GetMethodID(
      decoder_factory_class, "createVideoDecoder",
      "(Lorg/webrtc/VideoCodecInfo;)Lorg/webrtc/VideoDecoder;");
}

webrtc::VideoDecoder* VideoDecoderFactoryWrapper::CreateVideoDecoder(
    webrtc::VideoCodecType type) {
  JNIEnv* jni = AttachCurrentThreadIfNeeded();
  jobject decoder =
      jni->CallObjectMethod(*decoder_factory_, create_decoder_method_, nullptr);
  return decoder != nullptr ? new VideoDecoderWrapper(jni, decoder) : nullptr;
}

void VideoDecoderFactoryWrapper::DestroyVideoDecoder(
    webrtc::VideoDecoder* decoder) {
  delete decoder;
}

}  // namespace webrtc_jni
