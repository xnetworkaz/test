/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_format.h"

#include <utility>

#include "modules/rtp_rtcp/source/rtp_format_h264.h"
#include "modules/rtp_rtcp/source/rtp_format_video_generic.h"
#include "modules/rtp_rtcp/source/rtp_format_vp8.h"
#include "modules/rtp_rtcp/source/rtp_format_vp9.h"

namespace webrtc {

std::unique_ptr<RtpPacketizer> RtpPacketizer::Create(
    VideoCodecType type,
    const RTPVideoHeader& rtp_video_header,
    rtc::ArrayView<const uint8_t> payload,
    Options options) {
  switch (type) {
    case kVideoCodecH264: {
      const auto& h264 =
          absl::get<RTPVideoHeaderH264>(rtp_video_header.video_type_header);
      return absl::make_unique<RtpPacketizerH264>(h264.packetization_mode,
                                                  payload, options);
    }
    case kVideoCodecVP8: {
      const auto& vp8 =
          absl::get<RTPVideoHeaderVP8>(rtp_video_header.video_type_header);
      return absl::make_unique<RtpPacketizerVp8>(vp8, payload, options);
    }
    case kVideoCodecVP9: {
      const auto& vp9 =
          absl::get<RTPVideoHeaderVP9>(rtp_video_header.video_type_header);
      return absl::make_unique<RtpPacketizerVp9>(vp9, payload, options);
    }
    default:
      return absl::make_unique<RtpPacketizerGeneric>(rtp_video_header, payload,
                                                     options);
  }
}

RtpDepacketizer* RtpDepacketizer::Create(VideoCodecType type) {
  switch (type) {
    case kVideoCodecH264:
      return new RtpDepacketizerH264();
    case kVideoCodecVP8:
      return new RtpDepacketizerVp8();
    case kVideoCodecVP9:
      return new RtpDepacketizerVp9();
    default:
      return new RtpDepacketizerGeneric();
  }
}
}  // namespace webrtc
