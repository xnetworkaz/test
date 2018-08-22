/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstring>
#include <memory>

#include "modules/video_coding/include/video_coding_defines.h"
#include "modules/video_coding/nack_module.h"
#include "system_wrappers/include/clock.h"
#include "test/gtest.h"

namespace webrtc {
class TestNackModule : public ::testing::Test,
                       public NackSender,
                       public KeyFrameRequestSender {
 protected:
  TestNackModule()
      : clock_(new SimulatedClock(0)),
        nack_module_(clock_.get(), this, this),
        keyframes_requested_(0) {}

  void SendNack(const std::vector<uint16_t>& sequence_numbers) override {
    sent_nacks_.insert(sent_nacks_.end(), sequence_numbers.begin(),
                       sequence_numbers.end());
  }

  void RequestKeyFrame() override { ++keyframes_requested_; }

  std::unique_ptr<SimulatedClock> clock_;
  NackModule nack_module_;
  std::vector<uint16_t> sent_nacks_;
  int keyframes_requested_;
};

TEST_F(TestNackModule, NackOnePacket) {
  nack_module_.OnReceivedPacket(1, false);
  nack_module_.OnReceivedPacket(3, false);
  EXPECT_EQ(1u, sent_nacks_.size());
  EXPECT_EQ(2, sent_nacks_[0]);
}

TEST_F(TestNackModule, WrappingSeqNum) {
  nack_module_.OnReceivedPacket(0xfffe, false);
  nack_module_.OnReceivedPacket(1, false);
  EXPECT_EQ(2u, sent_nacks_.size());
  EXPECT_EQ(0xffff, sent_nacks_[0]);
  EXPECT_EQ(0, sent_nacks_[1]);
}

TEST_F(TestNackModule, WrappingSeqNumClearToKeyframe) {
  nack_module_.OnReceivedPacket(0xfffe, false);
  nack_module_.OnReceivedPacket(1, false);
  EXPECT_EQ(2u, sent_nacks_.size());
  EXPECT_EQ(0xffff, sent_nacks_[0]);
  EXPECT_EQ(0, sent_nacks_[1]);

  sent_nacks_.clear();
  nack_module_.OnReceivedPacket(2, true);
  EXPECT_EQ(0u, sent_nacks_.size());

  nack_module_.OnReceivedPacket(501, true);
  EXPECT_EQ(498u, sent_nacks_.size());
  for (int seq_num = 3; seq_num < 501; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 3]);

  sent_nacks_.clear();
  nack_module_.OnReceivedPacket(1001, false);
  EXPECT_EQ(499u, sent_nacks_.size());
  for (int seq_num = 502; seq_num < 1001; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 502]);

  sent_nacks_.clear();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(999u, sent_nacks_.size());
  EXPECT_EQ(0xffff, sent_nacks_[0]);
  EXPECT_EQ(0, sent_nacks_[1]);
  for (int seq_num = 3; seq_num < 501; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 1]);
  for (int seq_num = 502; seq_num < 1001; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 2]);

  // Adding packet 1004 will cause the nack list to reach it's max limit.
  // It will then clear all nacks up to the next keyframe (seq num 2),
  // thus removing 0xffff and 0 from the nack list.
  sent_nacks_.clear();
  nack_module_.OnReceivedPacket(1004, false);
  EXPECT_EQ(2u, sent_nacks_.size());
  EXPECT_EQ(1002, sent_nacks_[0]);
  EXPECT_EQ(1003, sent_nacks_[1]);

  sent_nacks_.clear();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(999u, sent_nacks_.size());
  for (int seq_num = 3; seq_num < 501; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 3]);
  for (int seq_num = 502; seq_num < 1001; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 4]);

  // Adding packet 1007 will cause the nack module to overflow again, thus
  // clearing everything up to 501 which is the next keyframe.
  nack_module_.OnReceivedPacket(1007, false);
  sent_nacks_.clear();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(503u, sent_nacks_.size());
  for (int seq_num = 502; seq_num < 1001; ++seq_num)
    EXPECT_EQ(seq_num, sent_nacks_[seq_num - 502]);
  EXPECT_EQ(1005, sent_nacks_[501]);
  EXPECT_EQ(1006, sent_nacks_[502]);
}

TEST_F(TestNackModule, DontBurstOnTimeSkip) {
  nack_module_.Process();
  clock_->AdvanceTimeMilliseconds(20);
  EXPECT_EQ(0, nack_module_.TimeUntilNextProcess());
  nack_module_.Process();

  clock_->AdvanceTimeMilliseconds(100);
  EXPECT_EQ(0, nack_module_.TimeUntilNextProcess());
  nack_module_.Process();
  EXPECT_EQ(20, nack_module_.TimeUntilNextProcess());

  clock_->AdvanceTimeMilliseconds(19);
  EXPECT_EQ(1, nack_module_.TimeUntilNextProcess());
  clock_->AdvanceTimeMilliseconds(2);
  nack_module_.Process();
  EXPECT_EQ(19, nack_module_.TimeUntilNextProcess());

  clock_->AdvanceTimeMilliseconds(19);
  EXPECT_EQ(0, nack_module_.TimeUntilNextProcess());
  nack_module_.Process();

  clock_->AdvanceTimeMilliseconds(21);
  EXPECT_EQ(0, nack_module_.TimeUntilNextProcess());
  nack_module_.Process();
  EXPECT_EQ(19, nack_module_.TimeUntilNextProcess());
}

