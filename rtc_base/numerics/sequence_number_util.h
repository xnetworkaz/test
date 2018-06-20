/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_NUMERICS_SEQUENCE_NUMBER_UTIL_H_
#define RTC_BASE_NUMERICS_SEQUENCE_NUMBER_UTIL_H_

#include <limits>
#include <type_traits>

#include "absl/types/optional.h"
#include "rtc_base/numerics/mod_ops.h"
#include "rtc_base/numerics/safe_compare.h"

namespace webrtc {

// Test if the sequence number |a| is ahead or at sequence number |b|.
//
// If |M| is an even number and the two sequence numbers are at max distance
// from each other, then the sequence number with the highest value is
// considered to be ahead.
template <typename T, T M>
inline typename std::enable_if<(M > 0), bool>::type AheadOrAt(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  const T maxDist = M / 2;
  if (!(M & 1) && MinDiff<T, M>(a, b) == maxDist)
    return b < a;
  return ForwardDiff<T, M>(b, a) <= maxDist;
}

template <typename T, T M>
inline typename std::enable_if<(M == 0), bool>::type AheadOrAt(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  const T maxDist = std::numeric_limits<T>::max() / 2 + T(1);
  if (a - b == maxDist)
    return b < a;
  return ForwardDiff(b, a) < maxDist;
}

template <typename T>
inline bool AheadOrAt(T a, T b) {
  return AheadOrAt<T, 0>(a, b);
}

// Test if the sequence number |a| is ahead of sequence number |b|.
//
// If |M| is an even number and the two sequence numbers are at max distance
// from each other, then the sequence number with the highest value is
// considered to be ahead.
template <typename T, T M = 0>
inline bool AheadOf(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  return a != b && AheadOrAt<T, M>(a, b);
}

// Comparator used to compare sequence numbers in a continuous fashion.
//
// WARNING! If used to sort sequence numbers of length M then the interval
//          covered by the sequence numbers may not be larger than floor(M/2).
template <typename T, T M = 0>
struct AscendingSeqNumComp {
  bool operator()(T a, T b) const { return AheadOf<T, M>(a, b); }
};

// Comparator used to compare sequence numbers in a continuous fashion.
//
// WARNING! If used to sort sequence numbers of length M then the interval
//          covered by the sequence numbers may not be larger than floor(M/2).
template <typename T, T M = 0>
struct DescendingSeqNumComp {
  bool operator()(T a, T b) const { return AheadOf<T, M>(b, a); }
};

// A sequencer number unwrapper where the start value of the unwrapped sequence
// can be set. The unwrapped value is not allowed to wrap.
template <typename T, T M = 0>
class SeqNumUnwrapper {
  // Use '<' instead of rtc::SafeLt to avoid crbug.com/753488
  static_assert(
      std::is_unsigned<T>::value &&
          std::numeric_limits<T>::max() < std::numeric_limits<uint64_t>::max(),
      "Type unwrapped must be an unsigned integer smaller than uint64_t.");

 public:
  // We want a default value that is close to 2^62 for a two reasons. Firstly,
  // we can unwrap wrapping numbers in either direction, and secondly, the
  // unwrapped numbers can be stored in either int64_t or uint64_t. We also want
  // the default value to be human readable, which makes a power of 10 suitable.
  static constexpr uint64_t kDefaultStartValue = 1000000000000000000UL;

  SeqNumUnwrapper() : last_unwrapped_(kDefaultStartValue) {}
  explicit SeqNumUnwrapper(uint64_t start_at) : last_unwrapped_(start_at) {}

  uint64_t Unwrap(T value) {
    if (!last_value_)
      last_value_.emplace(value);

    uint64_t unwrapped = 0;
    if (AheadOrAt<T, M>(value, *last_value_)) {
      unwrapped = last_unwrapped_ + ForwardDiff<T, M>(*last_value_, value);
      RTC_CHECK_GE(unwrapped, last_unwrapped_);
    } else {
      unwrapped = last_unwrapped_ - ReverseDiff<T, M>(*last_value_, value);
      RTC_CHECK_LT(unwrapped, last_unwrapped_);
    }

    *last_value_ = value;
    last_unwrapped_ = unwrapped;
    return last_unwrapped_;
  }

 private:
  uint64_t last_unwrapped_;
  absl::optional<T> last_value_;
};

}  // namespace webrtc

#endif  // RTC_BASE_NUMERICS_SEQUENCE_NUMBER_UTIL_H_
