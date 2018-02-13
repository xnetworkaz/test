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
#include "test/gtest.h"

#if RTC_HAS_MSAN
#include <sanitizer/msan_interface.h>
#endif

namespace rtc {
namespace {

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
  __msan_set_expect_umr(1);
#endif
  f();
#if RTC_HAS_MSAN
  // Disallow uninitialized memory reads again, and verify that at least
  // one uninitialized memory read happened while we weren't looking.
  __msan_set_expect_umr(0);
#endif
}

}  // namespace

TEST(SanitizerTest, MsanUninitialized) {
  Bar bar = MsanUninitialized<Bar>({});
  // Check that a read after initialization is OK.
  bar.ID = 1;
  EXPECT_EQ(1u, bar.ID);
  // Check that an uninitialized read is not OK.
  MsanExpectUninitializedRead([&] { EXPECT_EQ(0u, bar.foo.field1); });
  MsanExpectUninitializedRead([&] { EXPECT_EQ(0u, bar.foo.field2); });
}

}  // namespace rtc
