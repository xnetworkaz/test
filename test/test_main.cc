/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "test/test_main_lib.h"

int main(int argc, char* argv[]) {
  std::unique_ptr<webrtc::TestMain> main = webrtc::TestMain::Create();
  main->Init(argc, argv);
  main->Run(argc, argv);
}
