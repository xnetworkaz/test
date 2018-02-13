/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/interpolated_gain_curve.h"

#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace {

constexpr bool kComputeStats = true;

}  // namespace

const std::array<float, kInterpolatedGainCurveTotalPoints>
    InterpolatedGainCurve::approximation_params_x_ = {
        {30057.296875,    30148.986328125, 30240.67578125,  30424.052734375,
         30607.4296875,   30790.806640625, 30974.18359375,  31157.560546875,
         31340.939453125, 31524.31640625,  31707.693359375, 31891.0703125,
         32074.447265625, 32257.82421875,  32441.201171875, 32624.580078125,
         32807.95703125,  32991.33203125,  33174.7109375,   33358.08984375,
         33541.46484375,  33724.84375,     33819.53515625,  34009.5390625,
         34200.05859375,  34389.81640625,  34674.48828125,  35054.375,
         35434.86328125,  35814.81640625,  36195.16796875,  36575.03125}};

const std::array<float, kInterpolatedGainCurveTotalPoints>
    InterpolatedGainCurve::approximation_params_m_ = {
        {-3.515235675877192989e-07, -1.050251626111275982e-06,
         -2.085213736791047268e-06, -3.443004743530764244e-06,
         -4.773849468620028347e-06, -6.077375928725814447e-06,
         -7.353257842623861507e-06, -8.601219633419532329e-06,
         -9.821013009059242904e-06, -1.101243378798244521e-05,
         -1.217532644659513608e-05, -1.330956911260727793e-05,
         -1.441507538402220234e-05, -1.549179251014720649e-05,
         -1.653970684856176376e-05, -1.755882840370759368e-05,
         -1.854918446042574942e-05, -1.951086778717581183e-05,
         -2.044398024736437947e-05, -2.1348627342376858e-05,
         -2.222496914328075945e-05, -2.265374678245279938e-05,
         -2.242570917587727308e-05, -2.220122041762806475e-05,
         -2.19802095671184361e-05,  -2.176260204578284174e-05,
         -2.133731686626560986e-05, -2.092481918225530535e-05,
         -2.052459603874012828e-05, -2.013615448959171772e-05,
         -1.975903069251216948e-05, -1.939277899509761482e-05}};

const std::array<float, kInterpolatedGainCurveTotalPoints>
    InterpolatedGainCurve::approximation_params_q_ = {
        {1.010565876960754395, 1.031631827354431152, 1.062929749488830566,
         1.104239225387573242, 1.144973039627075195, 1.185109615325927734,
         1.224629044532775879, 1.263512492179870605, 1.301741957664489746,
         1.339300632476806641, 1.376173257827758789, 1.412345528602600098,
         1.447803974151611328, 1.482536554336547852, 1.516532182693481445,
         1.549780607223510742, 1.582272171974182129, 1.613999366760253906,
         1.644955039024353027, 1.675132393836975098, 1.704526185989379883,
         1.718986630439758301, 1.711274504661560059, 1.703639745712280273,
         1.696081161499023438, 1.688597679138183594, 1.673851132392883301,
         1.659391283988952637, 1.645209431648254395, 1.631297469139099121,
         1.617647409439086914, 1.604251742362976074}};

InterpolatedGainCurve::InterpolatedGainCurve(ApmDataDumper* apm_data_dumper)
    : apm_data_dumper_(apm_data_dumper) {}

InterpolatedGainCurve::~InterpolatedGainCurve() {
  if (stats_.available) {
    // TODO(alessiob): We might want to add these stats as RTC metrics.
    RTC_DCHECK(apm_data_dumper_);
    apm_data_dumper_->DumpRaw("agc2_interp_gain_curve_lookups_identity",
                              stats_.look_ups_identity_region);
    apm_data_dumper_->DumpRaw("agc2_interp_gain_curve_lookups_knee",
                              stats_.look_ups_knee_region);
    apm_data_dumper_->DumpRaw("agc2_interp_gain_curve_lookups_limiter",
                              stats_.look_ups_limiter_region);
    apm_data_dumper_->DumpRaw("agc2_interp_gain_curve_lookups_saturation",
                              stats_.look_ups_saturation_region);
  }
}

void InterpolatedGainCurve::UpdateStats(float input_level) const {
  if (!kComputeStats) {
    return;
  }
  stats_.available = true;

  if (input_level < approximation_params_x_[0]) {
    stats_.look_ups_identity_region++;
  } else if (input_level <
             approximation_params_x_[kInterpolatedGainCurveKneePoints - 1]) {
    stats_.look_ups_knee_region++;
  } else if (input_level < kMaxInputLevelLinear) {
    stats_.look_ups_limiter_region++;
  } else {
    stats_.look_ups_saturation_region++;
  }
}

// Looks up a gain to apply given a non-negative input level.
// The cost of this operation depends on the region in which |input_level|
// falls.
// For the identity and the saturation regions the cost is O(1).
// For the other regions, namely knee and limiter, the cost is
// O(2 + log2(|LightkInterpolatedGainCurveTotalPoints|), plus O(1) for the
// linear interpolation (one product and one sum).
float InterpolatedGainCurve::LookUpGainToApply(float input_level) const {
  UpdateStats(input_level);

  if (input_level <= approximation_params_x_[0]) {
    // Identity region.
    return 1.0f;
  }

  if (input_level >= kMaxInputLevelLinear) {
    // Saturation region. Return a gain value that triggers hard-clipping
    // instead of sticking to the actual gain curve defined by the limiter.
    // Note that different choices affect (i) the scaling of saturating
    // subframes and (ii) the scaling of their adjacent subframes (due to linear
    // interpolation of the gain coefficients) and therefore (iii) the harmonic
    // distorsion.

    // TODO(alessiob): Keep a single method once a decision is made.

    // Saturation gain. Compared to using the gain defined by the limiter, this
    // method returns a constant upperbound, which makes the recovery from
    // saturation slightly quicker (because the overestimated gain is closer to
    // 1.0 than the actual one). Also note that this choice reduces the
    // attenuation of non-saturating samples in the proximity of the saturating
    // burst.
    // return saturation_gain_;

    // Saturating lower bound. The saturing samples exactly hit the clipping
    // level. This method achieves has the lowest harmonic distorsion, but it
    // may reduce the amplitude of the non-saturating samples too much.
    return 32768.f / input_level;
  }

  // Knee and limiter regions; find the linear piece index.
  auto* const it = std::lower_bound(approximation_params_x_.begin(),
                                    approximation_params_x_.end(), input_level);
  const size_t index = std::distance(approximation_params_x_.begin(), it) - 1;
  RTC_DCHECK_LE(0, index);
  RTC_DCHECK_LT(index, approximation_params_m_.size());
  RTC_DCHECK_LE(approximation_params_x_[index], input_level);
  if (index < approximation_params_m_.size() - 1) {
    RTC_DCHECK_LE(input_level, approximation_params_x_[index + 1]);
  }

  // Piece-wise linear interploation.
  const float gain = approximation_params_m_[index] * input_level +
                     approximation_params_q_[index];
  RTC_DCHECK_LE(0.f, gain);
  return gain;
}

}  // namespace webrtc
