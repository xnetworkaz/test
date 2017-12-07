/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits>

#include "logging/rtc_event_log/output/rtc_event_log_output_file.h"
#include "logging/rtc_event_log/rtc_event_log.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

// Together with the assumption of no single Write() would ever be called on
// an input with length greater-than-or-equal-to (max(size_t) / 2), this
// guarantees no overflow of the check for remaining file capacity in Write().
// This does *not* apply to files with unlimited size.
const size_t RtcEventLogOutputFile::kMaxReasonableFileSize =
    std::numeric_limits<size_t>::max() / 2;

RtcEventLogOutputFile::RtcEventLogOutputFile(const std::string& file_name)
    : RtcEventLogOutputFile(file_name, RtcEventLog::kUnlimitedOutput) {}

RtcEventLogOutputFile::RtcEventLogOutputFile(const std::string& file_name,
                                             size_t max_size_bytes)

    // Unlike plain fopen, CreatePlatformFile takes care of filename utf8 ->
    // wchar conversion on windows.
    : RtcEventLogOutputFile(rtc::CreatePlatformFile(file_name),
                            max_size_bytes) {}

RtcEventLogOutputFile::RtcEventLogOutputFile(rtc::PlatformFile file)
    : RtcEventLogOutputFile(file, RtcEventLog::kUnlimitedOutput) {}

RtcEventLogOutputFile::RtcEventLogOutputFile(rtc::PlatformFile platform_file,
                                             size_t max_size_bytes)
    : max_size_bytes_(max_size_bytes) {
  RTC_CHECK_LE(max_size_bytes_, kMaxReasonableFileSize);

  // Handle errors from the CreatePlatformFile call in above constructor.
  if (platform_file == rtc::kInvalidPlatformFileValue) {
    RTC_LOG(LS_ERROR) << "Invalid file. WebRTC event log not started.";
    return;
  }
  file_ = rtc::FdopenPlatformFileForWriting(platform_file);
  if (!file_) {
    RTC_LOG(LS_ERROR) << "Can't open file. WebRTC event log not started.";
    // Even though we failed to open a FILE*, the file is still open
    // and needs to be closed.
    if (!rtc::ClosePlatformFile(platform_file)) {
      RTC_LOG(LS_ERROR) << "Can't close file.";
    }
  }
}

RtcEventLogOutputFile::~RtcEventLogOutputFile() {
  if (file_) {
    fclose(file_);
  }
}

bool RtcEventLogOutputFile::IsActive() const {
  return IsActiveInternal();
}

bool RtcEventLogOutputFile::Write(const std::string& output) {
  RTC_DCHECK(IsActiveInternal());
  // No single write may be so big, that it would risk overflowing the
  // calculation of (written_bytes_ + output.length()).
  RTC_DCHECK_LT(output.length(), kMaxReasonableFileSize);

  if (max_size_bytes_ == RtcEventLog::kUnlimitedOutput ||
      written_bytes_ + output.length() <= max_size_bytes_) {
    if (fwrite(output.c_str(), 1, output.size(), file_) == output.size()) {
      written_bytes_ += output.size();
      return true;
    } else {
      RTC_LOG(LS_ERROR) << "Write to WebRtcEventLog file failed.";
    }
  } else {
    RTC_LOG(LS_VERBOSE) << "Max file size reached.";
  }

  // Failed, for one of above reasons. Close output file.
  fclose(file_);
  file_ = nullptr;
  return false;
}

// Internal non-virtual method.
bool RtcEventLogOutputFile::IsActiveInternal() const {
  return file_ != nullptr;
}

}  // namespace webrtc
