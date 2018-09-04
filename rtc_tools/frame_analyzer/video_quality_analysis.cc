/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/frame_analyzer/video_quality_analysis.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "test/testsupport/perf_test.h"
#include "third_party/libyuv/include/libyuv/compare.h"
#include "third_party/libyuv/include/libyuv/convert.h"

#define STATS_LINE_LENGTH 32

namespace webrtc {
namespace test {

ResultsContainer::ResultsContainer() {}
ResultsContainer::~ResultsContainer() {}

int GetI420FrameSize(int width, int height) {
  int half_width = (width + 1) >> 1;
  int half_height = (height + 1) >> 1;

  int y_plane = width * height;            // I420 Y plane.
  int u_plane = half_width * half_height;  // I420 U plane.
  int v_plane = half_width * half_height;  // I420 V plane.

  return y_plane + u_plane + v_plane;
}

int ExtractFrameSequenceNumber(std::string line) {
  size_t space_position = line.find(' ');
  if (space_position == std::string::npos) {
    return -1;
  }
  std::string frame = line.substr(0, space_position);

  size_t underscore_position = frame.find('_');
  if (underscore_position == std::string::npos) {
    return -1;
  }
  std::string frame_number = frame.substr(underscore_position + 1);

  return strtol(frame_number.c_str(), NULL, 10);
}

int ExtractDecodedFrameNumber(std::string line) {
  size_t space_position = line.find(' ');
  if (space_position == std::string::npos) {
    return -1;
  }
  std::string decoded_number = line.substr(space_position + 1);

  return strtol(decoded_number.c_str(), NULL, 10);
}

bool IsThereBarcodeError(std::string line) {
  size_t barcode_error_position = line.find("Barcode error");
  if (barcode_error_position != std::string::npos) {
    return true;
  }
  return false;
}

bool GetNextStatsLine(FILE* stats_file, char* line) {
  int chars = 0;
  char buf = 0;

  while (buf != '\n') {
    size_t chars_read = fread(&buf, 1, 1, stats_file);
    if (chars_read != 1 || feof(stats_file)) {
      return false;
    }
    line[chars] = buf;
    ++chars;
  }
  line[chars - 1] = '\0';  // Strip the trailing \n and put end of string.
  return true;
}

template <typename FrameMetricFunction>
static double CalculateMetric(
    const FrameMetricFunction& frame_metric_function,
    const rtc::scoped_refptr<I420BufferInterface>& ref_buffer,
    const rtc::scoped_refptr<I420BufferInterface>& test_buffer) {
  RTC_CHECK_EQ(ref_buffer->width(), test_buffer->width());
  RTC_CHECK_EQ(ref_buffer->height(), test_buffer->height());
  return frame_metric_function(
      ref_buffer->DataY(), ref_buffer->StrideY(), ref_buffer->DataU(),
      ref_buffer->StrideU(), ref_buffer->DataV(), ref_buffer->StrideV(),
      test_buffer->DataY(), test_buffer->StrideY(), test_buffer->DataU(),
      test_buffer->StrideU(), test_buffer->DataV(), test_buffer->StrideV(),
      test_buffer->width(), test_buffer->height());
}

double Psnr(const rtc::scoped_refptr<I420BufferInterface>& ref_buffer,
            const rtc::scoped_refptr<I420BufferInterface>& test_buffer) {
  // LibYuv sets the max psnr value to 128, we restrict it to 48.
  // In case of 0 mse in one frame, 128 can skew the results significantly.
  return std::min(48.0,
                  CalculateMetric(&libyuv::I420Psnr, ref_buffer, test_buffer));
}

double Ssim(const rtc::scoped_refptr<I420BufferInterface>& ref_buffer,
            const rtc::scoped_refptr<I420BufferInterface>& test_buffer) {
  return CalculateMetric(&libyuv::I420Ssim, ref_buffer, test_buffer);
}

void RunAnalysis(const rtc::scoped_refptr<webrtc::test::Video>& reference_video,
                 const rtc::scoped_refptr<webrtc::test::Video>& test_video,
                 const char* stats_file_reference_name,
                 const char* stats_file_test_name,
                 int width,
                 int height,
                 ResultsContainer* results) {
  FILE* stats_file_ref = fopen(stats_file_reference_name, "r");
  FILE* stats_file_test = fopen(stats_file_test_name, "r");

  // String buffer for the lines in the stats file.
  char line[STATS_LINE_LENGTH];

  int previous_frame_number = -1;

  // Maps barcode id to the frame id for the reference video.
  // In case two frames have same id, then we only save the first one.
  std::map<int, int> ref_barcode_to_frame;
  // While there are entries in the stats file.
  while (GetNextStatsLine(stats_file_ref, line)) {
    int extracted_ref_frame = ExtractFrameSequenceNumber(line);
    int decoded_frame_number = ExtractDecodedFrameNumber(line);

    // Insert will only add if it is not in map already.
    ref_barcode_to_frame.insert(
        std::make_pair(decoded_frame_number, extracted_ref_frame));
  }

  while (GetNextStatsLine(stats_file_test, line)) {
    int extracted_test_frame = ExtractFrameSequenceNumber(line);
    int decoded_frame_number = ExtractDecodedFrameNumber(line);
    auto it = ref_barcode_to_frame.find(decoded_frame_number);
    if (it == ref_barcode_to_frame.end()) {
      // Not found in the reference video.
      // TODO(mandermo) print
      continue;
    }
    int extracted_ref_frame = it->second;

    // If there was problem decoding the barcode in this frame or the frame has
    // been duplicated, continue.
    if (IsThereBarcodeError(line) ||
        decoded_frame_number == previous_frame_number) {
      continue;
    }

    assert(extracted_test_frame != -1);
    assert(decoded_frame_number != -1);

    const rtc::scoped_refptr<webrtc::I420BufferInterface> test_frame =
        test_video->GetFrame(extracted_test_frame);
    const rtc::scoped_refptr<webrtc::I420BufferInterface> reference_frame =
        reference_video->GetFrame(extracted_ref_frame);

    // Calculate the PSNR and SSIM.
    double result_psnr = Psnr(reference_frame, test_frame);
    double result_ssim = Ssim(reference_frame, test_frame);

    previous_frame_number = decoded_frame_number;

    // Fill in the result struct.
    AnalysisResult result;
    result.frame_number = decoded_frame_number;
    result.psnr_value = result_psnr;
    result.ssim_value = result_ssim;

    results->frames.push_back(result);
  }

  // Cleanup.
  fclose(stats_file_ref);
  fclose(stats_file_test);
}

std::vector<std::pair<int, int> > CalculateFrameClusters(
    FILE* file,
    int* num_decode_errors) {
  if (num_decode_errors) {
    *num_decode_errors = 0;
  }
  std::vector<std::pair<int, int> > frame_cnt;
  char line[STATS_LINE_LENGTH];
  while (GetNextStatsLine(file, line)) {
    int decoded_frame_number;
    if (IsThereBarcodeError(line)) {
      decoded_frame_number = DECODE_ERROR;
      if (num_decode_errors) {
        ++*num_decode_errors;
      }
    } else {
      decoded_frame_number = ExtractDecodedFrameNumber(line);
    }
    if (frame_cnt.size() >= 2 && decoded_frame_number != DECODE_ERROR &&
        frame_cnt.back().first == DECODE_ERROR &&
        frame_cnt[frame_cnt.size() - 2].first == decoded_frame_number) {
      // Handle when there is a decoding error inside a cluster of frames.
      frame_cnt[frame_cnt.size() - 2].second += frame_cnt.back().second + 1;
      frame_cnt.pop_back();
    } else if (frame_cnt.empty() ||
               frame_cnt.back().first != decoded_frame_number) {
      frame_cnt.push_back(std::make_pair(decoded_frame_number, 1));
    } else {
      ++frame_cnt.back().second;
    }
  }
  return frame_cnt;
}

void GetMaxRepeatedAndSkippedFrames(const std::string& stats_file_ref_name,
                                    const std::string& stats_file_test_name,
                                    ResultsContainer* results) {
  FILE* stats_file_ref = fopen(stats_file_ref_name.c_str(), "r");
  FILE* stats_file_test = fopen(stats_file_test_name.c_str(), "r");
  if (stats_file_ref == NULL) {
    fprintf(stderr, "Couldn't open reference stats file for reading: %s\n",
            stats_file_ref_name.c_str());
    return;
  }
  if (stats_file_test == NULL) {
    fprintf(stderr, "Couldn't open test stats file for reading: %s\n",
            stats_file_test_name.c_str());
    fclose(stats_file_ref);
    return;
  }

  int max_repeated_frames = 1;
  int max_skipped_frames = 0;

  int decode_errors_ref = 0;
  int decode_errors_test = 0;

  std::vector<std::pair<int, int> > frame_cnt_ref =
      CalculateFrameClusters(stats_file_ref, &decode_errors_ref);

  std::vector<std::pair<int, int> > frame_cnt_test =
      CalculateFrameClusters(stats_file_test, &decode_errors_test);

  fclose(stats_file_ref);
  fclose(stats_file_test);

  auto it_ref = frame_cnt_ref.begin();
  auto it_test = frame_cnt_test.begin();
  auto end_ref = frame_cnt_ref.end();
  auto end_test = frame_cnt_test.end();

  if (it_test == end_test || it_ref == end_ref) {
    fprintf(stderr, "Either test or ref file is empty, nothing to print\n");
    return;
  }

  while (it_test != end_test && it_test->first == DECODE_ERROR) {
    ++it_test;
  }

  if (it_test == end_test) {
    fprintf(stderr, "Test video only has barcode decode errors\n");
    return;
  }

  // Find the first frame in the reference video that match the first frame in
  // the test video.
  while (it_ref != end_ref &&
         (it_ref->first == DECODE_ERROR || it_ref->first != it_test->first)) {
    ++it_ref;
  }
  if (it_ref == end_ref) {
    fprintf(stderr,
            "The barcode in the test video's first frame is not in the "
            "reference video.\n");
    return;
  }

  int total_skipped_frames = 0;
  for (;;) {
    max_repeated_frames =
        std::max(max_repeated_frames, it_test->second - it_ref->second + 1);

    bool passed_error = false;

    ++it_test;
    while (it_test != end_test && it_test->first == DECODE_ERROR) {
      ++it_test;
      passed_error = true;
    }
    if (it_test == end_test) {
      break;
    }

    int skipped_frames = 0;
    ++it_ref;
    for (; it_ref != end_ref; ++it_ref) {
      if (it_ref->first != DECODE_ERROR && it_ref->first >= it_test->first) {
        break;
      }
      ++skipped_frames;
    }
    if (passed_error) {
      // If we pass an error in the test video, then we are conservative
      // and will not calculate skipped frames for that part.
      skipped_frames = 0;
    }
    if (it_ref != end_ref && it_ref->first == it_test->first) {
      total_skipped_frames += skipped_frames;
      if (skipped_frames > max_skipped_frames) {
        max_skipped_frames = skipped_frames;
      }
      continue;
    }
    fprintf(stdout,
            "Found barcode %d in test video, which is not in reference video\n",
            it_test->first);
    break;
  }

  results->max_repeated_frames = max_repeated_frames;
  results->max_skipped_frames = max_skipped_frames;
  results->total_skipped_frames = total_skipped_frames;
  results->decode_errors_ref = decode_errors_ref;
  results->decode_errors_test = decode_errors_test;
}

void PrintAnalysisResults(const std::string& label, ResultsContainer* results) {
  PrintAnalysisResults(stdout, label, results);
}

void PrintAnalysisResults(FILE* output,
                          const std::string& label,
                          ResultsContainer* results) {
  SetPerfResultsOutput(output);

  if (results->frames.size() > 0u) {
    PrintResult("Unique_frames_count", "", label, results->frames.size(),
                "score", false);

    std::vector<double> psnr_values;
    std::vector<double> ssim_values;
    for (const auto& frame : results->frames) {
      psnr_values.push_back(frame.psnr_value);
      ssim_values.push_back(frame.ssim_value);
    }

    PrintResultList("PSNR", "", label, psnr_values, "dB", false);
    PrintResultList("SSIM", "", label, ssim_values, "score", false);
  }

  PrintResult("Max_repeated", "", label, results->max_repeated_frames, "",
              false);
  PrintResult("Max_skipped", "", label, results->max_skipped_frames, "", false);
  PrintResult("Total_skipped", "", label, results->total_skipped_frames, "",
              false);
  PrintResult("Decode_errors_reference", "", label, results->decode_errors_ref,
              "", false);
  PrintResult("Decode_errors_test", "", label, results->decode_errors_test, "",
              false);
}

}  // namespace test
}  // namespace webrtc
