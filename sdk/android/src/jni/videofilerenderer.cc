/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <jni.h>

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "sdk/android/generated_video_jni/jni/VideoFileRenderer_jni.h"
#include "sdk/android/native_api/jni/scoped_java_ref.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace webrtc {
namespace jni {

static void JNI_VideoFileRenderer_I420Scale(
    JNIEnv* jni,
    const JavaParamRef<jclass>&,
    const JavaParamRef<jobject>& j_src_buffer_y,
    jint j_src_stride_y,
    const JavaParamRef<jobject>& j_src_buffer_u,
    jint j_src_stride_u,
    const JavaParamRef<jobject>& j_src_buffer_v,
    jint j_src_stride_v,
    jint width,
    jint height,
    const JavaParamRef<jobject>& j_dst_buffer,
    jint dstWidth,
    jint dstHeight) {
  size_t src_size_y = jni->GetDirectBufferCapacity(j_src_buffer_y.obj());
  size_t src_size_u = jni->GetDirectBufferCapacity(j_src_buffer_u.obj());
  size_t src_size_v = jni->GetDirectBufferCapacity(j_src_buffer_v.obj());
  size_t dst_size = jni->GetDirectBufferCapacity(j_dst_buffer.obj());
  int dst_stride = dstWidth;
  RTC_CHECK_GE(src_size_y, j_src_stride_y * height);
  RTC_CHECK_GE(src_size_u, j_src_stride_u * height / 4);
  RTC_CHECK_GE(src_size_v, j_src_stride_v * height / 4);
  RTC_CHECK_GE(dst_size, dst_stride * dstHeight * 3 / 2);
  uint8_t* src_y = reinterpret_cast<uint8_t*>(
      jni->GetDirectBufferAddress(j_src_buffer_y.obj()));
  uint8_t* src_u = reinterpret_cast<uint8_t*>(
      jni->GetDirectBufferAddress(j_src_buffer_u.obj()));
  uint8_t* src_v = reinterpret_cast<uint8_t*>(
      jni->GetDirectBufferAddress(j_src_buffer_v.obj()));
  uint8_t* dst = reinterpret_cast<uint8_t*>(
      jni->GetDirectBufferAddress(j_dst_buffer.obj()));

  uint8_t* dst_y = dst;
  size_t dst_stride_y = dst_stride;
  uint8_t* dst_u = dst + dst_stride * dstHeight;
  size_t dst_stride_u = dst_stride / 2;
  uint8_t* dst_v = dst + dst_stride * dstHeight * 5 / 4;
  size_t dst_stride_v = dst_stride / 2;

  int ret = libyuv::I420Scale(
      src_y, j_src_stride_y, src_u, j_src_stride_u, src_v, j_src_stride_v,
      width, height, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v,
      dst_stride_v, dstWidth, dstHeight, libyuv::kFilterBilinear);
  if (ret) {
    RTC_LOG(LS_ERROR) << "Error scaling I420 frame: " << ret;
  }
}

}  // namespace jni
}  // namespace webrtc
