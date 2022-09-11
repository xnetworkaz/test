/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/testsupport/metrics/metric.h"

#include <string>

namespace webrtc {
namespace test {

std::string ToString(Unit unit) {
  switch (unit) {
    case Unit::kTimeMs:
      return "TimeMs";
    case Unit::kPercent:
      return "Percent";
    case Unit::kSizeInBytes:
      return "SizeInBytes";
    case Unit::kKilobitsPerSecond:
      return "KilobitsPerSecond";
    case Unit::kHertz:
      return "Hertz";
    case Unit::kUnitless:
      return "Unitless";
    case Unit::kCount:
      return "Count";
  }
}

std::string ToString(ImprovementDirection direction) {
  switch (direction) {
    case ImprovementDirection::kBiggerIsBetter:
      return "BiggerIsBetter";
    case ImprovementDirection::kBothPossible:
      return "BothPossible";
    case ImprovementDirection::kSmallerIsBetter:
      return "SmallerIsBetter";
  }
}

}  // namespace test
}  // namespace webrtc
