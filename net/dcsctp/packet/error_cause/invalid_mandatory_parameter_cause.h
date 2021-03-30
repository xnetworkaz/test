/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef NET_DCSCTP_PACKET_ERROR_CAUSE_INVALID_MANDATORY_PARAMETER_CAUSE_H_
#define NET_DCSCTP_PACKET_ERROR_CAUSE_INVALID_MANDATORY_PARAMETER_CAUSE_H_
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/array_view.h"
#include "net/dcsctp/packet/error_cause/error_cause.h"
#include "net/dcsctp/packet/tlv_trait.h"

namespace dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.7
struct InvalidMandatoryParameterCauseConfig : public ParameterConfig {
  static constexpr int kType = 7;
  static constexpr size_t kHeaderSize = 4;
  static constexpr size_t kVariableLengthAlignment = 0;
};

class InvalidMandatoryParameterCause
    : public Parameter,
      public TLVTrait<InvalidMandatoryParameterCauseConfig> {
 public:
  static constexpr int kType = InvalidMandatoryParameterCauseConfig::kType;

  InvalidMandatoryParameterCause() {}

  static absl::optional<InvalidMandatoryParameterCause> Parse(
      rtc::ArrayView<const uint8_t> data);

  void SerializeTo(std::vector<uint8_t>& out) const override;
  std::string ToString() const override;
};
}  // namespace dcsctp

#endif  // NET_DCSCTP_PACKET_ERROR_CAUSE_INVALID_MANDATORY_PARAMETER_CAUSE_H_
