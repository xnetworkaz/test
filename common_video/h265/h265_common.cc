/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_video/h265/h265_common.h"

#include "common_video/h264/h264_common.h"

namespace webrtc {
namespace H265 {

constexpr uint8_t kNaluTypeMask = 0x7E;

std::vector<NaluIndex> FindNaluIndices(const uint8_t* buffer,
                                       size_t buffer_size) {
  std::vector<H264::NaluIndex> indices =
      H264::FindNaluIndices(buffer, buffer_size);
  std::vector<NaluIndex> results;
  for (auto& index : indices) {
    results.push_back(
        {index.start_offset, index.payload_start_offset, index.payload_size});
  }
  return results;
}

NaluType ParseNaluType(uint8_t data) {
  return static_cast<NaluType>((data & kNaluTypeMask) >> 1);
}

std::vector<uint8_t> ParseRbsp(const uint8_t* data, size_t length) {
  return H264::ParseRbsp(data, length);
}

void WriteRbsp(const uint8_t* bytes, size_t length, rtc::Buffer* destination) {
  H264::WriteRbsp(bytes, length, destination);
}

}  // namespace H265
}  // namespace webrtc
