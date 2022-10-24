/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AGC2_INPUT_VOLUME_STATS_REPORTER_H_
#define MODULES_AUDIO_PROCESSING_AGC2_INPUT_VOLUME_STATS_REPORTER_H_

#include "absl/types/optional.h"
#include "rtc_base/gtest_prod_util.h"

namespace webrtc {

// Input volume statistics calculator. Computes aggregate stats based on the
// framewise input volume observed by `UpdateStatistics()`. Periodically logs
// the statistics into a histogram.
class InputVolumeStatsReporter {
 public:
  InputVolumeStatsReporter();
  InputVolumeStatsReporter(const InputVolumeStatsReporter&) = delete;
  InputVolumeStatsReporter operator=(const InputVolumeStatsReporter&) = delete;
  ~InputVolumeStatsReporter();

  // Updates the stats based on `input_volume`. Periodically logs the stats into
  // a histogram.
  void UpdateStatistics(int input_volume);

 private:
  FRIEND_TEST_ALL_PREFIXES(InputVolumeStatsReporterTest,
                           CheckVolumeUpdateStatsForEmptyStats);
  FRIEND_TEST_ALL_PREFIXES(InputVolumeStatsReporterTest,
                           CheckVolumeUpdateStatsAfterNoVolumeChange);
  FRIEND_TEST_ALL_PREFIXES(InputVolumeStatsReporterTest,
                           CheckVolumeUpdateStatsAfterVolumeIncrease);
  FRIEND_TEST_ALL_PREFIXES(InputVolumeStatsReporterTest,
                           CheckVolumeUpdateStatsAfterVolumeDecrease);
  FRIEND_TEST_ALL_PREFIXES(InputVolumeStatsReporterTest,
                           CheckVolumeUpdateStatsAfterReset);

  // Stores input volume update stats to enable calculation of update rate and
  // average update separately for volume increases and decreases.
  struct VolumeUpdateStats {
    int num_decreases = 0;
    int num_increases = 0;
    int sum_decreases = 0;
    int sum_increases = 0;
  } volume_update_stats_;

  // Returns a copy of the stored statistics. Use only for testing.
  VolumeUpdateStats volume_update_stats() const { return volume_update_stats_; }

  // Computes aggregate stat and logs them into a histogram.
  void LogVolumeUpdateStats() const;

  int log_volume_update_stats_counter_ = 0;
  absl::optional<int> previous_input_volume_ = absl::nullopt;
};
}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AGC2_INPUT_VOLUME_STATS_REPORTER_H_
