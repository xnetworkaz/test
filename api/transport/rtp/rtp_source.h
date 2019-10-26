/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_TRANSPORT_RTP_RTP_SOURCE_H_
#define API_TRANSPORT_RTP_RTP_SOURCE_H_

#include <stdint.h>

#include "absl/types/optional.h"
#include "api/rtp_headers.h"
#include "rtc_base/checks.h"

namespace webrtc {

enum class RtpSourceType {
  SSRC,
  CSRC,
};

class RtpSource {
 public:
  RtpSource() = delete;

  RtpSource(int64_t timestamp_ms,
            uint32_t source_id,
            RtpSourceType source_type,
            absl::optional<uint8_t> audio_level,
            absl::optional<AbsoluteCaptureTime> absolute_capture_time,
            uint32_t rtp_timestamp)
      : timestamp_ms_(timestamp_ms),
        source_id_(source_id),
        source_type_(source_type),
        audio_level_(audio_level),
        absolute_capture_time_(absolute_capture_time),
        rtp_timestamp_(rtp_timestamp) {}

  RtpSource(const RtpSource&) = default;
  RtpSource& operator=(const RtpSource&) = default;
  ~RtpSource() = default;

  int64_t timestamp_ms() const { return timestamp_ms_; }
  void update_timestamp_ms(int64_t timestamp_ms) {
    RTC_DCHECK_LE(timestamp_ms_, timestamp_ms);
    timestamp_ms_ = timestamp_ms;
  }

  // The identifier of the source can be the CSRC or the SSRC.
  uint32_t source_id() const { return source_id_; }

  // The source can be either a contributing source or a synchronization source.
  RtpSourceType source_type() const { return source_type_; }

  absl::optional<uint8_t> audio_level() const { return audio_level_; }
  void set_audio_level(const absl::optional<uint8_t>& level) {
    audio_level_ = level;
  }

  absl::optional<AbsoluteCaptureTime> absolute_capture_time() const {
    return absolute_capture_time_;
  }

  uint32_t rtp_timestamp() const { return rtp_timestamp_; }

  bool operator==(const RtpSource& o) const {
    return timestamp_ms_ == o.timestamp_ms() && source_id_ == o.source_id() &&
           source_type_ == o.source_type() && audio_level_ == o.audio_level_ &&
           absolute_capture_time_ == o.absolute_capture_time_ &&
           rtp_timestamp_ == o.rtp_timestamp();
  }

 private:
  int64_t timestamp_ms_;
  uint32_t source_id_;
  RtpSourceType source_type_;
  absl::optional<uint8_t> audio_level_;
  absl::optional<AbsoluteCaptureTime> absolute_capture_time_;
  uint32_t rtp_timestamp_;
};

}  // namespace webrtc

#endif  // API_TRANSPORT_RTP_RTP_SOURCE_H_
