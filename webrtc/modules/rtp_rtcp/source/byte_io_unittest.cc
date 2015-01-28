/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits>

#include "testing/gtest/include/gtest/gtest.h"
#include "webrtc/modules/rtp_rtcp/source/byte_io.h"

namespace webrtc {
namespace {

class ByteIoTest : public ::testing::Test {
 protected:
  ByteIoTest() {}
  virtual ~ByteIoTest() {}

  enum { kAlignments = sizeof(uint64_t) - 1 };

  // Method to create a test value that is not the same when byte reversed.
  template <typename T>
  T CreateTestValue(bool negative, uint8_t num_bytes) {
    T val = 0;
    for (uint8_t i = 0; i != num_bytes; ++i) {
      val = (val << 8) + (negative ? (0xFF - i) : (i + 1));
    }
    if (negative && std::numeric_limits<T>::is_signed) {
      val |= static_cast<T>(-1) << (8 * num_bytes);
    }
    return val;
  }

  // Populate byte buffer with value, in big endian format.
  template <typename T>
  void PopulateTestData(uint8_t* data, T value, int num_bytes, bool bigendian) {
    if (bigendian) {
      for (int i = 0; i < num_bytes; ++i) {
        data[i] = (value >> ((num_bytes - i - 1) * 8)) & 0xFF;
      }
    } else {
      for (int i = 0; i < num_bytes; ++i) {
        data[i] = (value >> (i * 8)) & 0xFF;
      }
    }
  }

  // Test reading big endian numbers.
  // Template arguments: Type T, read method RM(buffer), B bytes of data.
  template <typename T, T (*RM)(const uint8_t*), int B>
  void TestRead(bool big_endian) {
    // Test both for values that are positive and negative (if signed)
    for (int neg = 0; neg < 2; ++neg) {
      bool negative = neg > 0;

      // Write test value to byte buffer, in big endian format.
      T test_value = CreateTestValue<T>(negative, B);
      uint8_t bytes[B + kAlignments];

      // Make one test for each alignment.
      for (int i = 0; i < kAlignments; ++i) {
        PopulateTestData(bytes + i, test_value, B, big_endian);

        // Check that test value is retrieved from buffer when used read method.
        EXPECT_EQ(test_value, RM(bytes + i));
      }
    }
  }

