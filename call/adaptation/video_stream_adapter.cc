/*
 *  Copyright 2020 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/adaptation/video_stream_adapter.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "api/video/video_adaptation_counters.h"
#include "api/video/video_adaptation_reason.h"
#include "api/video_codecs/video_encoder.h"
#include "call/adaptation/video_source_restrictions.h"
#include "call/adaptation/video_stream_input_state.h"
#include "rtc_base/checks.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/synchronization/sequence_checker.h"

namespace webrtc {

const int kMinFrameRateFps = 2;

namespace {

// For frame rate, the steps we take are 2/3 (down) and 3/2 (up).
int GetLowerFrameRateThan(int fps) {
  RTC_DCHECK(fps != std::numeric_limits<int>::max());
  return (fps * 2) / 3;
}
// TODO(hbos): Use absl::optional<> instead?
int GetHigherFrameRateThan(int fps) {
  return fps != std::numeric_limits<int>::max()
             ? (fps * 3) / 2
             : std::numeric_limits<int>::max();
}

// For resolution, the steps we take are 3/5 (down) and 5/3 (up).
// Notice the asymmetry of which restriction property is set depending on if
// we are adapting up or down:
// - VideoSourceRestrictor::DecreaseResolution() sets the max_pixels_per_frame()
//   to the desired target and target_pixels_per_frame() to null.
// - VideoSourceRestrictor::IncreaseResolutionTo() sets the
//   target_pixels_per_frame() to the desired target, and max_pixels_per_frame()
//   is set according to VideoSourceRestrictor::GetIncreasedMaxPixelsWanted().
int GetLowerResolutionThan(int pixel_count) {
  RTC_DCHECK(pixel_count != std::numeric_limits<int>::max());
  return (pixel_count * 3) / 5;
}

int GetIncreasedMaxPixelsWanted(int target_pixels) {
  if (target_pixels == std::numeric_limits<int>::max())
    return std::numeric_limits<int>::max();
  // When we decrease resolution, we go down to at most 3/5 of current pixels.
  // Thus to increase resolution, we need 3/5 to get back to where we started.
  // When going up, the desired max_pixels_per_frame() has to be significantly
  // higher than the target because the source's native resolutions might not
  // match the target. We pick 12/5 of the target.
  //
  // (This value was historically 4 times the old target, which is (3/5)*4 of
  // the new target - or 12/5 - assuming the target is adjusted according to
  // the above steps.)
  RTC_DCHECK(target_pixels != std::numeric_limits<int>::max());
  return (target_pixels * 12) / 5;
}

bool CanDecreaseResolutionTo(int target_pixels,
                             const VideoStreamInputState& input_state,
                             const VideoSourceRestrictions& restrictions) {
  int max_pixels_per_frame =
      rtc::dchecked_cast<int>(restrictions.max_pixels_per_frame().value_or(
          std::numeric_limits<int>::max()));
  return target_pixels < max_pixels_per_frame &&
         target_pixels >= input_state.min_pixels_per_frame();
}

bool CanIncreaseResolutionTo(int target_pixels,
                             const VideoSourceRestrictions& restrictions) {
  int max_pixels_wanted = GetIncreasedMaxPixelsWanted(target_pixels);
  int max_pixels_per_frame =
      rtc::dchecked_cast<int>(restrictions.max_pixels_per_frame().value_or(
          std::numeric_limits<int>::max()));
  return max_pixels_wanted > max_pixels_per_frame;
}

bool CanDecreaseFrameRateTo(int max_frame_rate,
                            const VideoSourceRestrictions& restrictions) {
  const int fps_wanted = std::max(kMinFrameRateFps, max_frame_rate);
  return fps_wanted <
         rtc::dchecked_cast<int>(restrictions.max_frame_rate().value_or(
             std::numeric_limits<int>::max()));
}

bool CanIncreaseFrameRateTo(int max_frame_rate,
                            const VideoSourceRestrictions& restrictions) {
  return max_frame_rate >
         rtc::dchecked_cast<int>(restrictions.max_frame_rate().value_or(
             std::numeric_limits<int>::max()));
}

}  // namespace

VideoSourceRestrictionsListener::~VideoSourceRestrictionsListener() = default;

VideoSourceRestrictions FilterRestrictionsByDegradationPreference(
    VideoSourceRestrictions source_restrictions,
    DegradationPreference degradation_preference) {
  switch (degradation_preference) {
    case DegradationPreference::BALANCED:
      break;
    case DegradationPreference::MAINTAIN_FRAMERATE:
      source_restrictions.set_max_frame_rate(absl::nullopt);
      break;
    case DegradationPreference::MAINTAIN_RESOLUTION:
      source_restrictions.set_max_pixels_per_frame(absl::nullopt);
      source_restrictions.set_target_pixels_per_frame(absl::nullopt);
      break;
    case DegradationPreference::DISABLED:
      source_restrictions.set_max_pixels_per_frame(absl::nullopt);
      source_restrictions.set_target_pixels_per_frame(absl::nullopt);
      source_restrictions.set_max_frame_rate(absl::nullopt);
  }
  return source_restrictions;
}

// TODO(hbos): Use absl::optional<> instead?
int GetHigherResolutionThan(int pixel_count) {
  return pixel_count != std::numeric_limits<int>::max()
             ? (pixel_count * 5) / 3
             : std::numeric_limits<int>::max();
}

// static
const char* Adaptation::StatusToString(Adaptation::Status status) {
  switch (status) {
    case Adaptation::Status::kValid:
      return "kValid";
    case Adaptation::Status::kLimitReached:
      return "kLimitReached";
    case Adaptation::Status::kAwaitingPreviousAdaptation:
      return "kAwaitingPreviousAdaptation";
    case Status::kInsufficientInput:
      return "kInsufficientInput";
  }
}

Adaptation::Adaptation(int validation_id,
                       VideoSourceRestrictions restrictions,
                       VideoAdaptationCounters counters,
                       VideoStreamInputState input_state,
                       bool min_pixel_limit_reached)
    : validation_id_(validation_id),
      status_(Status::kValid),
      min_pixel_limit_reached_(min_pixel_limit_reached),
      input_state_(std::move(input_state)),
      restrictions_(std::move(restrictions)),
      counters_(std::move(counters)) {}

Adaptation::Adaptation(int validation_id,
                       Status invalid_status,
                       VideoStreamInputState input_state,
                       bool min_pixel_limit_reached)
    : validation_id_(validation_id),
      status_(invalid_status),
      min_pixel_limit_reached_(min_pixel_limit_reached),
      input_state_(std::move(input_state)) {
  RTC_DCHECK_NE(status_, Status::kValid);
}

Adaptation::Status Adaptation::status() const {
  return status_;
}

bool Adaptation::min_pixel_limit_reached() const {
  return min_pixel_limit_reached_;
}

const VideoStreamInputState& Adaptation::input_state() const {
  return input_state_;
}

const VideoSourceRestrictions& Adaptation::restrictions() const {
  return restrictions_;
}

const VideoAdaptationCounters& Adaptation::counters() const {
  return counters_;
}

VideoStreamAdapter::VideoStreamAdapter(
    VideoStreamInputStateProvider* input_state_provider)
    : input_state_provider_(input_state_provider),
      balanced_settings_(),
      adaptation_validation_id_(0),
      degradation_preference_(DegradationPreference::DISABLED),
      awaiting_frame_size_change_(absl::nullopt),
      last_video_source_restrictions_() {
  sequence_checker_.Detach();
}

VideoStreamAdapter::~VideoStreamAdapter() {}

VideoSourceRestrictions VideoStreamAdapter::source_restrictions() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return current_restrictions_.restrictions;
}

const VideoAdaptationCounters& VideoStreamAdapter::adaptation_counters() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return current_restrictions_.counters;
}

void VideoStreamAdapter::ClearRestrictions() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // Invalidate any previously returned Adaptation.
  RTC_LOG(INFO) << "Resetting restrictions";
  ++adaptation_validation_id_;
  current_restrictions_ = {VideoSourceRestrictions(),
                           VideoAdaptationCounters()};
  awaiting_frame_size_change_ = absl::nullopt;
  BroadcastVideoRestrictionsUpdate(nullptr);
}

void VideoStreamAdapter::AddRestrictionsListener(
    VideoSourceRestrictionsListener* restrictions_listener) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(std::find(restrictions_listeners_.begin(),
                       restrictions_listeners_.end(),
                       restrictions_listener) == restrictions_listeners_.end());
  restrictions_listeners_.push_back(restrictions_listener);
}

void VideoStreamAdapter::RemoveRestrictionsListener(
    VideoSourceRestrictionsListener* restrictions_listener) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  auto it = std::find(restrictions_listeners_.begin(),
                      restrictions_listeners_.end(), restrictions_listener);
  RTC_DCHECK(it != restrictions_listeners_.end());
  restrictions_listeners_.erase(it);
}

void VideoStreamAdapter::SetDegradationPreference(
    DegradationPreference degradation_preference) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (degradation_preference_ == degradation_preference)
    return;
  // Invalidate any previously returned Adaptation.
  ++adaptation_validation_id_;
  bool balanced_switch =
      degradation_preference == DegradationPreference::BALANCED ||
      degradation_preference_ == DegradationPreference::BALANCED;
  degradation_preference_ = degradation_preference;
  if (balanced_switch) {
    // ClearRestrictions() calls BroadcastVideoRestrictionsUpdate(nullptr).
    ClearRestrictions();
  } else {
    BroadcastVideoRestrictionsUpdate(nullptr);
  }
}

struct VideoStreamAdapter::RestrictionsOrStateVisitor {
  Adaptation operator()(const RestrictionsWithCounters& r) const {
    return Adaptation(adaptation_validation_id, r.restrictions, r.counters,
                      input_state, min_pixel_limit_reached());
  }
  Adaptation operator()(const Adaptation::Status& status) const {
    RTC_DCHECK_NE(status, Adaptation::Status::kValid);
    return Adaptation(adaptation_validation_id, status, input_state,
                      min_pixel_limit_reached());
  }
  bool min_pixel_limit_reached() const {
    return input_state.frame_size_pixels().has_value() &&
           GetLowerResolutionThan(input_state.frame_size_pixels().value()) <
               input_state.min_pixels_per_frame();
  }

  const int adaptation_validation_id;
  const VideoStreamInputState& input_state;
};

Adaptation VideoStreamAdapter::RestrictionsOrStateToAdaptation(
    VideoStreamAdapter::RestrictionsOrState step_or_state,
    const VideoStreamInputState& input_state) const {
  RTC_DCHECK(!step_or_state.valueless_by_exception());
  return absl::visit(
      RestrictionsOrStateVisitor{adaptation_validation_id_, input_state},
      step_or_state);
}

Adaptation VideoStreamAdapter::GetAdaptationUp(
    const VideoStreamInputState& input_state) const {
  return RestrictionsOrStateToAdaptation(GetAdaptationUpStep(input_state),
                                         input_state);
}

Adaptation VideoStreamAdapter::GetAdaptationUp() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK_NE(degradation_preference_, DegradationPreference::DISABLED);
  VideoStreamInputState input_state = input_state_provider_->InputState();
  ++adaptation_validation_id_;
  Adaptation adaptation = GetAdaptationUp(input_state);
  return adaptation;
}

VideoStreamAdapter::RestrictionsOrState VideoStreamAdapter::GetAdaptationUpStep(
    const VideoStreamInputState& input_state) const {
  if (!HasSufficientInputForAdaptation(input_state)) {
    return Adaptation::Status::kInsufficientInput;
  }
  // Don't adapt if we're awaiting a previous adaptation to have an effect.
  if (awaiting_frame_size_change_ &&
      awaiting_frame_size_change_->pixels_increased &&
      degradation_preference_ == DegradationPreference::MAINTAIN_FRAMERATE &&
      input_state.frame_size_pixels().value() <=
          awaiting_frame_size_change_->frame_size_pixels) {
    return Adaptation::Status::kAwaitingPreviousAdaptation;
  }

  // Maybe propose targets based on degradation preference.
  switch (degradation_preference_) {
    case DegradationPreference::BALANCED: {
      // Attempt to increase target frame rate.
      RestrictionsOrState increase_frame_rate =
          IncreaseFramerate(input_state, current_restrictions_);
      if (absl::holds_alternative<RestrictionsWithCounters>(
              increase_frame_rate)) {
        return increase_frame_rate;
      }
      // else, increase resolution.
      ABSL_FALLTHROUGH_INTENDED;
    }
    case DegradationPreference::MAINTAIN_FRAMERATE: {
      // Attempt to increase pixel count.
      return IncreaseResolution(input_state, current_restrictions_);
    }
    case DegradationPreference::MAINTAIN_RESOLUTION: {
      // Scale up framerate.
      return IncreaseFramerate(input_state, current_restrictions_);
    }
    case DegradationPreference::DISABLED:
      RTC_NOTREACHED();
      return Adaptation::Status::kLimitReached;
  }
}

Adaptation VideoStreamAdapter::GetAdaptationDown() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK_NE(degradation_preference_, DegradationPreference::DISABLED);
  VideoStreamInputState input_state = input_state_provider_->InputState();
  ++adaptation_validation_id_;
  return RestrictionsOrStateToAdaptation(GetAdaptationDownStep(input_state),
                                         input_state);
}

VideoStreamAdapter::RestrictionsOrState
VideoStreamAdapter::GetAdaptationDownStep(
    const VideoStreamInputState& input_state) const {
  if (!HasSufficientInputForAdaptation(input_state)) {
    return Adaptation::Status::kInsufficientInput;
  }
  // Don't adapt if we're awaiting a previous adaptation to have an effect or
  // if we switched degradation preference.
  if (awaiting_frame_size_change_ &&
      !awaiting_frame_size_change_->pixels_increased &&
      degradation_preference_ == DegradationPreference::MAINTAIN_FRAMERATE &&
      input_state.frame_size_pixels().value() >=
          awaiting_frame_size_change_->frame_size_pixels) {
    return Adaptation::Status::kAwaitingPreviousAdaptation;
  }
  // Maybe propose targets based on degradation preference.
  switch (degradation_preference_) {
    case DegradationPreference::BALANCED: {
      // Try scale down framerate, if lower.
      RestrictionsOrState decrease_frame_rate =
          DecreaseFramerate(input_state, current_restrictions_);
      if (absl::holds_alternative<RestrictionsWithCounters>(
              decrease_frame_rate)) {
        return decrease_frame_rate;
      }
      // else, decrease resolution.
      ABSL_FALLTHROUGH_INTENDED;
    }
    case DegradationPreference::MAINTAIN_FRAMERATE: {
      return DecreaseResolution(input_state, current_restrictions_);
    }
    case DegradationPreference::MAINTAIN_RESOLUTION: {
      return DecreaseFramerate(input_state, current_restrictions_);
    }
    case DegradationPreference::DISABLED:
      RTC_NOTREACHED();
      return Adaptation::Status::kLimitReached;
  }
}

VideoStreamAdapter::RestrictionsOrState VideoStreamAdapter::DecreaseResolution(
    const VideoStreamInputState& input_state,
    const RestrictionsWithCounters& current_restrictions) {
  int target_pixels =
      GetLowerResolutionThan(input_state.frame_size_pixels().value());
  if (!CanDecreaseResolutionTo(target_pixels, input_state,
                               current_restrictions.restrictions)) {
    return Adaptation::Status::kLimitReached;
  }
  RestrictionsWithCounters new_restrictions = current_restrictions;
  RTC_LOG(LS_INFO) << "Scaling down resolution, max pixels: " << target_pixels;
  new_restrictions.restrictions.set_max_pixels_per_frame(
      target_pixels != std::numeric_limits<int>::max()
          ? absl::optional<size_t>(target_pixels)
          : absl::nullopt);
  new_restrictions.restrictions.set_target_pixels_per_frame(absl::nullopt);
  ++new_restrictions.counters.resolution_adaptations;
  return new_restrictions;
}

VideoStreamAdapter::RestrictionsOrState VideoStreamAdapter::DecreaseFramerate(
    const VideoStreamInputState& input_state,
    const RestrictionsWithCounters& current_restrictions) const {
  int max_frame_rate;
  if (degradation_preference_ == DegradationPreference::MAINTAIN_RESOLUTION) {
    max_frame_rate = GetLowerFrameRateThan(input_state.frames_per_second());
  } else if (degradation_preference_ == DegradationPreference::BALANCED) {
    max_frame_rate =
        balanced_settings_.MinFps(input_state.video_codec_type(),
                                  input_state.frame_size_pixels().value());
  } else {
    RTC_NOTREACHED();
  }
  if (!CanDecreaseFrameRateTo(max_frame_rate,
                              current_restrictions.restrictions)) {
    return Adaptation::Status::kLimitReached;
  }
  RestrictionsWithCounters new_restrictions = current_restrictions;
  max_frame_rate = std::max(kMinFrameRateFps, max_frame_rate);
  RTC_LOG(LS_INFO) << "Scaling down framerate: " << max_frame_rate;
  new_restrictions.restrictions.set_max_frame_rate(
      max_frame_rate != std::numeric_limits<int>::max()
          ? absl::optional<double>(max_frame_rate)
          : absl::nullopt);
  ++new_restrictions.counters.fps_adaptations;
  return new_restrictions;
}

VideoStreamAdapter::RestrictionsOrState VideoStreamAdapter::IncreaseResolution(
    const VideoStreamInputState& input_state,
    const RestrictionsWithCounters& current_restrictions) {
  int target_pixels = input_state.frame_size_pixels().value();
  if (current_restrictions.counters.resolution_adaptations == 1) {
    RTC_LOG(LS_INFO) << "Removing resolution down-scaling setting.";
    target_pixels = std::numeric_limits<int>::max();
  }
  target_pixels = GetHigherResolutionThan(target_pixels);
  if (!CanIncreaseResolutionTo(target_pixels,
                               current_restrictions.restrictions)) {
    return Adaptation::Status::kLimitReached;
  }
  int max_pixels_wanted = GetIncreasedMaxPixelsWanted(target_pixels);
  RestrictionsWithCounters new_restrictions = current_restrictions;
  RTC_LOG(LS_INFO) << "Scaling up resolution, max pixels: "
                   << max_pixels_wanted;
  new_restrictions.restrictions.set_max_pixels_per_frame(
      max_pixels_wanted != std::numeric_limits<int>::max()
          ? absl::optional<size_t>(max_pixels_wanted)
          : absl::nullopt);
  new_restrictions.restrictions.set_target_pixels_per_frame(
      max_pixels_wanted != std::numeric_limits<int>::max()
          ? absl::optional<size_t>(target_pixels)
          : absl::nullopt);
  --new_restrictions.counters.resolution_adaptations;
  RTC_DCHECK_GE(new_restrictions.counters.resolution_adaptations, 0);
  return new_restrictions;
}

VideoStreamAdapter::RestrictionsOrState VideoStreamAdapter::IncreaseFramerate(
    const VideoStreamInputState& input_state,
    const RestrictionsWithCounters& current_restrictions) const {
  int max_frame_rate;
  if (degradation_preference_ == DegradationPreference::MAINTAIN_RESOLUTION) {
    max_frame_rate = GetHigherFrameRateThan(input_state.frames_per_second());
  } else if (degradation_preference_ == DegradationPreference::BALANCED) {
    max_frame_rate =
        balanced_settings_.MaxFps(input_state.video_codec_type(),
                                  input_state.frame_size_pixels().value());
    // In BALANCED, the max_frame_rate must be checked before proceeding. This
    // is because the MaxFps might be the current Fps and so the balanced
    // settings may want to scale up the resolution.=
    if (!CanIncreaseFrameRateTo(max_frame_rate,
                                current_restrictions.restrictions)) {
      return Adaptation::Status::kLimitReached;
    }
  } else {
    RTC_NOTREACHED();
  }
  if (current_restrictions.counters.fps_adaptations == 1) {
    RTC_LOG(LS_INFO) << "Removing framerate down-scaling setting.";
    max_frame_rate = std::numeric_limits<int>::max();
  }
  if (!CanIncreaseFrameRateTo(max_frame_rate,
                              current_restrictions.restrictions)) {
    return Adaptation::Status::kLimitReached;
  }
  RTC_LOG(LS_INFO) << "Scaling up framerate: " << max_frame_rate;
  RestrictionsWithCounters new_restrictions = current_restrictions;
  new_restrictions.restrictions.set_max_frame_rate(
      max_frame_rate != std::numeric_limits<int>::max()
          ? absl::optional<double>(max_frame_rate)
          : absl::nullopt);
  --new_restrictions.counters.fps_adaptations;
  RTC_DCHECK_GE(new_restrictions.counters.fps_adaptations, 0);
  return new_restrictions;
}

void VideoStreamAdapter::ApplyAdaptation(
    const Adaptation& adaptation,
    rtc::scoped_refptr<Resource> resource) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK_EQ(adaptation.validation_id_, adaptation_validation_id_);
  if (adaptation.status() != Adaptation::Status::kValid)
    return;
  // Remember the input pixels and fps of this adaptation. Used to avoid
  // adapting again before this adaptation has had an effect.
  if (DidIncreaseResolution(current_restrictions_.restrictions,
                            adaptation.restrictions())) {
    awaiting_frame_size_change_.emplace(
        true, adaptation.input_state().frame_size_pixels().value());
  } else if (DidDecreaseResolution(current_restrictions_.restrictions,
                                   adaptation.restrictions())) {
    awaiting_frame_size_change_.emplace(
        false, adaptation.input_state().frame_size_pixels().value());
  } else {
    awaiting_frame_size_change_ = absl::nullopt;
  }
  // Adapt!
  current_restrictions_ = {adaptation.restrictions(), adaptation.counters()};
  BroadcastVideoRestrictionsUpdate(resource);
}

Adaptation VideoStreamAdapter::GetAdaptationTo(
    const VideoAdaptationCounters& counters,
    const VideoSourceRestrictions& restrictions) {
  // Adapts up/down from the current levels so counters are equal.
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  VideoStreamInputState input_state = input_state_provider_->InputState();
  return Adaptation(adaptation_validation_id_, restrictions, counters,
                    input_state, false);
}

void VideoStreamAdapter::BroadcastVideoRestrictionsUpdate(
    const rtc::scoped_refptr<Resource>& resource) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  VideoSourceRestrictions filtered = FilterRestrictionsByDegradationPreference(
      source_restrictions(), degradation_preference_);
  if (last_filtered_restrictions_ == filtered) {
    return;
  }
  for (auto* restrictions_listener : restrictions_listeners_) {
    restrictions_listener->OnVideoSourceRestrictionsUpdated(
        filtered, current_restrictions_.counters, resource,
        source_restrictions());
  }
  last_video_source_restrictions_ = current_restrictions_.restrictions;
  last_filtered_restrictions_ = filtered;
}

bool VideoStreamAdapter::HasSufficientInputForAdaptation(
    const VideoStreamInputState& input_state) const {
  return input_state.HasInputFrameSizeAndFramesPerSecond() &&
         (degradation_preference_ !=
              DegradationPreference::MAINTAIN_RESOLUTION ||
          input_state.frames_per_second() >= kMinFrameRateFps);
}

VideoStreamAdapter::AwaitingFrameSizeChange::AwaitingFrameSizeChange(
    bool pixels_increased,
    int frame_size_pixels)
    : pixels_increased(pixels_increased),
      frame_size_pixels(frame_size_pixels) {}

}  // namespace webrtc
