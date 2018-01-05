/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXPERIMENTS_ALR_EXPERIMENT_H_
#define EXPERIMENTS_ALR_EXPERIMENT_H_

#include "api/optional.h"
#include "common_types.h"  // NOLINT(build/include)
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "rtc_base/criticalsection.h"
#include "rtc_base/rate_statistics.h"
#include "rtc_base/thread_annotations.h"
#include "typedefs.h"  // NOLINT(build/include)

namespace webrtc {
struct AlrExperimentSettings {
 public:
  float pacing_factor;
  int64_t max_paced_queue_time;
  int alr_bandwidth_usage_percent;
  int alr_start_budget_level_percent;
  int alr_stop_budget_level_percent;
  // Will be sent to the receive side for stats slicing.
  // Can be 0..6, because it's sent as a 3 bits value and there's also
  // reserved value to indicate absence of experiment.
  int group_id;

  static const char kScreenshareProbingBweExperimentName[];
  static const char kStrictPacingAndProbingExperimentName[];
  static rtc::Optional<AlrExperimentSettings> CreateFromFieldTrial(
      const char* experiment_name);
  static bool MaxOneFieldTrialEnabled();

 private:
  AlrExperimentSettings() = default;
};
}  // namespace webrtc

#endif  // EXPERIMENTS_ALR_EXPERIMENT_H_
