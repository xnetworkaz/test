/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/linux/wayland/screencast_stream_utils.h"

#include <libdrm/drm_fourcc.h>

#include <string>

#include "rtc_base/string_encode.h"
#include "rtc_base/string_to_number.h"

#if !PW_CHECK_VERSION(0, 3, 29)
#define SPA_POD_PROP_FLAG_MANDATORY (1u << 3)
#endif
#if !PW_CHECK_VERSION(0, 3, 33)
#define SPA_POD_PROP_FLAG_DONT_FIXATE (1u << 4)
#endif

namespace webrtc {

PipeWireThreadLoopLock::PipeWireThreadLoopLock(pw_thread_loop* loop)
    : loop_(loop) {
  pw_thread_loop_lock(loop_);
}

PipeWireThreadLoopLock::~PipeWireThreadLoopLock() {
  pw_thread_loop_unlock(loop_);
}

PipeWireVersion ParsePipeWireVersion(const char* version) {
  std::vector<std::string> parsed_version;
  rtc::split(version, '.', &parsed_version);

  if (parsed_version.size() != 3) {
    return {};
  }

  absl::optional<int> major = rtc::StringToNumber<int>(parsed_version.at(0));
  absl::optional<int> minor = rtc::StringToNumber<int>(parsed_version.at(1));
  absl::optional<int> micro = rtc::StringToNumber<int>(parsed_version.at(2));

  // Return invalid version if we failed to parse it
  if (!major || !minor || !micro) {
    return {0, 0, 0};
  }
  return {major.value(), micro.value(), micro.value()};
}

bool operator>=(const PipeWireVersion& current_pw_version,
                const PipeWireVersion& required_pw_version) {
  if (!current_pw_version.major && !current_pw_version.minor &&
      !current_pw_version.micro) {
    return false;
  }

  return std::tie(current_pw_version.major, current_pw_version.minor,
                  current_pw_version.micro) >=
         std::tie(required_pw_version.major, required_pw_version.minor,
                  required_pw_version.micro);
}

bool operator<=(const PipeWireVersion& current_pw_version,
                const PipeWireVersion& required_pw_version) {
  if (!current_pw_version.major && !current_pw_version.minor &&
      !current_pw_version.micro) {
    return false;
  }

  return std::tie(current_pw_version.major, current_pw_version.minor,
                  current_pw_version.micro) <=
         std::tie(required_pw_version.major, required_pw_version.minor,
                  required_pw_version.micro);
}

spa_pod* BuildFormat(spa_pod_builder* builder,
                     uint32_t format,
                     const std::vector<uint64_t>& modifiers,
                     struct spa_rectangle* resolution) {
  bool first = true;
  spa_pod_frame frames[2];
  spa_rectangle pw_min_screen_bounds = spa_rectangle{1, 1};
  spa_rectangle pw_max_screen_bounds = spa_rectangle{UINT32_MAX, UINT32_MAX};

  spa_pod_builder_push_object(builder, &frames[0], SPA_TYPE_OBJECT_Format,
                              SPA_PARAM_EnumFormat);
  spa_pod_builder_add(builder, SPA_FORMAT_mediaType,
                      SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
  spa_pod_builder_add(builder, SPA_FORMAT_mediaSubtype,
                      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
  spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);

  if (modifiers.size()) {
    if (modifiers.size() == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID) {
      spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_modifier,
                           SPA_POD_PROP_FLAG_MANDATORY);
      spa_pod_builder_long(builder, modifiers[0]);
    } else {
      spa_pod_builder_prop(
          builder, SPA_FORMAT_VIDEO_modifier,
          SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
      spa_pod_builder_push_choice(builder, &frames[1], SPA_CHOICE_Enum, 0);

      // modifiers from the array
      for (int64_t val : modifiers) {
        spa_pod_builder_long(builder, val);
        // Add the first modifier twice as the very first value is the default
        // option
        if (first) {
          spa_pod_builder_long(builder, val);
          first = false;
        }
      }
      spa_pod_builder_pop(builder, &frames[1]);
    }
  }

  if (resolution) {
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_size,
                        SPA_POD_Rectangle(resolution), 0);
  } else {
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_size,
                        SPA_POD_CHOICE_RANGE_Rectangle(&pw_min_screen_bounds,
                                                       &pw_min_screen_bounds,
                                                       &pw_max_screen_bounds),
                        0);
  }

  return static_cast<spa_pod*>(spa_pod_builder_pop(builder, &frames[0]));
}

}  // namespace webrtc
