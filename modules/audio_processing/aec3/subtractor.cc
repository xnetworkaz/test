/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/aec3/subtractor.h"

#include <algorithm>
#include <utility>

#include "api/array_view.h"
#include "modules/audio_processing/aec3/adaptive_fir_filter_erl.h"
#include "modules/audio_processing/aec3/fft_data.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/safe_minmax.h"

namespace webrtc {

namespace {

void PredictionError(const Aec3Fft& fft,
                     const FftData& S,
                     rtc::ArrayView<const float> y,
                     std::array<float, kBlockSize>* e,
                     std::array<float, kBlockSize>* s) {
  std::array<float, kFftLength> tmp;
  fft.Ifft(S, &tmp);
  constexpr float kScale = 1.0f / kFftLengthBy2;
  std::transform(y.begin(), y.end(), tmp.begin() + kFftLengthBy2, e->begin(),
                 [&](float a, float b) { return a - b * kScale; });

  if (s) {
    for (size_t k = 0; k < s->size(); ++k) {
      (*s)[k] = kScale * tmp[k + kFftLengthBy2];
    }
  }
}

void ScaleFilterOutput(rtc::ArrayView<const float> y,
                       float factor,
                       rtc::ArrayView<float> e,
                       rtc::ArrayView<float> s) {
  RTC_DCHECK_EQ(y.size(), e.size());
  RTC_DCHECK_EQ(y.size(), s.size());
  for (size_t k = 0; k < y.size(); ++k) {
    s[k] *= factor;
    e[k] = y[k] - s[k];
  }
}

}  // namespace

Subtractor::Subtractor(const EchoCanceller3Config& config,
                       size_t num_render_channels,
                       size_t num_capture_channels,
                       ApmDataDumper* data_dumper,
                       Aec3Optimization optimization)
    : fft_(),
      data_dumper_(data_dumper),
      optimization_(optimization),
      config_(config),
      num_capture_channels_(num_capture_channels),
      refined_filters_(num_capture_channels_),
      shadow_filter_(num_capture_channels_),
      refined_gains_(num_capture_channels_),
      shadow_gains_(num_capture_channels_),
      filter_misadjustment_estimators_(num_capture_channels_),
      poor_shadow_filter_counters_(num_capture_channels_, 0),
      refined_frequency_responses_(
          num_capture_channels_,
          std::vector<std::array<float, kFftLengthBy2Plus1>>(
              std::max(config_.filter.refined_initial.length_blocks,
                       config_.filter.refined.length_blocks),
              std::array<float, kFftLengthBy2Plus1>())),
      refined_impulse_responses_(
          num_capture_channels_,
          std::vector<float>(GetTimeDomainLength(std::max(
                                 config_.filter.refined_initial.length_blocks,
                                 config_.filter.refined.length_blocks)),
                             0.f)) {
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    refined_filters_[ch] = std::make_unique<AdaptiveFirFilter>(
        config_.filter.refined.length_blocks,
        config_.filter.refined_initial.length_blocks,
        config.filter.config_change_duration_blocks, num_render_channels,
        optimization, data_dumper_);

    shadow_filter_[ch] = std::make_unique<AdaptiveFirFilter>(
        config_.filter.shadow.length_blocks,
        config_.filter.shadow_initial.length_blocks,
        config.filter.config_change_duration_blocks, num_render_channels,
        optimization, data_dumper_);
    refined_gains_[ch] = std::make_unique<RefinedFilterUpdateGain>(
        config_.filter.refined_initial,
        config_.filter.config_change_duration_blocks);
    shadow_gains_[ch] = std::make_unique<ShadowFilterUpdateGain>(
        config_.filter.shadow_initial,
        config.filter.config_change_duration_blocks);
  }

  RTC_DCHECK(data_dumper_);
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    for (auto& H2_k : refined_frequency_responses_[ch]) {
      H2_k.fill(0.f);
    }
  }
}

Subtractor::~Subtractor() = default;

