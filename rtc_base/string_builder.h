/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_STRING_BUILDER_H_
#define RTC_BASE_STRING_BUILDER_H_

#include <string>
#include <cstdio>

#include "rtc_base/checks.h"
#include "rtc_base/stringutils.h"

namespace rtc {

// This is a minimalistic string builder class meant to cover the most cases
// of when you might otherwise be tempted to use a stringstream (discouraged
// for anything except logging).
// This class allocates a fixed size buffer on the stack and concatenates
// strings and numbers into it, allowing the results to be read via |str()|.
template <size_t size>
class SimpleStringBuilder {
 public:
  SimpleStringBuilder() { buffer_[0] = '\0'; }

  SimpleStringBuilder& operator<<(const char* str) {
    return Append(str);
  }

  SimpleStringBuilder& operator<<(char ch) {
    return Append(&ch, 1);
  }

  SimpleStringBuilder& operator<<(const std::string& str) {
    return Append(str.c_str(), str.length());
  }

  // Numeric conversion routines.
  //
  // We use std::[v]snprintf instead of std::to_string because:
  // * std::to_string relies on the current locale for formatting purposes,
  //   and therefore concurrent calls to std::to_string from multiple threads
  //   may result in partial serialization of calls
  // * snprintf allows us to print the number directly into our buffer.
  // * avoid allocating a std::string (potential heap alloc).
  // TODO(tommi): Switch to std::to_chars in C++17.

  SimpleStringBuilder& operator<<(int i) {
    return AppendFormat("%d", i);
  }

  SimpleStringBuilder& operator<<(unsigned i) {
    return AppendFormat("%u", i);
  }

  SimpleStringBuilder& operator<<(long i) {  // NOLINT
    return AppendFormat("%ld", i);
  }

  SimpleStringBuilder& operator<<(long long i) {  // NOLINT
    return AppendFormat("%lld", i);
  }

  SimpleStringBuilder& operator<<(unsigned long i) {  // NOLINT
    return AppendFormat("%lu", i);
  }

  SimpleStringBuilder& operator<<(unsigned long long i) {  // NOLINT
    return AppendFormat("%llu", i);
  }

  SimpleStringBuilder& operator<<(float f) {
    return AppendFormat("%f", f);
  }

  SimpleStringBuilder& operator<<(double f) {
    return AppendFormat("%f", f);
  }

  SimpleStringBuilder& operator<<(long double f) {
    return AppendFormat("%Lf", f);
  }

  const char* str() const { return &buffer_[0]; }
  size_t length() const { return length_; }

 private:
  SimpleStringBuilder& AppendFormat(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    UpdateLength(std::vsnprintf(&buffer_[length_], size - length_, fmt, args));
    va_end(args);
    return *this;
  }

  SimpleStringBuilder& Append(const char* str, size_t length = SIZE_UNKNOWN) {
    UpdateLength(rtc::strcpyn(&buffer_[length_], size - length_, str, length));
    return *this;
  }

  void UpdateLength(size_t chars_added) {
    length_ += chars_added;
    RTC_DCHECK_LT(length_, size - 1)
        << "Buffer size limit reached (" << size << ")";
  }

  char buffer_[size];  // NOLINT
  size_t length_ = 0;

  SimpleStringBuilder(const SimpleStringBuilder&) = delete;
  SimpleStringBuilder& operator=(const SimpleStringBuilder&) = delete;
};

}  // namespace rtc

#endif  // RTC_BASE_STRING_BUILDER_H__
