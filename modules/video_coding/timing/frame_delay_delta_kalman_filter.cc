/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/timing/frame_delay_delta_kalman_filter.h"

#include "api/units/data_size.h"
#include "api/units/time_delta.h"

namespace webrtc {

namespace {
constexpr double kThetaLow = 0.000001;
}

FrameDelayDeltaKalmanFilter::FrameDelayDeltaKalmanFilter() {
  theta_[0] = 1 / (512e3 / 8);
  theta_[1] = 0;

  theta_cov_[0][0] = 1e-4;
  theta_cov_[1][1] = 1e2;
  theta_cov_[0][1] = theta_cov_[1][0] = 0;
  q_cov_[0][0] = 2.5e-10;
  q_cov_[1][1] = 1e-10;
  q_cov_[0][1] = q_cov_[1][0] = 0;
}

void FrameDelayDeltaKalmanFilter::PredictAndUpdate(
    TimeDelta frame_delay_delta,
    double frame_size_delta_bytes,
    DataSize max_frame_size,
    double var_noise) {
  double Mh[2];
  double hMh_sigma;
  double kalmanGain[2];
  double measureRes;
  double t00, t01;

  // Kalman filtering

  // Prediction
  // M = M + Q
  theta_cov_[0][0] += q_cov_[0][0];
  theta_cov_[0][1] += q_cov_[0][1];
  theta_cov_[1][0] += q_cov_[1][0];
  theta_cov_[1][1] += q_cov_[1][1];

  // Kalman gain
  // K = M*h'/(sigma2n + h*M*h') = M*h'/(1 + h*M*h')
  // h = [dFS 1]
  // Mh = M*h'
  // hMh_sigma = h*M*h' + R
  Mh[0] = theta_cov_[0][0] * frame_size_delta_bytes + theta_cov_[0][1];
  Mh[1] = theta_cov_[1][0] * frame_size_delta_bytes + theta_cov_[1][1];
  // sigma weights measurements with a small deltaFS as noisy and
  // measurements with large deltaFS as good
  if (max_frame_size < DataSize::Bytes(1)) {
    return;
  }
  double sigma = (300.0 * exp(-fabs(frame_size_delta_bytes) /
                              (1e0 * max_frame_size.bytes())) +
                  1) *
                 sqrt(var_noise);
  if (sigma < 1.0) {
    sigma = 1.0;
  }
  hMh_sigma = frame_size_delta_bytes * Mh[0] + Mh[1] + sigma;
  if ((hMh_sigma < 1e-9 && hMh_sigma >= 0) ||
      (hMh_sigma > -1e-9 && hMh_sigma <= 0)) {
    RTC_DCHECK_NOTREACHED();
    return;
  }
  kalmanGain[0] = Mh[0] / hMh_sigma;
  kalmanGain[1] = Mh[1] / hMh_sigma;

  // Correction
  // theta = theta + K*(dT - h*theta)
  measureRes =
      frame_delay_delta.ms() - (frame_size_delta_bytes * theta_[0] + theta_[1]);
  theta_[0] += kalmanGain[0] * measureRes;
  theta_[1] += kalmanGain[1] * measureRes;

  if (theta_[0] < kThetaLow) {
    theta_[0] = kThetaLow;
  }

  // M = (I - K*h)*M
  t00 = theta_cov_[0][0];
  t01 = theta_cov_[0][1];
  theta_cov_[0][0] = (1 - kalmanGain[0] * frame_size_delta_bytes) * t00 -
                     kalmanGain[0] * theta_cov_[1][0];
  theta_cov_[0][1] = (1 - kalmanGain[0] * frame_size_delta_bytes) * t01 -
                     kalmanGain[0] * theta_cov_[1][1];
  theta_cov_[1][0] = theta_cov_[1][0] * (1 - kalmanGain[1]) -
                     kalmanGain[1] * frame_size_delta_bytes * t00;
  theta_cov_[1][1] = theta_cov_[1][1] * (1 - kalmanGain[1]) -
                     kalmanGain[1] * frame_size_delta_bytes * t01;

  // Covariance matrix, must be positive semi-definite.
  RTC_DCHECK(theta_cov_[0][0] + theta_cov_[1][1] >= 0 &&
             theta_cov_[0][0] * theta_cov_[1][1] -
                     theta_cov_[0][1] * theta_cov_[1][0] >=
                 0 &&
             theta_cov_[0][0] >= 0);
}

double FrameDelayDeltaKalmanFilter::GetFrameDelayDeltaEstimateSizeBased(
    double frame_size_delta_bytes) const {
  // Unit: [1 / bytes per millisecond] * [bytes] = [milliseconds].
  return theta_[0] * frame_size_delta_bytes;
}

double FrameDelayDeltaKalmanFilter::GetFrameDelayDeltaEstimateTotal(
    double frame_size_delta_bytes) const {
  double frame_transmission_delay_ms =
      GetFrameDelayDeltaEstimateSizeBased(frame_size_delta_bytes);
  double link_propagation_delay_ms = theta_[1];
  return frame_transmission_delay_ms + link_propagation_delay_ms;
}

}  // namespace webrtc
