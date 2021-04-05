/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/tx/retransmission_timeout.h"

#include <cmath>
#include <cstdint>

#include "net/dcsctp/public/dcsctp_options.h"

namespace dcsctp {
namespace {
// https://tools.ietf.org/html/rfc4960#section-15
constexpr double kRtoAlpha = 0.125;
constexpr double kRtoBeta = 0.25;
}  // namespace

RetransmissionTimeout::RetransmissionTimeout(const DcSctpOptions& options)
    : min_rto_(*options.rto_min),
      max_rto_(*options.rto_max),
      rto_(*options.rto_initial) {}

void RetransmissionTimeout::ObserveRTT(DurationMs measured_rtt) {
  double rtt = *measured_rtt;

  // Unrealistic values will simply be skipped.
  // 800 ms * speed of light in optical fiber / earth circumference = 3,99.
  if (rtt < 0.0 || rtt > max_rto_) {
    return;
  }

  if (first_measurement_) {
    // https://tools.ietf.org/html/rfc4960#section-6.3.1
    // "When the first RTT measurement R is made, set"
    srtt_ = rtt;
    rttvar_ = rtt * 0.5;
    rto_ = srtt_ + 4 * rttvar_;
    first_measurement_ = false;
  } else {
    // https://tools.ietf.org/html/rfc4960#section-6.3.1
    // "When a new RTT measurement R' is made, set"
    // Note: All calculations are done in the floating point domain to maintain
    // precision.
    double diff = std::abs(rtt - srtt_);
    rttvar_ = (1 - kRtoBeta) * rttvar_ + kRtoBeta * diff;
    srtt_ = (1 - kRtoAlpha) * srtt_ + kRtoAlpha * rtt;
    rto_ = srtt_ + 4 * rttvar_;
  }

  // Clamp RTO between min and max.
  rto_ = std::fmin(std::fmax(rto_, min_rto_), max_rto_);
}
}  // namespace dcsctp
