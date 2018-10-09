/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <string>
#include <vector>

#include "p2p/base/icecredentialsiterator.h"
#include "rtc_base/gunit.h"

using cricket::IceParameters;
using cricket::IceCredentialsIterator;

class IceCredentialsIteratorTest : public testing::Test {
 public:
  IceCredentialsIteratorTest() {}
};

TEST_F(IceCredentialsIteratorTest, GetEmpty) {
  std::vector<IceParameters> empty;
  IceCredentialsIterator iterator(empty);
  // Verify that we can get credentials even if input is empty.
  IceParameters credentials1 = iterator.GetIceCredentials();
}

TEST_F(IceCredentialsIteratorTest, GetOne) {
  std::vector<IceParameters> one = {
      IceCredentialsIterator::CreateRandomIceCredentials()};
  IceCredentialsIterator iterator(one);
  EXPECT_EQ(iterator.GetIceCredentials(), one[0]);
  auto random = iterator.GetIceCredentials();
  EXPECT_NE(random, one[0]);
  EXPECT_NE(random, iterator.GetIceCredentials());
}

TEST_F(IceCredentialsIteratorTest, GetTwo) {
  std::vector<IceParameters> two = {
      IceCredentialsIterator::CreateRandomIceCredentials(),
      IceCredentialsIterator::CreateRandomIceCredentials()};
  IceCredentialsIterator iterator(two);
  EXPECT_EQ(iterator.GetIceCredentials(), two[1]);
  EXPECT_EQ(iterator.GetIceCredentials(), two[0]);
  auto random = iterator.GetIceCredentials();
  EXPECT_NE(random, two[0]);
  EXPECT_NE(random, two[1]);
  EXPECT_NE(random, iterator.GetIceCredentials());
}
