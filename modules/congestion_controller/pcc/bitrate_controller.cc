/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "modules/congestion_controller/pcc/bitrate_controller.h"
#include "rtc_base/ptr_util.h"

namespace webrtc {
namespace pcc {

PccBitrateController::PccBitrateController(double initial_conversion_factor,
                                           double initial_dynamic_boundary,
                                           double dynamic_boundary_increment,
                                           double rtt_gradient_coefficient,
                                           double loss_coefficient,
                                           double throughput_coefficient,
                                           double throughput_power,
                                           double rtt_gradient_threshold)
    : consecutive_rate_adjustments_number_(0),
      initial_dynamic_boundary_(initial_dynamic_boundary),
      dynamic_boundary_increment_(dynamic_boundary_increment),
      utility_function_(
          rtc::MakeUnique<VivaceUtilityFunction>(rtt_gradient_coefficient,
                                                 loss_coefficient,
                                                 throughput_coefficient,
                                                 throughput_power,
                                                 rtt_gradient_threshold)),
      step_size_adjustments_number_(0),
      initial_conversion_factor_(initial_conversion_factor) {}

PccBitrateController::PccBitrateController(
    double initial_conversion_factor,
    double initial_dynamic_boundary,
    double dynamic_boundary_increment,
    std::unique_ptr<PccUtilityFunctionInterface> utility_function)
    : consecutive_rate_adjustments_number_(0),
      initial_dynamic_boundary_(initial_dynamic_boundary),
      dynamic_boundary_increment_(dynamic_boundary_increment),
      utility_function_(std::move(utility_function)),
      step_size_adjustments_number_(0),
      initial_conversion_factor_(initial_conversion_factor) {}

PccBitrateController::~PccBitrateController() = default;

double PccBitrateController::ComputeStepSize(double utility_gradient) {
  // Computes number of consecutive rate adjustments.
  if (utility_gradient > 0) {
    step_size_adjustments_number_ =
        std::max<int64_t>(step_size_adjustments_number_ + 1, 1);
  } else if (utility_gradient < 0) {
    step_size_adjustments_number_ =
        std::min<int64_t>(step_size_adjustments_number_ - 1, -1);
  } else {
    step_size_adjustments_number_ = 0;
  }
  // Computes step size amplifier
  int64_t step_size_amplifier = 1;
  if (std::abs(step_size_adjustments_number_) <= 3) {
    step_size_amplifier =
        std::max<int64_t>(std::abs(step_size_adjustments_number_), 1);
  } else {
    step_size_amplifier = 2 * std::abs(step_size_adjustments_number_) - 3;
  }
  return step_size_amplifier * initial_conversion_factor_;
}

double PccBitrateController::ApplyDynamicBoundary(double rate_change,
                                                  double bitrate) {
  double rate_change_abs = std::abs(rate_change);
  int64_t rate_change_sign = (rate_change > 0) ? 1 : -1;
  if (consecutive_rate_adjustments_number_ * rate_change_sign < 0) {
    consecutive_rate_adjustments_number_ = 0;
  }
  double dynamic_change_boundary =
      initial_dynamic_boundary_ +
      std::abs(consecutive_rate_adjustments_number_) *
          dynamic_boundary_increment_;
  double boundary = bitrate * dynamic_change_boundary;
  if (rate_change_abs > boundary) {
    consecutive_rate_adjustments_number_ += rate_change_sign;
    return boundary * rate_change_sign;
  }
  while (rate_change_abs <= boundary &&
         consecutive_rate_adjustments_number_ * rate_change_sign > 0) {
    consecutive_rate_adjustments_number_ -= rate_change_sign;
    dynamic_change_boundary = initial_dynamic_boundary_ +
                              std::abs(consecutive_rate_adjustments_number_) *
                                  dynamic_boundary_increment_;
    boundary = bitrate * dynamic_change_boundary;
  }
  consecutive_rate_adjustments_number_ += rate_change_sign;
  return rate_change;
}

DataRate PccBitrateController::ComputeRateUpdate(
    const std::vector<MonitorInterval>& block,
    DataRate bandwith_estimate) {
  if (block.size() == 1) {  // slow start mode
    double function_value = utility_function_->ComputeUtilityFunction(block[0]);
    if (!previous_function_value.has_value()) {
      previous_function_value = function_value;
      return bandwith_estimate * 2;
    }
    if (function_value > previous_function_value) {
      previous_function_value = function_value;
      return bandwith_estimate * 2;
    }
    return bandwith_estimate;
  }
  // online optimization mode
  double first_value = utility_function_->ComputeUtilityFunction(block[0]);
  double second_value = utility_function_->ComputeUtilityFunction(block[1]);
  double first_bitrate_kbps = block[0].GetTargetSendingRate().kbps();
  double second_bitrate_kbps = block[1].GetTargetSendingRate().kbps();
  double gradient =
      (first_value - second_value) / (first_bitrate_kbps - second_bitrate_kbps);
  double rate_change_kbps = gradient * ComputeStepSize(gradient);  // delta_r
  rate_change_kbps =
      ApplyDynamicBoundary(rate_change_kbps, bandwith_estimate.kbps());
  return DataRate::kbps(
      std::max<double>(0, bandwith_estimate.kbps() + rate_change_kbps));
}

}  // namespace pcc
}  // namespace webrtc
