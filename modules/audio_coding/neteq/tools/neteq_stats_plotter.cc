/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/neteq/tools/neteq_stats_plotter.h"

#include <inttypes.h>
#include <stdio.h>

namespace webrtc {
namespace test {

NetEqStatsPlotter::NetEqStatsPlotter(NetEqStatsGetter* stats_getter,
                                     NetEqDelayAnalyzer* delay_analyzer,
                                     bool make_matlab_plot,
                                     bool make_python_plot,
                                     bool show_concealment_events,
                                     std::string base_file_name)
    : stats_getter_(stats_getter),
      delay_analyzer_(delay_analyzer),
      make_matlab_plot_(make_matlab_plot),
      make_python_plot_(make_python_plot),
      show_concealment_events_(show_concealment_events),
      base_file_name_(base_file_name) {}

void NetEqStatsPlotter::SimulationEnded(int64_t simulation_time_ms) {
  if (make_matlab_plot_) {
    auto matlab_script_name = base_file_name_;
    std::replace(matlab_script_name.begin(), matlab_script_name.end(), '.',
                 '_');
    printf("Creating Matlab plot script %s.m\n", matlab_script_name.c_str());
    delay_analyzer_->CreateMatlabScript(matlab_script_name + ".m");
  }
  if (make_python_plot_) {
    auto python_script_name = base_file_name_;
    std::replace(python_script_name.begin(), python_script_name.end(), '.',
                 '_');
    printf("Creating Python plot script %s.py\n", python_script_name.c_str());
    delay_analyzer_->CreatePythonScript(python_script_name + ".py");
  }

  printf("Simulation statistics:\n");
  printf("  output duration: %" PRId64 " ms\n", simulation_time_ms);
  auto stats = stats_getter_->AverageStats();
  printf("  packet_loss_rate: %f %%\n", 100.0 * stats.packet_loss_rate);
  printf("  expand_rate: %f %%\n", 100.0 * stats.expand_rate);
  printf("  speech_expand_rate: %f %%\n", 100.0 * stats.speech_expand_rate);
  printf("  preemptive_rate: %f %%\n", 100.0 * stats.preemptive_rate);
  printf("  accelerate_rate: %f %%\n", 100.0 * stats.accelerate_rate);
  printf("  secondary_decoded_rate: %f %%\n",
         100.0 * stats.secondary_decoded_rate);
  printf("  secondary_discarded_rate: %f %%\n",
         100.0 * stats.secondary_discarded_rate);
  printf("  clockdrift_ppm: %f ppm\n", stats.clockdrift_ppm);
  printf("  mean_waiting_time_ms: %f ms\n", stats.mean_waiting_time_ms);
  printf("  median_waiting_time_ms: %f ms\n", stats.median_waiting_time_ms);
  printf("  min_waiting_time_ms: %f ms\n", stats.min_waiting_time_ms);
  printf("  max_waiting_time_ms: %f ms\n", stats.max_waiting_time_ms);
  printf("  current_buffer_size_ms: %f ms\n", stats.current_buffer_size_ms);
  printf("  preferred_buffer_size_ms: %f ms\n", stats.preferred_buffer_size_ms);
  if (show_concealment_events_) {
    printf(" concealment_events_ms:\n");
    for (auto concealment_event : stats_getter_->concealment_events())
      printf("%s\n", concealment_event.ToString().c_str());
    printf(" end of concealment_events_ms\n");
  }
}

}  // namespace test
}  // namespace webrtc
