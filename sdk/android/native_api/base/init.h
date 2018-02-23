/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SDK_ANDROID_NATIVE_API_BASE_INIT_H_
#define SDK_ANDROID_NATIVE_API_BASE_INIT_H_

#include <jni.h>

namespace webrtc {

// Initializes global state needed by WebRTC Android NDK. When using this
// version, the client must call ContextUtils.initialize from Java code before
// using the library.
void InitAndroid(JavaVM* jvm);

// Initializes global state needed by WebRTC Android NDK. This version calls
// ContextUtils.initialize through JNI.
void InitAndroid(JavaVM* jvm, jobject application_context_);

}  // namespace webrtc

#endif  // SDK_ANDROID_NATIVE_API_BASE_INIT_H_
