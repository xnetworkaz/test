/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Originally this class is from Chromium.
// https://cs.chromium.org/chromium/src/base/android/jni_int_wrapper.h.

#ifndef SDK_ANDROID_NATIVE_API_JNI_JNI_INT_WRAPPER_H_
#define SDK_ANDROID_NATIVE_API_JNI_JNI_INT_WRAPPER_H_

#include <cstdint>

// Wrapper used to receive int when calling Java from native. The wrapper
// disallows automatic conversion of anything besides int32_t to a jint.
// Checking is only done in debugging builds.

#ifdef NDEBUG

typedef jint JniIntWrapper;

// This inline is sufficiently trivial that it does not change the
// final code generated by g++.
inline jint as_jint(JniIntWrapper wrapper) {
  return wrapper;
}

#else

class JniIntWrapper {
 public:
  JniIntWrapper() : i_(0) {}
  JniIntWrapper(int32_t i) : i_(i) {}  // NOLINT(runtime/explicit)
  explicit JniIntWrapper(const JniIntWrapper& ji) : i_(ji.i_) {}

  jint as_jint() const { return i_; }

  // If you get an "invokes a deleted function" error at the lines below it is
  // because you used an implicit conversion to convert e.g. a long to an
  // int32_t when calling Java. We disallow this. If you want a lossy
  // conversion, please use an explicit conversion in your C++ code.
  JniIntWrapper(uint32_t) = delete;  // NOLINT(runtime/explicit)
  JniIntWrapper(uint64_t) = delete;  // NOLINT(runtime/explicit)
  JniIntWrapper(int64_t) = delete;   // NOLINT(runtime/explicit)

 private:
  const jint i_;
};

inline jint as_jint(const JniIntWrapper& wrapper) {
  return wrapper.as_jint();
}

#endif  // NDEBUG

#endif  // SDK_ANDROID_NATIVE_API_JNI_JNI_INT_WRAPPER_H_
