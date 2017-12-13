/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/portallocator.h"
#include "sdk/android/src/jni/jni_helpers.h"

namespace webrtc {
namespace jni {

JNI_FUNCTION_DECLARATION(void,
                         PortAllocator_nativeFreePortAllocator,
                         JNIEnv* jni,
                         jclass,
                         jlong j_port_allocator_pointer) {
  delete reinterpret_cast<cricket::PortAllocator*>(j_port_allocator_pointer);
}

}  // namespace jni
}  // namespace webrtc