  // Test writing big endian numbers.
  // Template arguments: Type T, write method WM(buffer, value), B bytes of data
  template <typename T, void (*WM)(uint8_t*, T), int B>
  void TestWrite(bool big_endian) {
    // Test both for values that are positive and negative (if signed).
    for (int neg = 0; neg < 2; ++neg) {
      bool negative = neg > 0;

      // Write test value to byte buffer, in big endian format.
      T test_value = CreateTestValue<T>(negative, B);
      uint8_t expected_bytes[B + kAlignments];
      uint8_t bytes[B + kAlignments];

      // Make one test for each alignment.
      for (int i = 0; i < kAlignments; ++i) {
        PopulateTestData(expected_bytes + i, test_value, B, big_endian);

        // Zero initialize buffer and let WM populate it.
        memset(bytes, 0, B + kAlignments);
        WM(bytes + i, test_value);

        // Check that data produced by WM is big endian as expected.
        for (int j = 0; j < B; ++j) {
          EXPECT_EQ(expected_bytes[i + j], bytes[i + j]);
        }
      }
    }
  }
};

TEST_F(ByteIoTest, Test16UBitBigEndian) {
  TestRead<uint16_t, ByteReader<uint16_t>::ReadBigEndian,
      sizeof(uint16_t)>(true);
  TestWrite<uint16_t, ByteWriter<uint16_t>::WriteBigEndian,
    sizeof(uint16_t)>(true);
}

TEST_F(ByteIoTest, Test24UBitBigEndian) {
  TestRead<uint32_t, ByteReader<uint32_t, 3>::ReadBigEndian, 3>(true);
  TestWrite<uint32_t, ByteWriter<uint32_t, 3>::WriteBigEndian, 3>(true);
}

TEST_F(ByteIoTest, Test32UBitBigEndian) {
  TestRead<uint32_t, ByteReader<uint32_t>::ReadBigEndian,
      sizeof(uint32_t)>(true);
  TestWrite<uint32_t, ByteWriter<uint32_t>::WriteBigEndian,
      sizeof(uint32_t)>(true);
}

TEST_F(ByteIoTest, Test64UBitBigEndian) {
  TestRead<uint64_t, ByteReader<uint64_t>::ReadBigEndian,
      sizeof(uint64_t)>(true);
  TestWrite<uint64_t, ByteWriter<uint64_t>::WriteBigEndian,
      sizeof(uint64_t)>(true);
}

TEST_F(ByteIoTest, Test16SBitBigEndian) {
  TestRead<int16_t, ByteReader<int16_t>::ReadBigEndian,
      sizeof(int16_t)>(true);
  TestWrite<int16_t, ByteWriter<int16_t>::WriteBigEndian,
      sizeof(int16_t)>(true);
}

TEST_F(ByteIoTest, Test24SBitBigEndian) {
  TestRead<int32_t, ByteReader<int32_t, 3>::ReadBigEndian, 3>(true);
  TestWrite<int32_t, ByteWriter<int32_t, 3>::WriteBigEndian, 3>(true);
}

TEST_F(ByteIoTest, Test32SBitBigEndian) {
  TestRead<int32_t, ByteReader<int32_t>::ReadBigEndian,
      sizeof(int32_t)>(true);
  TestWrite<int32_t, ByteWriter<int32_t>::WriteBigEndian,
      sizeof(int32_t)>(true);
}

TEST_F(ByteIoTest, Test64SBitBigEndian) {
  TestRead<int64_t, ByteReader<int64_t>::ReadBigEndian,
      sizeof(int64_t)>(true);
  TestWrite<int64_t, ByteWriter<int64_t>::WriteBigEndian,
      sizeof(int64_t)>(true);
}

TEST_F(ByteIoTest, Test16UBitLittleEndian) {
  TestRead<uint16_t, ByteReader<uint16_t>::ReadLittleEndian,
      sizeof(uint16_t)>(false);
  TestWrite<uint16_t, ByteWriter<uint16_t>::WriteLittleEndian,
      sizeof(uint16_t)>(false);
}

TEST_F(ByteIoTest, Test24UBitLittleEndian) {
  TestRead<uint32_t, ByteReader<uint32_t, 3>::ReadLittleEndian, 3>(false);
  TestWrite<uint32_t, ByteWriter<uint32_t, 3>::WriteLittleEndian, 3>(false);
}

TEST_F(ByteIoTest, Test32UBitLittleEndian) {
  TestRead<uint32_t, ByteReader<uint32_t>::ReadLittleEndian,
      sizeof(uint32_t)>(false);
  TestWrite<uint32_t, ByteWriter<uint32_t>::WriteLittleEndian,
      sizeof(uint32_t)>(false);
}

TEST_F(ByteIoTest, Test64UBitLittleEndian) {
  TestRead<uint64_t, ByteReader<uint64_t>::ReadLittleEndian,
      sizeof(uint64_t)>(false);
  TestWrite<uint64_t, ByteWriter<uint64_t>::WriteLittleEndian,
      sizeof(uint64_t)>(false);
}

TEST_F(ByteIoTest, Test16SBitLittleEndian) {
  TestRead<int16_t, ByteReader<int16_t>::ReadLittleEndian,
      sizeof(int16_t)>(false);
  TestWrite<int16_t, ByteWriter<int16_t>::WriteLittleEndian,
      sizeof(int16_t)>(false);
}

TEST_F(ByteIoTest, Test24SBitLittleEndian) {
  TestRead<int32_t, ByteReader<int32_t, 3>::ReadLittleEndian, 3>(false);
  TestWrite<int32_t, ByteWriter<int32_t, 3>::WriteLittleEndian, 3>(false);
}

TEST_F(ByteIoTest, Test32SBitLittleEndian) {
  TestRead<int32_t, ByteReader<int32_t>::ReadLittleEndian,
      sizeof(int32_t)>(false);
  TestWrite<int32_t, ByteWriter<int32_t>::WriteLittleEndian,
      sizeof(int32_t)>(false);
}

TEST_F(ByteIoTest, Test64SBitLittleEndian) {
  TestRead<int64_t, ByteReader<int64_t>::ReadLittleEndian,
      sizeof(int64_t)>(false);
  TestWrite<int64_t, ByteWriter<int64_t>::WriteLittleEndian,
      sizeof(int64_t)>(false);
}

}  // namespace
}  // namespace webrtc
