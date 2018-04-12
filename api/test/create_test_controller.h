/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef API_TEST_CREATE_TEST_CONTROLLER_H_
#define API_TEST_CREATE_TEST_CONTROLLER_H_

#include <memory>

#include "api/test/test_controller.h"

namespace webrtc {

std::unique_ptr<TestControllerInterface> CreateTestController();

}

#endif  // API_TEST_CREATE_TEST_CONTROLLER_H_