void Subtractor::HandleEchoPathChange(
    const EchoPathVariability& echo_path_variability) {
  const auto full_reset = [&]() {
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      refined_filters_[ch]->HandleEchoPathChange();
      shadow_filter_[ch]->HandleEchoPathChange();
      refined_gains_[ch]->HandleEchoPathChange(echo_path_variability);
      shadow_gains_[ch]->HandleEchoPathChange();
      refined_gains_[ch]->SetConfig(config_.filter.refined_initial, true);
      shadow_gains_[ch]->SetConfig(config_.filter.shadow_initial, true);
      refined_filters_[ch]->SetSizePartitions(
          config_.filter.refined_initial.length_blocks, true);
      shadow_filter_[ch]->SetSizePartitions(
          config_.filter.shadow_initial.length_blocks, true);
    }
  };

  if (echo_path_variability.delay_change !=
      EchoPathVariability::DelayAdjustment::kNone) {
    full_reset();
  }

  if (echo_path_variability.gain_change) {
    for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
      refined_gains_[ch]->HandleEchoPathChange(echo_path_variability);
    }
  }
}

void Subtractor::ExitInitialState() {
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    refined_gains_[ch]->SetConfig(config_.filter.refined, false);
    shadow_gains_[ch]->SetConfig(config_.filter.shadow, false);
    refined_filters_[ch]->SetSizePartitions(
        config_.filter.refined.length_blocks, false);
    shadow_filter_[ch]->SetSizePartitions(config_.filter.shadow.length_blocks,
                                          false);
  }
}

void Subtractor::Process(const RenderBuffer& render_buffer,
                         const std::vector<std::vector<float>>& capture,
                         const RenderSignalAnalyzer& render_signal_analyzer,
                         const AecState& aec_state,
                         rtc::ArrayView<SubtractorOutput> outputs) {
  RTC_DCHECK_EQ(num_capture_channels_, capture.size());

  // Compute the render powers.
  const bool same_filter_sizes = refined_filters_[0]->SizePartitions() ==
                                 shadow_filter_[0]->SizePartitions();
  std::array<float, kFftLengthBy2Plus1> X2_refined;
  std::array<float, kFftLengthBy2Plus1> X2_shadow_data;
  auto& X2_shadow = same_filter_sizes ? X2_refined : X2_shadow_data;
  if (same_filter_sizes) {
    render_buffer.SpectralSum(refined_filters_[0]->SizePartitions(),
                              &X2_refined);
  } else if (refined_filters_[0]->SizePartitions() >
             shadow_filter_[0]->SizePartitions()) {
    render_buffer.SpectralSums(shadow_filter_[0]->SizePartitions(),
                               refined_filters_[0]->SizePartitions(),
                               &X2_shadow, &X2_refined);
  } else {
    render_buffer.SpectralSums(refined_filters_[0]->SizePartitions(),
                               shadow_filter_[0]->SizePartitions(), &X2_refined,
                               &X2_shadow);
  }

  // Process all capture channels
  for (size_t ch = 0; ch < num_capture_channels_; ++ch) {
    RTC_DCHECK_EQ(kBlockSize, capture[ch].size());
    SubtractorOutput& output = outputs[ch];
    rtc::ArrayView<const float> y = capture[ch];
    FftData& E_refined = output.E_refined;
    FftData E_shadow;
    std::array<float, kBlockSize>& e_refined = output.e_refined;
    std::array<float, kBlockSize>& e_shadow = output.e_shadow;

    FftData S;
    FftData& G = S;

    // Form the outputs of the refined and shadow filters.
    refined_filters_[ch]->Filter(render_buffer, &S);
    PredictionError(fft_, S, y, &e_refined, &output.s_refined);

    shadow_filter_[ch]->Filter(render_buffer, &S);
    PredictionError(fft_, S, y, &e_shadow, &output.s_shadow);

    // Compute the signal powers in the subtractor output.
    output.ComputeMetrics(y);

    // Adjust the filter if needed.
    bool refined_filters_adjusted = false;
    filter_misadjustment_estimators_[ch].Update(output);
    if (filter_misadjustment_estimators_[ch].IsAdjustmentNeeded()) {
      float scale = filter_misadjustment_estimators_[ch].GetMisadjustment();
      refined_filters_[ch]->ScaleFilter(scale);
      for (auto& h_k : refined_impulse_responses_[ch]) {
        h_k *= scale;
      }
      ScaleFilterOutput(y, scale, e_refined, output.s_refined);
      filter_misadjustment_estimators_[ch].Reset();
      refined_filters_adjusted = true;
    }

    // Compute the FFts of the refined and shadow filter outputs.
    fft_.ZeroPaddedFft(e_refined, Aec3Fft::Window::kHanning, &E_refined);
    fft_.ZeroPaddedFft(e_shadow, Aec3Fft::Window::kHanning, &E_shadow);

    // Compute spectra for future use.
    E_shadow.Spectrum(optimization_, output.E2_shadow);
    E_refined.Spectrum(optimization_, output.E2_refined);

    // Update the refined filter.
    if (!refined_filters_adjusted) {
      std::array<float, kFftLengthBy2Plus1> erl;
      ComputeErl(optimization_, refined_frequency_responses_[ch], erl);
      refined_gains_[ch]->Compute(X2_refined, render_signal_analyzer, output,
                                  erl, refined_filters_[ch]->SizePartitions(),
                                  aec_state.SaturatedCapture(), &G);
    } else {
      G.re.fill(0.f);
      G.im.fill(0.f);
    }
    refined_filters_[ch]->Adapt(render_buffer, G,
                                &refined_impulse_responses_[ch]);
    refined_filters_[ch]->ComputeFrequencyResponse(
        &refined_frequency_responses_[ch]);

    if (ch == 0) {
      data_dumper_->DumpRaw("aec3_subtractor_G_refined", G.re);
      data_dumper_->DumpRaw("aec3_subtractor_G_refined", G.im);
    }

    // Update the shadow filter.
    poor_shadow_filter_counters_[ch] =
        output.e2_refined < output.e2_shadow
            ? poor_shadow_filter_counters_[ch] + 1
            : 0;
    if (poor_shadow_filter_counters_[ch] < 5) {
      shadow_gains_[ch]->Compute(X2_shadow, render_signal_analyzer, E_shadow,
                                 shadow_filter_[ch]->SizePartitions(),
                                 aec_state.SaturatedCapture(), &G);
    } else {
      poor_shadow_filter_counters_[ch] = 0;
      shadow_filter_[ch]->SetFilter(refined_filters_[ch]->SizePartitions(),
                                    refined_filters_[ch]->GetFilter());
      shadow_gains_[ch]->Compute(X2_shadow, render_signal_analyzer, E_refined,
                                 shadow_filter_[ch]->SizePartitions(),
                                 aec_state.SaturatedCapture(), &G);
    }

    shadow_filter_[ch]->Adapt(render_buffer, G);
    if (ch == 0) {
      data_dumper_->DumpRaw("aec3_subtractor_G_shadow", G.re);
      data_dumper_->DumpRaw("aec3_subtractor_G_shadow", G.im);
      filter_misadjustment_estimators_[ch].Dump(data_dumper_);
      DumpFilters();
    }

    std::for_each(e_refined.begin(), e_refined.end(),
                  [](float& a) { a = rtc::SafeClamp(a, -32768.f, 32767.f); });

    if (ch == 0) {
      data_dumper_->DumpWav("aec3_refined_filters_output", kBlockSize,
                            &e_refined[0], 16000, 1);
      data_dumper_->DumpWav("aec3_shadow_filter_output", kBlockSize,
                            &e_shadow[0], 16000, 1);
    }
  }
}

