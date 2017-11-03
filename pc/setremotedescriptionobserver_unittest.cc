/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/setremotedescriptionobserver.h"

#include <string>

#include "api/failurereason.h"
#include "api/jsep.h"
#include "api/optional.h"
#include "pc/test/mockpeerconnectionobservers.h"
#include "rtc_base/checks.h"
#include "rtc_base/gunit.h"
#include "rtc_base/scoped_ref_ptr.h"
#include "rtc_base/thread.h"

const int kDefaultTimeoutMs = 1000;

class SetRemoteDescriptionSessionObserverWrapperTest : public testing::Test {
 public:
  SetRemoteDescriptionSessionObserverWrapperTest()
      : set_desc_observer_(new rtc::RefCountedObject<
                           webrtc::MockSetSessionDescriptionObserver>()),
        observer_(new webrtc::SetRemoteDescriptionSessionObserverWrapper(
            set_desc_observer_)) {}

 protected:
  rtc::scoped_refptr<webrtc::MockSetSessionDescriptionObserver>
      set_desc_observer_;
  rtc::scoped_refptr<webrtc::SetRemoteDescriptionSessionObserverWrapper>
      observer_;
};

TEST_F(SetRemoteDescriptionSessionObserverWrapperTest, OnSuccess) {
  webrtc::SetRemoteDescriptionObserver::StateChanges state_changes;
  observer_->OnSuccess(std::move(state_changes));
  EXPECT_TRUE_WAIT(set_desc_observer_->called(), kDefaultTimeoutMs);
  EXPECT_TRUE(set_desc_observer_->result());
}

TEST_F(SetRemoteDescriptionSessionObserverWrapperTest, OnFailure) {
  observer_->OnFailure(webrtc::FailureReason("FailureMessage"));
  EXPECT_TRUE_WAIT(set_desc_observer_->called(), kDefaultTimeoutMs);
  EXPECT_FALSE(set_desc_observer_->result());
  EXPECT_EQ(set_desc_observer_->error(), "FailureMessage");
}

TEST_F(SetRemoteDescriptionSessionObserverWrapperTest, IsAsynchronous) {
  webrtc::SetRemoteDescriptionObserver::StateChanges state_changes;
  observer_->OnSuccess(std::move(state_changes));
  // Untill this thread's messages are processed by EXPECT_TRUE_WAIT,
  // |set_desc_observer_| should not have been called.
  EXPECT_FALSE(set_desc_observer_->called());
  EXPECT_TRUE_WAIT(set_desc_observer_->called(), kDefaultTimeoutMs);
  EXPECT_TRUE(set_desc_observer_->result());
}

TEST_F(SetRemoteDescriptionSessionObserverWrapperTest, SurvivesDereferencing) {
  webrtc::SetRemoteDescriptionObserver::StateChanges state_changes;
  observer_->OnSuccess(std::move(state_changes));
  // Even if there are no external references to |observer_| the operation
  // should complete.
  observer_ = nullptr;
  EXPECT_TRUE_WAIT(set_desc_observer_->called(), kDefaultTimeoutMs);
  EXPECT_TRUE(set_desc_observer_->result());
}
