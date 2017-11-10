/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "system_wrappers/include/rtp_to_ntp_estimator.h"

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {
namespace {
// Number of RTCP SR reports to use to map between RTP and NTP.
const size_t kNumRtcpReportsToUse = 2;

// Calculates the RTP timestamp frequency from two pairs of NTP/RTP timestamps.
bool CalculateFrequency(int64_t ntp_ms1,
                        uint32_t rtp_timestamp1,
                        int64_t ntp_ms2,
                        uint32_t rtp_timestamp2,
                        double* frequency_khz) {
  if (ntp_ms1 <= ntp_ms2)
    return false;

  *frequency_khz = static_cast<double>(rtp_timestamp1 - rtp_timestamp2) /
                   static_cast<double>(ntp_ms1 - ntp_ms2);
  return true;
}

bool Contains(const std::list<RtpToNtpEstimator::RtcpMeasurement>& measurements,
              const RtpToNtpEstimator::RtcpMeasurement& other) {
  for (const auto& measurement : measurements) {
    if (measurement.IsEqual(other))
      return true;
  }
  return false;
}
}  // namespace


RtpToNtpEstimator::RtcpMeasurement::RtcpMeasurement(uint32_t ntp_secs,
                                                    uint32_t ntp_frac,
                                                    int64_t unwrapped_timestamp)
    : ntp_time(ntp_secs, ntp_frac),
      unwrapped_rtp_timestamp(unwrapped_timestamp) {}

bool RtpToNtpEstimator::RtcpMeasurement::IsEqual(
    const RtcpMeasurement& other) const {
  // Use || since two equal timestamps will result in zero frequency and in
  // RtpToNtpMs, |rtp_timestamp_ms| is estimated by dividing by the frequency.
  return (ntp_time == other.ntp_time) ||
         (unwrapped_rtp_timestamp == other.unwrapped_rtp_timestamp);
}

// Class for converting an RTP timestamp to the NTP domain.
RtpToNtpEstimator::RtpToNtpEstimator()
    : consecutive_invalid_samples_(0),
      params_calculated_(false),
      wrap_arounds_(0) {}
RtpToNtpEstimator::~RtpToNtpEstimator() {}

void RtpToNtpEstimator::UpdateParameters() {
  if (measurements_.size() != kNumRtcpReportsToUse)
    return;

  int64_t timestamp_new = measurements_.front().unwrapped_rtp_timestamp;
  int64_t timestamp_old = measurements_.back().unwrapped_rtp_timestamp;

  int64_t ntp_ms_new = measurements_.front().ntp_time.ToMs();
  int64_t ntp_ms_old = measurements_.back().ntp_time.ToMs();

  if (!CalculateFrequency(ntp_ms_new, timestamp_new, ntp_ms_old, timestamp_old,
                          &params_.frequency_khz)) {
    return;
  }
  params_.offset_ms = timestamp_new - params_.frequency_khz * ntp_ms_new;
  params_calculated_ = true;
}

bool RtpToNtpEstimator::UpdateMeasurements(uint32_t ntp_secs,
                                           uint32_t ntp_frac,
                                           uint32_t rtp_timestamp,
                                           bool* new_rtcp_sr) {
  *new_rtcp_sr = false;

  int64_t unwrapped_rtp_timestamp = Unwrap(rtp_timestamp);

  RtcpMeasurement new_measurement(ntp_secs, ntp_frac, unwrapped_rtp_timestamp);

  if (Contains(measurements_, new_measurement)) {
    // RTCP SR report already added.
    return true;
  }
  if (!new_measurement.ntp_time.Valid())
    return false;

  int64_t ntp_ms_new = new_measurement.ntp_time.ToMs();
  bool invalid_sample = false;
  if (!measurements_.empty()) {
    if (ntp_ms_new <= measurements_.back().ntp_time.ToMs()) {
      invalid_sample = true;
    } else if (unwrapped_rtp_timestamp <=
               measurements_.back().unwrapped_rtp_timestamp) {
      LOG(LS_WARNING)
          << "Newer RTCP SR report with older RTP timestamp, dropping";
      invalid_sample = true;
    }
  }

  if (invalid_sample) {
    ++consecutive_invalid_samples_;
    if (consecutive_invalid_samples_ < kMaxInvalidSamples) {
      return false;
    }
    RTC_LOG(LS_WARNING) << "Multiple consecutively invalid RTCP SR reports, "
                           "clearing measurements.";
    measurements_.clear();
    params_calculated_ = false;
  }
  consecutive_invalid_samples_ = 0;

  // Insert new RTCP SR report.
  if (measurements_.size() == kNumRtcpReportsToUse)
    measurements_.pop_back();

  measurements_.push_front(new_measurement);
  *new_rtcp_sr = true;

  // List updated, calculate new parameters.
  UpdateParameters();
  return true;
}

bool RtpToNtpEstimator::Estimate(int64_t rtp_timestamp,
                                 int64_t* rtp_timestamp_ms) const {
  if (!params_calculated_ || measurements_.empty())
    return false;

  int64_t rtp_timestamp_unwrapped = Unwrap(rtp_timestamp);

  // params_calculated_ should not be true unless ms params_.frequency_khz has
  // been calculated to something non zero.
  RTC_DCHECK_NE(params_.frequency_khz, 0.0);
  double rtp_ms =
      (static_cast<double>(rtp_timestamp_unwrapped) - params_.offset_ms) /
          params_.frequency_khz +
      0.5f;

  if (rtp_ms < 0)
    return false;

  *rtp_timestamp_ms = rtp_ms;
  return true;
}

int64_t RtpToNtpEstimator::Unwrap(uint32_t rtp_timestamp) const {
  if (!lastWrapTimestamp_) {
    lastWrapTimestamp_.emplace(rtp_timestamp);
    return rtp_timestamp;
  }
  wrap_arounds_ += CheckForWrapArounds(rtp_timestamp, *lastWrapTimestamp_);
  lastWrapTimestamp_.emplace(rtp_timestamp);
  return rtp_timestamp + (wrap_arounds_ << 32);
}

int CheckForWrapArounds(uint32_t new_timestamp, uint32_t old_timestamp) {
  if (new_timestamp < old_timestamp) {
    // This difference should be less than -2^31 if we have had a wrap around
    // (e.g. |new_timestamp| = 1, |rtcp_rtp_timestamp| = 2^32 - 1). Since it is
    // cast to a int32_t, it should be positive.
    if (static_cast<int32_t>(new_timestamp - old_timestamp) > 0) {
      // Forward wrap around.
      return 1;
    }
  } else if (static_cast<int32_t>(old_timestamp - new_timestamp) > 0) {
    // This difference should be less than -2^31 if we have had a backward wrap
    // around. Since it is cast to a int32_t, it should be positive.
    return -1;
  }
  return 0;
}

}  // namespace webrtc
