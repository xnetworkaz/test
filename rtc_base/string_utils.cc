/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/string_utils.h"

namespace rtc {

size_t strcpyn(char* buffer,
               size_t buflen,
               const char* source,
               size_t srclen /* = SIZE_UNKNOWN */) {
  if (buflen <= 0)
    return 0;

  if (srclen == SIZE_UNKNOWN) {
    srclen = strlen(source);
  }
  if (srclen >= buflen) {
    srclen = buflen - 1;
  }
  memcpy(buffer, source, srclen);
  buffer[srclen] = 0;
  return srclen;
}

static const char kWhitespace[] = " \n\r\t";

std::string string_trim(absl::string_view s) {
  absl::string_view::size_type first = s.find_first_not_of(kWhitespace);
  absl::string_view::size_type last = s.find_last_not_of(kWhitespace);

  if (first == absl::string_view::npos || last == absl::string_view::npos) {
    return std::string("");
  }

  return std::string(s.substr(first, last - first + 1));
}

std::string ToHex(const int i) {
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "%x", i);

  return std::string(buffer);
}

}  // namespace rtc
