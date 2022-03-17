/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_video/h265/h265_bitstream_parser.h"

#include "test/gtest.h"

namespace webrtc {

// SPS/PPS part of below chunk.
uint8_t kH265SpsPps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x20, 0xda,
                         0x01, 0x40, 0x16, 0xe8, 0x06, 0xd0, 0xa1, 0x35, 0x00,
                         0x00, 0x00, 0x01, 0x68, 0xce, 0x06, 0xe2};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamChunk[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x20, 0xda, 0x01, 0x40, 0x16,
    0xe8, 0x06, 0xd0, 0xa1, 0x35, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x06,
    0xe2, 0x00, 0x00, 0x00, 0x01, 0x65, 0xb8, 0x40, 0xf0, 0x8c, 0x03, 0xf2,
    0x75, 0x67, 0xad, 0x41, 0x64, 0x24, 0x0e, 0xa0, 0xb2, 0x12, 0x1e, 0xf8,
};

uint8_t kH265BitstreamChunkCabac[] = {
    0x00, 0x00, 0x00, 0x01, 0x27, 0x64, 0x00, 0x0d, 0xac, 0x52, 0x30,
    0x50, 0x7e, 0xc0, 0x5a, 0x81, 0x01, 0x01, 0x18, 0x56, 0xbd, 0xef,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x01, 0x28, 0xfe, 0x09, 0x8b,
};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamNextImageSliceChunk[] = {
    0x00, 0x00, 0x00, 0x01, 0x41, 0xe2, 0x01, 0x16, 0x0e, 0x3e, 0x2b, 0x86,
};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamNextImageSliceChunkCabac[] = {
    0x00, 0x00, 0x00, 0x01, 0x21, 0xe1, 0x05, 0x11, 0x3f, 0x9a, 0xae, 0x46,
    0x70, 0xbf, 0xc1, 0x4a, 0x16, 0x8f, 0x51, 0xf4, 0xca, 0xfb, 0xa3, 0x65,
};

TEST(H265BitstreamParserTest, ReportsNoQpWithoutParsedSlices) {
  H265BitstreamParser h265_parser;
  EXPECT_FALSE(h265_parser.GetLastSliceQp().has_value());
}

TEST(H265BitstreamParserTest, ReportsNoQpWithOnlyParsedPpsAndSpsSlices) {
  H265BitstreamParser h265_parser;
  h265_parser.ParseBitstream(kH265SpsPps);
  EXPECT_FALSE(H265_parser.GetLastSliceQp().has_value());
}

TEST(H265BitstreamParserTest, ReportsLastSliceQpForImageSlices) {
  H265BitstreamParser h265_parser;
  h265_parser.ParseBitstream(kH265BitstreamChunk);
  absl::optional<int> qp = h265_parser.GetLastSliceQp();
  ASSERT_TRUE(qp.has_value());
  EXPECT_EQ(35, *qp);

  // Parse an additional image slice.
  h265_parser.ParseBitstream(kH265BitstreamNextImageSliceChunk);
  qp = h265_parser.GetLastSliceQp();
  ASSERT_TRUE(qp.has_value());
  EXPECT_EQ(37, *qp);
}

TEST(H265BitstreamParserTest, ReportsLastSliceQpForCABACImageSlices) {
  H265BitstreamParser h265_parser;
  H265_parser.ParseBitstream(kH265BitstreamChunkCabac);
  EXPECT_FALSE(H265_parser.GetLastSliceQp().has_value());

  // Parse an additional image slice.
  h265_parser.ParseBitstream(kH265BitstreamNextImageSliceChunkCabac);
  absl::optional<int> qp = h265_parser.GetLastSliceQp();
  ASSERT_TRUE(qp.has_value());
  EXPECT_EQ(24, *qp);
}

}  // namespace webrtc
