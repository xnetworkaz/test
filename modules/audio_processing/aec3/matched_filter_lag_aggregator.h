/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AEC3_MATCHED_FILTER_LAG_AGGREGATOR_H_
#define MODULES_AUDIO_PROCESSING_AEC3_MATCHED_FILTER_LAG_AGGREGATOR_H_

#include <vector>

#include "api/optional.h"
#include "modules/audio_processing/aec3/matched_filter.h"
#include "rtc_base/constructormagic.h"

namespace webrtc {

class ApmDataDumper;

// Aggregates lag estimates produced by the MatchedFilter class into a single
// reliable combined lag estimate.
class MatchedFilterLagAggregator {
 public:
  MatchedFilterLagAggregator(ApmDataDumper* data_dumper,
                             size_t num_lag_estimates);
  ~MatchedFilterLagAggregator();

  // Resets the aggregator.
  void Reset();

  // Aggregates the provided lag estimates.
  rtc::Optional<size_t> Aggregate(
      rtc::ArrayView<const MatchedFilter::LagEstimate> lag_estimates);

 private:
  ApmDataDumper* const data_dumper_;
  std::vector<size_t> lag_updates_in_a_row_;
  size_t candidate_ = 0;
  size_t candidate_counter_ = 0;

  std::array<int, 1664> histogram_;
  std::array<int, 250> histogram_data_;
  int histogram_data_index_ = 0;
  bool filled_histogram_ = false;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(MatchedFilterLagAggregator);
};
}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AEC3_MATCHED_FILTER_LAG_AGGREGATOR_H_