TEST_F(TestNackModule, ResendNack) {
  nack_module_.OnReceivedPacket(1, false);
  nack_module_.OnReceivedPacket(3, false);
  EXPECT_EQ(1u, sent_nacks_.size());
  EXPECT_EQ(2, sent_nacks_[0]);

  // Default RTT is 100
  clock_->AdvanceTimeMilliseconds(99);
  nack_module_.Process();
  EXPECT_EQ(1u, sent_nacks_.size());

  clock_->AdvanceTimeMilliseconds(1);
  nack_module_.Process();
  EXPECT_EQ(2u, sent_nacks_.size());

  nack_module_.UpdateRtt(50);
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(3u, sent_nacks_.size());

  clock_->AdvanceTimeMilliseconds(50);
  nack_module_.Process();
  EXPECT_EQ(4u, sent_nacks_.size());

  nack_module_.OnReceivedPacket(2, false);
  clock_->AdvanceTimeMilliseconds(50);
  nack_module_.Process();
  EXPECT_EQ(4u, sent_nacks_.size());
}

TEST_F(TestNackModule, ResendPacketMaxRetries) {
  nack_module_.OnReceivedPacket(1, false);
  nack_module_.OnReceivedPacket(3, false);
  EXPECT_EQ(1u, sent_nacks_.size());
  EXPECT_EQ(2, sent_nacks_[0]);

  for (size_t retries = 1; retries < 10; ++retries) {
    clock_->AdvanceTimeMilliseconds(100);
    nack_module_.Process();
    EXPECT_EQ(retries + 1, sent_nacks_.size());
  }

  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(10u, sent_nacks_.size());
}

TEST_F(TestNackModule, TooLargeNackList) {
  nack_module_.OnReceivedPacket(0, false);
  nack_module_.OnReceivedPacket(1001, false);
  EXPECT_EQ(1000u, sent_nacks_.size());
  EXPECT_EQ(0, keyframes_requested_);
  nack_module_.OnReceivedPacket(1003, false);
  EXPECT_EQ(1000u, sent_nacks_.size());
  EXPECT_EQ(1, keyframes_requested_);
  nack_module_.OnReceivedPacket(1004, false);
  EXPECT_EQ(1000u, sent_nacks_.size());
  EXPECT_EQ(1, keyframes_requested_);
}

TEST_F(TestNackModule, TooLargeNackListWithKeyFrame) {
  nack_module_.OnReceivedPacket(0, false);
  nack_module_.OnReceivedPacket(1, true);
  nack_module_.OnReceivedPacket(1001, false);
  EXPECT_EQ(999u, sent_nacks_.size());
  EXPECT_EQ(0, keyframes_requested_);
  nack_module_.OnReceivedPacket(1003, false);
  EXPECT_EQ(1000u, sent_nacks_.size());
  EXPECT_EQ(0, keyframes_requested_);
  nack_module_.OnReceivedPacket(1005, false);
  EXPECT_EQ(1000u, sent_nacks_.size());
  EXPECT_EQ(1, keyframes_requested_);
}

TEST_F(TestNackModule, ClearUpTo) {
  nack_module_.OnReceivedPacket(0, false);
  nack_module_.OnReceivedPacket(100, false);
  EXPECT_EQ(99u, sent_nacks_.size());

  sent_nacks_.clear();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.ClearUpTo(50);
  nack_module_.Process();
  EXPECT_EQ(50u, sent_nacks_.size());
  EXPECT_EQ(50, sent_nacks_[0]);
}

TEST_F(TestNackModule, ClearUpToWrap) {
  nack_module_.OnReceivedPacket(0xfff0, false);
  nack_module_.OnReceivedPacket(0xf, false);
  EXPECT_EQ(30u, sent_nacks_.size());

  sent_nacks_.clear();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.ClearUpTo(0);
  nack_module_.Process();
  EXPECT_EQ(15u, sent_nacks_.size());
  EXPECT_EQ(0, sent_nacks_[0]);
}

TEST_F(TestNackModule, PacketNackCount) {
  EXPECT_EQ(0, nack_module_.OnReceivedPacket(0, false));
  EXPECT_EQ(0, nack_module_.OnReceivedPacket(2, false));
  EXPECT_EQ(1, nack_module_.OnReceivedPacket(1, false));

  sent_nacks_.clear();
  nack_module_.UpdateRtt(100);
  EXPECT_EQ(0, nack_module_.OnReceivedPacket(5, false));
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  clock_->AdvanceTimeMilliseconds(100);
  nack_module_.Process();
  EXPECT_EQ(3, nack_module_.OnReceivedPacket(3, false));
  EXPECT_EQ(3, nack_module_.OnReceivedPacket(4, false));
  EXPECT_EQ(0, nack_module_.OnReceivedPacket(4, false));
}

}  // namespace webrtc
