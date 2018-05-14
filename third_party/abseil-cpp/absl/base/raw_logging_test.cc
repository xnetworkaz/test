// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This test serves primarily as a compilation test for base/raw_logging.h.
// Raw logging testing is covered by logging_unittest.cc, which is not as
// portable as this test.

#include "absl/base/internal/raw_logging.h"

#include "gtest/gtest.h"

namespace {

TEST(RawLoggingCompilationTest, Log) {
  ABSL_RAW_LOG(INFO, "RAW INFO: %d", 1);
  ABSL_RAW_LOG(ERROR, "RAW ERROR: %d", 1);
}

TEST(RawLoggingCompilationTest, PassingCheck) {
  ABSL_RAW_CHECK(true, "RAW CHECK");
}

// Not all platforms support output from raw log, so we don't verify any
// particular output for RAW check failures (expecting the empty std::string
// accomplishes this).  This test is primarily a compilation test, but we
// are verifying process death when EXPECT_DEATH works for a platform.
const char kExpectedDeathOutput[] = "";

TEST(RawLoggingDeathTest, FailingCheck) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_CHECK(1 == 0, "explanation"),
                            kExpectedDeathOutput);
}

TEST(RawLoggingDeathTest, LogFatal) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_LOG(FATAL, "my dog has fleas"),
                            kExpectedDeathOutput);
}

}  // namespace
