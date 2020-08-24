/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#import <Foundation/Foundation.h>

#import "RTCMacros.h"
#import "RTCVideoEncoderVP9.h"
#import "RTCWrappedNativeVideoEncoder.h"

#include "modules/video_coding/codecs/vp9/include/vp9.h"

@implementation RTC_OBJC_TYPE (RTCVideoEncoderVP9)

+ (id<RTC_OBJC_TYPE(RTCVideoEncoder)>)vp9Encoder {
  return [[RTC_OBJC_TYPE(RTCWrappedNativeVideoEncoder) alloc]
      initWithNativeEncoder:std::unique_ptr<webrtc::VideoEncoder>(webrtc::VP9Encoder::Create())];
}

@end
