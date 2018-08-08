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

#include "absl/memory/memory.h"
#include "modules/congestion_controller/pcc/pcc_network_controller.h"
#include "rtc_base/random.h"

namespace webrtc {
namespace pcc {
namespace {
constexpr int64_t kInitialRttMs = 200;
constexpr int64_t kInitialBandwidthKbps = 300;
constexpr double kMonitorIntervalDurationRatio = 1;
constexpr double kDefaultSamplingStep = 0.05;
constexpr double kTimeoutRatio = 2;
constexpr double kAlphaForRtt = 0.9;
constexpr double kSlowStartModeIncrease = 2;

constexpr double kAlphaForPacketInterval = 0.9;
constexpr int64_t kMinPacketsNumberPerInterval = 10;
const TimeDelta kMinDurationOfMonitorInterval = TimeDelta::ms(50);
constexpr double kMinRateChangeBps = 2000;
const DataRate kMinRateHaveMultiplicativeRateChange =
    DataRate::bps(kMinRateChangeBps / kDefaultSamplingStep);

// Bitrate controller constants.
constexpr double kInitialConversionFactor = 1;
constexpr double kInitialDynamicBoundary = 0.05;
constexpr double kDynamicBoundaryIncrement = 0.1;
// Utility function parameters.
constexpr double kRttGradientCoefficientBps = 900e3;
constexpr double kLossCoefficientBps = 11.35e3;
constexpr double kThroughputCoefficient = 0.5e3;
constexpr double kThroughputPower = 0.9;
constexpr double kRttGradientThreshold = 0.01;
constexpr double kDelayGradientNegativeBound = 0.15;

constexpr int64_t kNumberOfPacketsToKeep = 10;
const uint64_t kRandomSeed = 100;
}  // namespace

PccNetworkController::PccNetworkController(NetworkControllerConfig config)
    : last_sent_packet_time_(Timestamp::ms(0)),
      smoothed_packets_sending_interval_(TimeDelta::Zero()),
      mode_(Mode::kSlowStart),
      default_bandwidth_(DataRate::kbps(kInitialBandwidthKbps)),
      bandwidth_estimate_(default_bandwidth_),
      rtt_tracker_(TimeDelta::ms(kInitialRttMs), kAlphaForRtt),
      monitor_interval_timeout_(TimeDelta::ms(kInitialRttMs) * kTimeoutRatio),
      monitor_interval_length_strategy_(
          MonitorIntervalLengthStrategy::kAdaptive),
      monitor_interval_duration_ratio_(kMonitorIntervalDurationRatio),
      sampling_step_(kDefaultSamplingStep),
      monitor_interval_timeout_ratio_(kTimeoutRatio),
      bitrate_controller_(kInitialConversionFactor,
                          kInitialDynamicBoundary,
                          kDynamicBoundaryIncrement,
                          kRttGradientCoefficient,
                          kLossCoefficient,
                          kThroughputCoefficient,
                          kThroughputPower,
                          kRttGradientThreshold,
                          kDelayGradientNegativeBound),
      monitor_intervals_duration_(TimeDelta::Zero()),
      complete_feedback_mi_number_(0),
      random_generator_(kRandomSeed) {
  if (config.starting_bandwidth.IsFinite()) {
    default_bandwidth_ = config.starting_bandwidth;
    bandwidth_estimate_ = default_bandwidth_;
  }
}

PccNetworkController::~PccNetworkController() {}

NetworkControlUpdate PccNetworkController::CreateRateUpdate(
    Timestamp at_time) const {
  DataRate sending_rate = DataRate::Zero();
  if (monitor_intervals_.empty() ||
      (monitor_intervals_.size() >= monitor_intervals_bitrates_.size() &&
       at_time >= monitor_intervals_.back().GetEndTime())) {
    sending_rate = bandwidth_estimate_;
  } else {
    sending_rate = monitor_intervals_.back().GetTargetSendingRate();
  }
  // Set up config when sending rate is computed.
  NetworkControlUpdate update;

  // Set up target rate to encoder.
  TargetTransferRate target_rate_msg;
  target_rate_msg.network_estimate.at_time = at_time;
  target_rate_msg.network_estimate.round_trip_time = rtt_tracker_.GetRtt();
  target_rate_msg.network_estimate.bandwidth = bandwidth_estimate_;
  // TODO(koloskova): Add correct estimate.
  target_rate_msg.network_estimate.loss_rate_ratio = 0;
  target_rate_msg.network_estimate.bwe_period =
      2 * monitor_interval_duration_ratio_ * rtt_tracker_.GetRtt();

  target_rate_msg.target_rate = sending_rate;
  update.target_rate = target_rate_msg;

  // Set up pacing/padding target rate.
  PacerConfig pacer_config;
  pacer_config.at_time = at_time;
  pacer_config.time_window = TimeDelta::ms(1);
  pacer_config.data_window = sending_rate * pacer_config.time_window;
  pacer_config.pad_window = sending_rate * pacer_config.time_window;

  update.pacer_config = pacer_config;
  return update;
}

NetworkControlUpdate PccNetworkController::OnSentPacket(SentPacket msg) {
  // Start new monitor interval if previous has finished.
  // Monitor interval is initialized in OnProcessInterval function.
  if (last_sent_packet_time_ != Timestamp::ms(0)) {
    smoothed_packets_sending_interval_ =
        (msg.send_time - last_sent_packet_time_) * kAlphaForPacketInterval +
        (1 - kAlphaForPacketInterval) * smoothed_packets_sending_interval_;
  }
  last_sent_packet_time_ = msg.send_time;
  if (!monitor_intervals_.empty() &&
      msg.send_time >= monitor_intervals_.back().GetEndTime() &&
      monitor_intervals_bitrates_.size() > monitor_intervals_.size()) {
    monitor_intervals_.emplace_back(
        monitor_intervals_bitrates_[monitor_intervals_.size()], msg.send_time,
        monitor_intervals_duration_);
  }
  if (IsTimeoutExpired(msg.send_time)) {
    DataSize received_size = DataSize::Zero();
    for (size_t i = 1; i < last_received_packets_.size(); ++i) {
      received_size += last_received_packets_[i].sent_packet->size;
    }
    TimeDelta sending_time = TimeDelta::Zero();
    if (last_received_packets_.size() > 0)
      sending_time = last_received_packets_.back().receive_time -
                     last_received_packets_.front().receive_time;
    DataRate reciving_rate = bandwidth_estimate_;
    if (sending_time > TimeDelta::Zero())
      reciving_rate = received_size / sending_time;
    bandwidth_estimate_ =
        std::min<DataRate>(bandwidth_estimate_ * 0.5, reciving_rate);
  }
  if (IsFeedbackCollectionDone() || IsTimeoutExpired(msg.send_time)) {
    // Creating new monitor intervals.
    monitor_intervals_.clear();
    monitor_interval_timeout_ =
        rtt_tracker_.GetRtt() * monitor_interval_timeout_ratio_;
    // Compute duration of monitor intervals.
    if (monitor_interval_length_strategy_ ==
        MonitorIntervalLengthStrategy::kAdaptive) {
      monitor_intervals_duration_ = std::max(
          rtt_tracker_.GetRtt() * monitor_interval_duration_ratio_,
          smoothed_packets_sending_interval_ * kMinPacketsNumberPerInterval);
    } else {
      RTC_DCHECK(monitor_interval_length_strategy_ ==
                 MonitorIntervalLengthStrategy::kFixed);
      monitor_intervals_duration_ =
          smoothed_packets_sending_interval_ * kMinPacketsNumberPerInterval;
    }
    monitor_intervals_duration_ =
        std::max(kMinDurationOfMonitorInterval, monitor_intervals_duration_);
    complete_feedback_mi_number_ = 0;
    // Compute bitrates and start first monitor interval.
    if (mode_ == Mode::kSlowStart) {
      monitor_intervals_bitrates_ = {kSlowStartModeIncrease *
                                     bandwidth_estimate_};
      monitor_intervals_.emplace_back(
          kSlowStartModeIncrease * bandwidth_estimate_, msg.send_time,
          monitor_intervals_duration_);
    } else {
      RTC_DCHECK(mode_ == Mode::kOnlineLearning || mode_ == Mode::kDoubleCheck);
      monitor_intervals_.clear();
      int64_t sign = 2 * (random_generator_.Rand(0, 1) % 2) - 1;
      RTC_DCHECK_GE(sign, -1);
      RTC_DCHECK_LE(sign, 1);
      if (bandwidth_estimate_ >= kMinRateHaveMultiplicativeRateChange) {
        monitor_intervals_bitrates_ = {
            bandwidth_estimate_ * (1 + sign * sampling_step_),
            bandwidth_estimate_ * (1 - sign * sampling_step_)};
      } else {
        monitor_intervals_bitrates_ = {
            DataRate::bps(std::max<double>(
                bandwidth_estimate_.bps() + sign * kMinRateChangeBps, 0)),
            DataRate::bps(std::max<double>(
                bandwidth_estimate_.bps() - sign * kMinRateChangeBps, 0))};
      }
      monitor_intervals_.emplace_back(monitor_intervals_bitrates_[0],
                                      msg.send_time,
                                      monitor_intervals_duration_);
    }
  }
  return CreateRateUpdate(msg.send_time);
}

bool PccNetworkController::IsTimeoutExpired(Timestamp current_time) const {
  if (complete_feedback_mi_number_ >= monitor_intervals_.size()) {
    return false;
  }
  return current_time -
             monitor_intervals_[complete_feedback_mi_number_].GetEndTime() >=
         monitor_interval_timeout_;
}

bool PccNetworkController::IsFeedbackCollectionDone() const {
  return complete_feedback_mi_number_ >= monitor_intervals_bitrates_.size();
}

NetworkControlUpdate PccNetworkController::OnTransportPacketsFeedback(
    TransportPacketsFeedback msg) {
  // Save packets to last_received_packets_ array.
  for (const PacketResult& packet_result : msg.ReceivedWithSendInfo()) {
    last_received_packets_.push_back(packet_result);
  }
  while (last_received_packets_.size() > kNumberOfPacketsToKeep) {
    last_received_packets_.pop_front();
  }
  rtt_tracker_.OnPacketsFeedback(msg.PacketsWithFeedback(), msg.feedback_time);
  // Skip rate update in case when online learning mode just started, but
  // corresponding monitor intervals were not started yet.
  if (mode_ == Mode::kOnlineLearning &&
      monitor_intervals_bitrates_.size() < 2) {
    return NetworkControlUpdate();
  }
  if (!IsFeedbackCollectionDone() && !monitor_intervals_.empty()) {
    while (complete_feedback_mi_number_ < monitor_intervals_.size()) {
      monitor_intervals_[complete_feedback_mi_number_].OnPacketsFeedback(
          msg.PacketsWithFeedback());
      if (!monitor_intervals_[complete_feedback_mi_number_]
               .IsFeedbackCollectionDone())
        break;
      ++complete_feedback_mi_number_;
    }
  }
  if (IsFeedbackCollectionDone() && !NeedDoubleCheckMeasurments()) {
    UpdateSendingRate();
  }
  return NetworkControlUpdate();
}

bool PccNetworkController::NeedDoubleCheckMeasurments() {
  if (IsFeedbackCollectionDone() || mode_ == Mode::kSlowStart) {
    return false;
  }
  if (mode_ == Mode::kDoubleCheck) {
    mode_ = Mode::kSlowStart;
    return false;
  }
  double first_loss_rate = monitor_intervals_[0].GetLossRate();
  double second_loss_rate = monitor_intervals_[1].GetLossRate();
  DataRate first_bitrate = monitor_intervals_[0].GetTargetSendingRate();
  DataRate second_bitrate = monitor_intervals_[1].GetTargetSendingRate();
  if ((first_bitrate.bps() - second_bitrate.bps()) *
          (first_loss_rate - second_loss_rate) <
      0) {
    mode_ = Mode::kDoubleCheck;
    return true;
  }
  return false;
}

void PccNetworkController::UpdateSendingRate() {
  if (monitor_intervals_.empty() || !IsFeedbackCollectionDone()) {
    return;
  }
  if (mode_ == Mode::kSlowStart) {
    DataRate old_bandwidth_estimate = bandwidth_estimate_;
    bandwidth_estimate_ = bitrate_controller_.ComputeRateUpdateForSlowStartMode(
        monitor_intervals_[0], bandwidth_estimate_);
    if (bandwidth_estimate_ <= old_bandwidth_estimate)
      mode_ = Mode::kOnlineLearning;
  } else {
    RTC_DCHECK(mode_ == Mode::kOnlineLearning);
    bandwidth_estimate_ =
        bitrate_controller_.ComputeRateUpdateForOnlineLearningMode(
            monitor_intervals_, bandwidth_estimate_);
  }
}

NetworkControlUpdate PccNetworkController::OnNetworkAvailability(
    NetworkAvailability msg) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnNetworkRouteChange(
    NetworkRouteChange msg) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnProcessInterval(
    ProcessInterval msg) {
  monitor_intervals_duration_ =
      rtt_tracker_.GetRtt() * monitor_interval_duration_ratio_;
  monitor_intervals_bitrates_ = {bandwidth_estimate_};
  monitor_intervals_.emplace_back(bandwidth_estimate_, msg.at_time,
                                  monitor_intervals_duration_);
  complete_feedback_mi_number_ = 0;
  return CreateRateUpdate(msg.at_time);
}

NetworkControlUpdate PccNetworkController::OnTargetRateConstraints(
    TargetRateConstraints msg) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnRemoteBitrateReport(
    RemoteBitrateReport) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnRoundTripTimeUpdate(
    RoundTripTimeUpdate) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnTransportLossReport(
    TransportLossReport) {
  return NetworkControlUpdate();
}

NetworkControlUpdate PccNetworkController::OnStreamsConfig(StreamsConfig) {
  return NetworkControlUpdate();
}

}  // namespace pcc
}  // namespace webrtc
