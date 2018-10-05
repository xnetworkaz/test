/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/bytebuffer.h"

#include <string.h>

#include <algorithm>

namespace rtc {

ByteBufferWriter::ByteBufferWriter() : ByteBufferWriterT() {}

ByteBufferWriter::ByteBufferWriter(ByteOrder byte_order)
    : ByteBufferWriterT(byte_order) {}

ByteBufferWriter::ByteBufferWriter(const char* bytes, size_t len)
    : ByteBufferWriterT(bytes, len) {}

ByteBufferWriter::ByteBufferWriter(const char* bytes,
                                   size_t len,
                                   ByteOrder byte_order)
    : ByteBufferWriterT(bytes, len, byte_order) {}

ByteBufferReader::ByteBufferReader(const char* bytes, size_t len)
    : ByteBuffer(ORDER_NETWORK) {
  Construct(bytes, len);
}

ByteBufferReader::ByteBufferReader(const char* bytes,
                                   size_t len,
                                   ByteOrder byte_order)
    : ByteBuffer(byte_order) {
  Construct(bytes, len);
}

ByteBufferReader::ByteBufferReader(const char* bytes)
    : ByteBuffer(ORDER_NETWORK) {
  Construct(bytes, strlen(bytes));
}

ByteBufferReader::ByteBufferReader(const Buffer& buf)
    : ByteBuffer(ORDER_NETWORK) {
  Construct(buf.data<char>(), buf.size());
}

ByteBufferReader::ByteBufferReader(const ByteBufferWriter& buf)
    : ByteBuffer(buf.Order()) {
  Construct(buf.Data(), buf.Length());
}

void ByteBufferReader::Construct(const char* bytes, size_t len) {
  bytes_ = bytes;
  size_ = len;
  start_ = 0;
  end_ = len;
}

bool ByteBufferReader::ReadUInt8(uint8_t* val) {
  if (!val)
    return false;

  return ReadBytes(reinterpret_cast<char*>(val), 1);
}

bool ByteBufferReader::ReadUInt16(uint16_t* val) {
  if (!val)
    return false;

  uint16_t v;
  if (!ReadBytes(reinterpret_cast<char*>(&v), 2)) {
    return false;
  } else {
    *val = (Order() == ORDER_NETWORK) ? NetworkToHost16(v) : v;
    return true;
  }
}

bool ByteBufferReader::ReadUInt24(uint32_t* val) {
  if (!val)
    return false;

  uint32_t v = 0;
  char* read_into = reinterpret_cast<char*>(&v);
  if (Order() == ORDER_NETWORK || IsHostBigEndian()) {
    ++read_into;
  }

  if (!ReadBytes(read_into, 3)) {
    return false;
  } else {
    *val = (Order() == ORDER_NETWORK) ? NetworkToHost32(v) : v;
    return true;
  }
}

bool ByteBufferReader::ReadUInt32(uint32_t* val) {
  if (!val)
    return false;

  uint32_t v;
  if (!ReadBytes(reinterpret_cast<char*>(&v), 4)) {
    return false;
  } else {
    *val = (Order() == ORDER_NETWORK) ? NetworkToHost32(v) : v;
    return true;
  }
}

bool ByteBufferReader::ReadUInt64(uint64_t* val) {
  if (!val)
    return false;

  uint64_t v;
  if (!ReadBytes(reinterpret_cast<char*>(&v), 8)) {
    return false;
  } else {
    *val = (Order() == ORDER_NETWORK) ? NetworkToHost64(v) : v;
    return true;
  }
}

bool ByteBufferReader::ReadUVarint(uint64_t* val) {
  if (!val) {
    return false;
  }
  // Integers are deserialized 7 bits at a time, with each byte having a
  // continuation byte (msb=1) if there are more bytes to be read.
  uint64_t v = 0;
  for (int i = 0; i < 64; i += 7) {
    char byte;
    if (!ReadBytes(&byte, 1)) {
      return false;
    }
    // Read the first 7 bits of the byte, then offset by bits read so far.
    v |= (static_cast<uint64_t>(byte) & 0x7F) << i;
    // True if the msb is not a continuation byte.
    if (static_cast<uint64_t>(byte) < 0x80) {
      *val = v;
      return true;
    }
  }
  return false;
}

bool ByteBufferReader::ReadString(std::string* val, size_t len) {
  if (!val)
    return false;

  if (len > Length()) {
    return false;
  } else {
    val->append(bytes_ + start_, len);
    start_ += len;
    return true;
  }
}

bool ByteBufferReader::ReadBytes(char* val, size_t len) {
  if (len > Length()) {
    return false;
  } else {
    memcpy(val, bytes_ + start_, len);
    start_ += len;
    return true;
  }
}

bool ByteBufferReader::Consume(size_t size) {
  if (size > Length())
    return false;
  start_ += size;
  return true;
}

}  // namespace rtc