void Subtractor::FilterMisadjustmentEstimator::Update(
    const SubtractorOutput& output) {
  e2_acum_ += output.e2_refined;
  y2_acum_ += output.y2;
  if (++n_blocks_acum_ == n_blocks_) {
    if (y2_acum_ > n_blocks_ * 200.f * 200.f * kBlockSize) {
      float update = (e2_acum_ / y2_acum_);
      if (e2_acum_ > n_blocks_ * 7500.f * 7500.f * kBlockSize) {
        // Duration equal to blockSizeMs * n_blocks_ * 4.
        overhang_ = 4;
      } else {
        overhang_ = std::max(overhang_ - 1, 0);
      }

      if ((update < inv_misadjustment_) || (overhang_ > 0)) {
        inv_misadjustment_ += 0.1f * (update - inv_misadjustment_);
      }
    }
    e2_acum_ = 0.f;
    y2_acum_ = 0.f;
    n_blocks_acum_ = 0;
  }
}

void Subtractor::FilterMisadjustmentEstimator::Reset() {
  e2_acum_ = 0.f;
  y2_acum_ = 0.f;
  n_blocks_acum_ = 0;
  inv_misadjustment_ = 0.f;
  overhang_ = 0.f;
}

void Subtractor::FilterMisadjustmentEstimator::Dump(
    ApmDataDumper* data_dumper) const {
  data_dumper->DumpRaw("aec3_inv_misadjustment_factor", inv_misadjustment_);
}

}  // namespace webrtc
