/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "common_video/h265/h265_bitstream_parser.h"

#include "common_video/h265/h265_common.h"
#include "test/gtest.h"

namespace webrtc {

// VPS/SPS/PPS part of below chunk.
uint8_t kH265VpsSpsPps[] = {
    0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x04, 0x08,
    0x00, 0x00, 0x03, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78,
    0x95, 0x98, 0x09, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x04, 0x08,
    0x00, 0x00, 0x03, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78,
    0xb0, 0x03, 0xc0, 0x80, 0x10, 0xe5, 0x96, 0x56, 0x69, 0x24, 0xca, 0xe0,
    0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0xe0, 0x80,
    0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamChunk[] = {
    0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x04, 0x08,
    0x00, 0x00, 0x03, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78,
    0x95, 0x98, 0x09, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x04, 0x08,
    0x00, 0x00, 0x03, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78,
    0xb0, 0x03, 0xc0, 0x80, 0x10, 0xe5, 0x96, 0x56, 0x69, 0x24, 0xca, 0xe0,
    0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0xe0, 0x80,
    0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40, 0x00,
    0x00, 0x01, 0x26, 0x01, 0xaf, 0x08, 0x42, 0x23, 0x10, 0x5d, 0x2b, 0x51,
    0xf9, 0x7a, 0x55, 0x15, 0x0d, 0x10, 0x40, 0xe8, 0x10, 0x05, 0x30, 0x95,
    0x09, 0x9a, 0xa5, 0xb6, 0x6a, 0x66, 0x6d, 0xde, 0xe0, 0xf9,
};

uint8_t kH265BitstreamChunkCabac[] = {
    0x00, 0x00, 0x00, 0x01, 0x27, 0x64, 0x00, 0x0d, 0xac, 0x52, 0x30,
    0x50, 0x7e, 0xc0, 0x5a, 0x81, 0x01, 0x01, 0x18, 0x56, 0xbd, 0xef,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x01, 0x28, 0xfe, 0x09, 0x8b,
};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamNextImageSliceChunk[] = {
    0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0xe0, 0x24, 0xbf, 0x82, 0x05,
    0x21, 0x12, 0x22, 0xa3, 0x29, 0xb4, 0x21, 0x91, 0xa1, 0xaa, 0x40,
};

// Contains enough of the image slice to contain slice QP.
uint8_t kH265BitstreamNextImageSliceChunkCabac[] = {
    0x00, 0x00, 0x00, 0x01, 0x21, 0xe1, 0x05, 0x11, 0x3f, 0x9a, 0xae, 0x46,
    0x70, 0xbf, 0xc1, 0x4a, 0x16, 0x8f, 0x51, 0xf4, 0xca, 0xfb, 0xa3, 0x65,
};

// Contains enough of the image slice to contain slice QP.
const uint8_t kH265SliceChunk[] = {
    0xa4, 0x04, 0x55, 0xa2, 0x6d, 0xce, 0xc0, 0xc3, 0xed, 0x0b, 0xac, 0xbc,
    0x00, 0xc4, 0x44, 0x2e, 0xf7, 0x55, 0xfd, 0x05, 0x86, 0x92, 0x19, 0xdf,
    0x58, 0xec, 0x38, 0x36, 0xb7, 0x7c, 0x00, 0x15, 0x33, 0x78, 0x03, 0x67,
    0x26, 0x0f, 0x7b, 0x30, 0x1c, 0xd7, 0xd4, 0x3a, 0xec, 0xad, 0xef, 0x73,
};

// Contains short term ref pic set slice to verify Log2Ceiling path.
const uint8_t kH265SliceStrChunk[] = {
    0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x99, 0x94, 0x90, 0x24, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x99, 0xa0, 0x01, 0x40, 0x20, 0x06, 0x41, 0xfe, 0x59,
    0x49, 0x26, 0x4d, 0x86, 0x16, 0x22, 0xaa, 0x4c, 0x4c, 0x32, 0xfb, 0x3e,
    0xbc, 0xdf, 0x96, 0x7d, 0x78, 0x51, 0x18, 0x9c, 0xbb, 0x20, 0x00, 0x00,
    0x00, 0x01, 0x44, 0x01, 0xc1, 0xa5, 0x58, 0x11, 0x20, 0x00, 0x00, 0x01,
    0x02, 0x01, 0xe1, 0x18, 0xfe, 0x47, 0x60, 0xd2, 0x74, 0xd6, 0x9f, 0xfc,
    0xbe, 0x6b, 0x15, 0x48, 0x59, 0x1f, 0xf7, 0xc1, 0x7c, 0xe2, 0xe8, 0x10,
};

TEST(H265BitstreamParserTest, ReportsNoQpWithoutParsedSlices) {
  H265BitstreamParser h265_parser;
  EXPECT_FALSE(h265_parser.GetLastSliceQp().has_value());
}

TEST(H265BitstreamParserTest, ReportsNoQpWithOnlyParsedPpsAndSpsSlices) {
  H265BitstreamParser h265_parser;
  h265_parser.ParseBitstream(kH265VpsSpsPps);
  EXPECT_FALSE(h265_parser.GetLastSliceQp().has_value());
}

TEST(H265BitstreamParserTest, ReportsLastSliceQpForImageSlices) {
  H265BitstreamParser h265_parser;
  h265_parser.ParseBitstream(kH265BitstreamChunk);
  absl::optional<int> qp = h265_parser.GetLastSliceQp();
  ASSERT_TRUE(qp.has_value());
  EXPECT_EQ(34, *qp);

  // Parse an additional image slice.
  h265_parser.ParseBitstream(kH265BitstreamNextImageSliceChunk);
  qp = h265_parser.GetLastSliceQp();
  ASSERT_TRUE(qp.has_value());
  EXPECT_EQ(36, *qp);
}

TEST(H265BitstreamParserTest,
     ReportsNoQpWithOnlyParsedShortTermReferenceSlices) {
  H265BitstreamParser h265_parser;
  h265_parser.ParseBitstream(kH265SliceStrChunk);
  EXPECT_TRUE(h265_parser.GetLastSliceQp().has_value());
}

TEST(H265BitstreamParserTest, PpsIdFromSlice) {
  H265BitstreamParser h265_parser;
  absl::optional<uint32_t> pps_id =
      h265_parser.ParsePpsIdFromSliceSegmentLayerRbsp(
          kH265SliceChunk, sizeof(kH265SliceChunk), H265::NaluType::kTrailR);
  ASSERT_TRUE(pps_id);
  EXPECT_EQ(1u, *pps_id);
}

}  // namespace webrtc
