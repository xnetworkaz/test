/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_tools/rtc_event_log_to_text/converter.h"

ABSL_FLAG(bool,
          parse_unconfigured_header_extensions,
          true,
          "Attempt to parse unconfigured header extensions using the default "
          "WebRTC mapping. This can give very misleading results if the "
          "application negotiates a different mapping.");

int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(
      "A tool for converting WebRTC event logs to text.\n"
      "Example usage:\n"
      "./rtc_event_log_to_text <inputfile> <outputfile>\n"
      "./rtc_event_log_to_text <inputfile>\n");
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  // Print RTC_LOG warnings and errors even in release builds.
  if (rtc::LogMessage::GetLogToDebug() > rtc::LS_WARNING) {
    rtc::LogMessage::LogToDebug(rtc::LS_WARNING);
  }
  rtc::LogMessage::SetLogToStderr(true);

  webrtc::ParsedRtcEventLog::UnconfiguredHeaderExtensions header_extensions =
      webrtc::ParsedRtcEventLog::UnconfiguredHeaderExtensions::kDontParse;
  if (absl::GetFlag(FLAGS_parse_unconfigured_header_extensions)) {
    header_extensions = webrtc::ParsedRtcEventLog::
        UnconfiguredHeaderExtensions::kAttemptWebrtcDefaultConfig;
  }

  std::string inputfile;
  FILE* output = nullptr;
  if (args.size() == 3) {
    inputfile = args[1];
    std::string outputfile = args[2];
    output = fopen(outputfile.c_str(), "r");
  } else if (args.size() == 2) {
    inputfile = args[1];
    output = stdout;
  } else {
    // Print usage information.
    std::cerr << absl::ProgramUsageMessage();
    return 1;
  }

  bool success = webrtc::Convert(inputfile, output, header_extensions);

  return success ? 0 : 1;
}
