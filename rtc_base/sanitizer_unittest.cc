/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/sanitizer.h"

#include "rtc_base/gunit.h"
#include "rtc_base/logging.h"

#if RTC_HAS_MSAN
#include <sanitizer/msan_interface.h>
#endif

namespace rtc {
namespace {

// Test sanitizer_impl::IsTriviallyCopyable (at compile time).

// Trivially copyable.

struct TrTrTr {
  TrTrTr(const TrTrTr&) = default;
  TrTrTr& operator=(const TrTrTr&) = default;
  ~TrTrTr() = default;
};
static_assert(sanitizer_impl::IsTriviallyCopyable<TrTrTr>(), "");

struct TrDeTr {
  TrDeTr(const TrDeTr&) = default;
  TrDeTr& operator=(const TrDeTr&) = delete;
  ~TrDeTr() = default;
};
static_assert(sanitizer_impl::IsTriviallyCopyable<TrDeTr>(), "");

// Non trivially copyable.

struct TrTrNt {
  TrTrNt(const TrTrNt&) = default;
  TrTrNt& operator=(const TrTrNt&) = default;
  ~TrTrNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<TrTrNt>(), "");

struct TrNtTr {
  TrNtTr(const TrNtTr&) = default;
  TrNtTr& operator=(const TrNtTr&);
  ~TrNtTr() = default;
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<TrNtTr>(), "");

struct TrNtNt {
  TrNtNt(const TrNtNt&) = default;
  TrNtNt& operator=(const TrNtNt&);
  ~TrNtNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<TrNtNt>(), "");

struct TrDeNt {
  TrDeNt(const TrDeNt&) = default;
  TrDeNt& operator=(const TrDeNt&) = delete;
  ~TrDeNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<TrDeNt>(), "");

struct NtTrTr {
  NtTrTr(const NtTrTr&);
  NtTrTr& operator=(const NtTrTr&) = default;
  ~NtTrTr() = default;
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtTrTr>(), "");

struct NtTrNt {
  NtTrNt(const NtTrNt&);
  NtTrNt& operator=(const NtTrNt&) = default;
  ~NtTrNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtTrNt>(), "");

struct NtNtTr {
  NtNtTr(const NtNtTr&);
  NtNtTr& operator=(const NtNtTr&);
  ~NtNtTr() = default;
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtNtTr>(), "");

struct NtNtNt {
  NtNtNt(const NtNtNt&);
  NtNtNt& operator=(const NtNtNt&);
  ~NtNtNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtNtNt>(), "");

struct NtDeTr {
  NtDeTr(const NtDeTr&);
  NtDeTr& operator=(const NtDeTr&) = delete;
  ~NtDeTr() = default;
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtDeTr>(), "");

struct NtDeNt {
  NtDeNt(const NtDeNt&);
  NtDeNt& operator=(const NtDeNt&) = delete;
  ~NtDeNt();
};
static_assert(!sanitizer_impl::IsTriviallyCopyable<NtDeNt>(), "");

// TODO(alessiob): Remove.
struct ChunkHeader {
  uint32_t ID;
  uint32_t Size;
};
struct RiffHeader {
  ChunkHeader header;
  uint32_t Format;
};
struct FmtSubchunk {
  ChunkHeader header;
  uint16_t AudioFormat;
  uint16_t NumChannels;
  uint32_t SampleRate;
  uint32_t ByteRate;
  uint16_t BlockAlign;
  uint16_t BitsPerSample;
};
struct WavHeader {
  RiffHeader riff;
  FmtSubchunk fmt;
  ChunkHeader data_header;
};
static_assert(sanitizer_impl::IsTriviallyCopyable<WavHeader>(), "");

// Trivially copyable types.

struct Foo {
  uint32_t field1;
  uint16_t field2;
};

struct Bar {
  uint32_t ID;
  Foo foo;
};

// Run the callback, and crash if it *doesn't* make an uninitialized memory
// read. If MSan isn't on, just run the callback.
template <typename F>
void MsanExpectUninitializedRead(F&& f) {
#if RTC_HAS_MSAN
  // Allow uninitialized memory reads.
  RTC_LOG(LS_INFO) << "__msan_set_expect_umr(1)";
  __msan_set_expect_umr(1);
#endif
  f();
#if RTC_HAS_MSAN
  // Disallow uninitialized memory reads again, and verify that at least
  // one uninitialized memory read happened while we weren't looking.
  RTC_LOG(LS_INFO) << "__msan_set_expect_umr(0)";
  __msan_set_expect_umr(0);
#endif
}

}  // namespace

// TODO(b/9116): Enable the test when the bug is fixed.
TEST(SanitizerTest, DISABLED_MsanUninitialized) {
  Bar bar = MsanUninitialized<Bar>({});
  // Check that a read after initialization is OK.
  bar.ID = 1;
  EXPECT_EQ(1u, bar.ID);
  RTC_LOG(LS_INFO) << "read after init passed";
  // Check that other fields are uninitialized and equal to zero.
  MsanExpectUninitializedRead([&] { EXPECT_EQ(0u, bar.foo.field1); });
  MsanExpectUninitializedRead([&] { EXPECT_EQ(0u, bar.foo.field2); });
  RTC_LOG(LS_INFO) << "read with no init passed";
}

}  // namespace rtc
