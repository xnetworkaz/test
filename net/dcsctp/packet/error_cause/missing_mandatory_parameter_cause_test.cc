/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/packet/error_cause/missing_mandatory_parameter_cause.h"

#include <stdint.h>

#include <type_traits>
#include <vector>

#include "api/array_view.h"
#include "net/dcsctp/testing/testing_macros.h"
#include "rtc_base/gunit.h"
#include "test/gmock.h"

namespace dcsctp {
namespace {
using ::testing::ElementsAre;

TEST(MissingMandatoryParameterCauseTest, SerializeAndDeserialize) {
  uint16_t parameter_types[] = {1, 2, 3};
  MissingMandatoryParameterCause parameter(parameter_types);

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      MissingMandatoryParameterCause deserialized,
      MissingMandatoryParameterCause::Parse(serialized));

  EXPECT_THAT(deserialized.missing_parameter_types(), ElementsAre(1, 2, 3));
}

}  // namespace
}  // namespace dcsctp
