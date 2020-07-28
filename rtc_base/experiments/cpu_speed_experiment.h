/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_EXPERIMENTS_CPU_SPEED_EXPERIMENT_H_
#define RTC_BASE_EXPERIMENTS_CPU_SPEED_EXPERIMENT_H_

#include <vector>

#include "absl/types/optional.h"

namespace webrtc {

class CpuSpeedExperiment {
 public:
  CpuSpeedExperiment();
  ~CpuSpeedExperiment();

  // Example:
  // WebRTC-VP8-CpuSpeed-Arm/pixels:100|200|300,cpu_speed:-1|-2|-3/
  // pixels <= 100 -> cpu speed: -1
  // pixels <= 200 -> cpu speed: -2
  // pixels <= 300 -> cpu speed: -3

  struct Config {
    int pixels = 0;     // The video frame size.
    int cpu_speed = 0;  // The |cpu_speed| to be used if the frame size is less
                        // than or equal to |pixels|.
  };

  // Gets the cpu speed based on |pixels|.
  absl::optional<int> GetValue(int pixels) const;

 private:
  std::vector<Config> configs_;
};

}  // namespace webrtc

#endif  // RTC_BASE_EXPERIMENTS_CPU_SPEED_EXPERIMENT_H_
