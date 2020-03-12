/*
 *  Copyright 2020 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_ADAPTATION_VIDEO_STREAM_ADAPTER_H_
#define VIDEO_ADAPTATION_VIDEO_STREAM_ADAPTER_H_

#include <memory>

#include "absl/types/optional.h"
#include "api/rtp_parameters.h"
#include "call/adaptation/encoder_settings.h"
#include "call/adaptation/resource.h"
#include "call/adaptation/video_source_restrictions.h"
#include "modules/video_coding/utility/quality_scaler.h"
#include "rtc_base/experiments/balanced_degradation_settings.h"
#include "video/adaptation/adaptation_counters.h"

namespace webrtc {

class VideoStreamAdapter;

// Represents one step that the VideoStreamAdapter can take when adapting the
// VideoSourceRestrictions up or down. Or, if adaptation is not valid, provides
// a Status code indicating the reason for not adapting.
class Adaptation final {
 public:
  enum class Status {
    // Applying this adaptation will have an effect. All other Status codes
    // indicate that adaptation is not possible and why.
    kIsValid,
    // Cannot adapt. DegradationPreference is DISABLED.
    // TODO(hbos): Don't support DISABLED, it doesn't exist in the spec and it
    // causes all adaptation to be ignored, even QP-scaling.
    kAdaptationDisabled,
    // Cannot adapt. Adaptation is refused because we don't have video, the
    // input frame rate is not known yet or is less than the minimum allowed
    // (below the limit).
    kInsufficientInput,
    // Cannot adapt. The minimum or maximum adaptation has already been reached.
    // There are no more steps to take.
    kLimitReached,
    // Cannot adapt. The resolution or frame rate requested by a recent
    // adaptation has not yet been reflected in the input resolution or frame
    // rate; adaptation is refused to avoid "double-adapting".
    // TODO(hbos): Can this be rephrased as a resource usage measurement
    // cooldown mechanism? In a multi-stream setup, we need to wait before
    // adapting again across streams. The best way to achieve this is probably
    // to not act on racy resource usage measurements, regardless of individual
    // adapters. When this logic is moved or replaced then remove this enum
    // value.
    kAwaitingPreviousAdaptation,
    // Cannot adapt. The adaptation that would have been proposed by the adapter
    // violates bitrate constraints and is therefore rejected.
    // TODO(hbos): This is a version of being resource limited, except in order
    // to know if we are constrained we need to have a proposed adaptation in
    // mind, thus the resource alone cannot determine this in isolation.
    // Proposal: ask resources for permission to apply a proposed adaptation.
    // This allows rejecting a given resolution or frame rate based on bitrate
    // limits without coupling it with the adapter's proposal logic. When this
    // is done, remove this enum value.
    kIsBitrateConstrained,
  };

  Status status() const;
  bool min_pixel_limit_reached() const;

 private:
  // The adapter needs to know about step type and step target in order to
  // construct and perform an Adaptation, which is a detail we do not want to
  // expose to the public interface.
  friend class VideoStreamAdapter;

  enum class StepType {
    kIncreaseResolution,
    kDecreaseResolution,
    kIncreaseFrameRate,
    kDecreaseFrameRate,
  };

  struct Step {
    Step(StepType type, int target);
    const StepType type;
    const int target;  // Pixel or frame rate depending on |type|.
  };

  // Constructs with a valid adaptation Step. Status is kIsValid.
  explicit Adaptation(Step step);
  Adaptation(Step step, bool min_pixel_limit_reached);
  // Constructor when adaptation is not valid. Status MUST NOT be kIsValid.
  explicit Adaptation(Status invalid_status);
  Adaptation(Status invalid_status, bool min_pixel_limit_reached);

  // Only applicable if |status_| is kIsValid.
  const Step& step() const;

  const Status status_;
  const absl::optional<Step> step_;
  const bool min_pixel_limit_reached_;
};

// Owns the VideoSourceRestriction for a single stream and is responsible for
// adapting it up or down when told to do so. This class serves the following
// purposes:
// 1. Keep track of a stream's restrictions.
// 2. Provide valid ways to adapt up or down the stream's restrictions.
// 3. Modify the stream's restrictions in one of the valid ways.
class VideoStreamAdapter {
 public:
  enum class SetDegradationPreferenceResult {
    kRestrictionsNotCleared,
    kRestrictionsCleared,
  };

  enum class VideoInputMode {
    kNoVideo,
    kNormalVideo,
    kScreenshareVideo,
  };

  VideoStreamAdapter();
  ~VideoStreamAdapter();

  VideoSourceRestrictions source_restrictions() const;
  const AdaptationCounters& adaptation_counters() const;
  // TODO(hbos): Can we get rid of any external dependencies on
  // BalancedDegradationPreference? How the adaptor generates possible next
  // steps for adaptation should be an implementation detail. Can the relevant
  // information be inferred from AdaptationTargetOrReason?
  const BalancedDegradationSettings& balanced_settings() const;
  void ClearRestrictions();

  // TODO(hbos): Setting the degradation preference should not clear
  // restrictions! This is not defined in the spec and is unexpected, there is a
  // tiny risk that people would discover and rely on this behavior.
  SetDegradationPreferenceResult SetDegradationPreference(
      DegradationPreference degradation_preference);

  void SetInput(VideoInputMode input_mode,
                int input_pixels,
                int input_fps,
                absl::optional<EncoderSettings> encoder_settings,
                absl::optional<uint32_t> encoder_target_bitrate_bps);

  // Returns an adaptation that we are guaranteed to be able to apply, or a
  // status code indicating the reason why we cannot adapt.
  Adaptation GetAdaptationUp(
      AdaptationObserverInterface::AdaptReason reason) const;
  Adaptation GetAdaptationDown() const;
  // Returns the restrictions that result from applying the adaptation, without
  // actually applying it. If the adaptation is not valid, current restrictions
  // are returned.
  VideoSourceRestrictions PeekNextRestrictions(
      const Adaptation& adaptation) const;
  // Applies the adaptation.
  // TODO(hbos): Delete ResourceListenerResponse!
  ResourceListenerResponse ApplyAdaptation(const Adaptation& adaptation);

 private:
  class VideoSourceRestrictor;

  // The input frame rate and resolution at the time of an adaptation in the
  // direction described by |mode_| (up or down).
  // TODO(https://crbug.com/webrtc/11393): Can this be renamed? Can this be
  // merged with AdaptationTarget?
  struct AdaptationRequest {
    // The pixel count produced by the source at the time of the adaptation.
    int input_pixel_count_;
    // Framerate received from the source at the time of the adaptation.
    int framerate_fps_;
    // Indicates if request was to adapt up or down.
    enum class Mode { kAdaptUp, kAdaptDown } mode_;

    // This is a static method rather than an anonymous namespace function due
    // to namespace visiblity.
    static Mode GetModeFromAdaptationAction(Adaptation::StepType step_type);
  };

  // Reinterprets "balanced + screenshare" as "maintain-resolution".
  // TODO(hbos): Don't do this. This is not what "balanced" means. If the
  // application wants to maintain resolution it should set that degradation
  // preference rather than depend on non-standard behaviors.
  DegradationPreference EffectiveDegradationPreference() const;

  // Owner and modifier of the VideoSourceRestriction of this stream adaptor.
  const std::unique_ptr<VideoSourceRestrictor> source_restrictor_;
  // Decides the next adaptation target in DegradationPreference::BALANCED.
  const BalancedDegradationSettings balanced_settings_;
  // When deciding the next target up or down, different strategies are used
  // depending on the DegradationPreference.
  // https://w3c.github.io/mst-content-hint/#dom-rtcdegradationpreference
  DegradationPreference degradation_preference_;

  VideoInputMode input_mode_;
  int input_pixels_;
  int input_fps_;
  absl::optional<EncoderSettings> encoder_settings_;
  absl::optional<uint32_t> encoder_target_bitrate_bps_;
  // The input frame rate, resolution and adaptation direction of the last
  // ApplyAdaptationTarget(). Used to avoid adapting twice if a recent
  // adaptation has not had an effect on the input frame rate or resolution yet.
  // TODO(hbos): Can we implement a more general "cooldown" mechanism of
  // resources intead? If we already have adapted it seems like we should wait
  // a while before adapting again, so that we are not acting on usage
  // measurements that are made obsolete/unreliable by an "ongoing" adaptation.
  absl::optional<AdaptationRequest> last_adaptation_request_;
};

}  // namespace webrtc

#endif  // VIDEO_ADAPTATION_VIDEO_STREAM_ADAPTER_H_
