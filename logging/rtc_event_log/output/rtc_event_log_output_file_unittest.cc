/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <fstream>
#include <memory>
#include <string>

#include "logging/rtc_event_log/output/rtc_event_log_output_file.h"
#include "rtc_base/ptr_util.h"
#include "rtc_base/random.h"
#include "system_wrappers/include/clock.h"
#include "test/gtest.h"
#include "test/testsupport/fileutils.h"

namespace webrtc {

class RtcEventLogOutputFileTest : public ::testing::Test {
 public:
  RtcEventLogOutputFileTest() : output_file_name_(GetOutputFilePath()) {
    // Ensure no leftovers from previous runs, which might not have terminated
    // in an orderly fashion.
    remove(output_file_name_.c_str());
  }

  ~RtcEventLogOutputFileTest() override {
    remove(output_file_name_.c_str());
  }

 protected:
  std::string GetOutputFilePath() const {
    auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return test::OutputPath() + test_info->test_case_name() + test_info->name();
  }

  bool OutputFileExists() const {
    return true;  // TODO(eladalon): !!! Implement.
  }

  std::string GetOutputFileContents() const {
    RTC_CHECK(OutputFileExists());
    std::ifstream file(output_file_name_,
                       std::ios_base::in | std::ios_base::binary);
    RTC_CHECK(file.is_open());
    RTC_CHECK(file.good());
    std::string file_str((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return file_str;
  }

  const std::string output_file_name_;
};

TEST_F(RtcEventLogOutputFileTest, NonDefectiveOutputsStartOutActive) {
  auto output_file = rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_);
  EXPECT_TRUE(output_file->IsActive());
}

TEST_F(RtcEventLogOutputFileTest, DefectiveOutputsStartOutInactive) {
  const std::string illegal_filename = "/////////";
  auto output_file = rtc::MakeUnique<RtcEventLogOutputFile>(illegal_filename);
  EXPECT_FALSE(output_file->IsActive());
}

// Sanity over opening a file (by filename) with an unlimited size.
TEST_F(RtcEventLogOutputFileTest, UnlimitedOutputFile) {
  const std::string output_str = "one two three";

  auto output_file = rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_);
  output_file->Write(output_str);
  output_file.reset();  // Closing the file flushes the buffer to disk.

  EXPECT_EQ(GetOutputFileContents(), output_str);
}

// Do not allow writing more bytes to the file than
TEST_F(RtcEventLogOutputFileTest, LimitedOutputFileCappedToCapacity) {
  // Fit two bytes, then the third should be rejected.
  auto output_file =
      rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_, 2);

  output_file->Write("1");
  output_file->Write("2");
  output_file->Write("3");
  output_file.reset();  // Closing the file flushes the buffer to disk.

  EXPECT_EQ(GetOutputFileContents(), "12");
}

// Make sure that calls to Write() either write everything to the file, or
// nothing (short of underlying issues in the module that handles the file,
// which would be beyond our control).
TEST_F(RtcEventLogOutputFileTest, DoNotWritePartialLines) {
  const std::string output_str_1 = "0123456789";
  const std::string output_str_2 = "abcdefghij";

  // Set a file size limit just shy of fitting the entire second line.
  const size_t size_limit = output_str_1.length() + output_str_2.length() - 1;
  auto output_file =
      rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_, size_limit);

  output_file->Write(output_str_1);
  output_file->Write(output_str_2);
  output_file.reset();  // Closing the file flushes the buffer to disk.

  EXPECT_EQ(GetOutputFileContents(), output_str_1);
}

TEST_F(RtcEventLogOutputFileTest, UnsuccessfulWriteReturnsFalse) {
  auto output_file =
      rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_, 2);
  EXPECT_FALSE(output_file->Write("abc"));
}

TEST_F(RtcEventLogOutputFileTest, SuccessfulWriteReturnsTrue) {
  auto output_file =
      rtc::MakeUnique<RtcEventLogOutputFile>(output_file_name_, 3);
  EXPECT_TRUE(output_file->Write("abc"));
}


}  // namespace webrtc
