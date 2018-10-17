/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/audio_send_stream.h"
#include "rtc_base/stringencode.h"
#include "rtc_base/strings/audio_format_to_string.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {

AudioSendStream::Stats::Stats() = default;
AudioSendStream::Stats::~Stats() = default;

AudioSendStream::Config::Config(Transport* send_transport)
    : send_transport(send_transport) {}

AudioSendStream::Config::~Config() = default;

std::string AudioSendStream::Config::ToString() const {
  char buf[1024];
  rtc::SimpleStringBuilder ss(buf);
  ss << "{rtp: " << rtp.ToString();
  ss << ", send_transport: " << (send_transport ? "(Transport)" : "null");
  ss << ", min_bitrate_bps: " << min_bitrate_bps;
  ss << ", max_bitrate_bps: " << max_bitrate_bps;
  ss << ", send_codec_spec: "
     << (send_codec_spec ? send_codec_spec->ToString() : "<unset>");
  ss << '}';
  return ss.str();
}

AudioSendStream::Config::Rtp::Rtp() = default;

AudioSendStream::Config::Rtp::~Rtp() = default;

std::string AudioSendStream::Config::Rtp::ToString() const {
  char buf[1024];
  rtc::SimpleStringBuilder ss(buf);
  ss << "{ssrc: " << ssrc;
  ss << ", extmap-allow-mixed: "
     << (mixed_one_two_byte_header_extensions_supported ? "true" : "false");
  ss << ", extensions: [";
  for (size_t i = 0; i < extensions.size(); ++i) {
    ss << extensions[i].ToString();
    if (i != extensions.size() - 1) {
      ss << ", ";
    }
  }
  ss << ']';
  ss << ", nack: " << nack.ToString();
  ss << ", c_name: " << c_name;
  ss << '}';
  return ss.str();
}

AudioSendStream::Config::SendCodecSpec::SendCodecSpec(
    int payload_type,
    const SdpAudioFormat& format)
    : payload_type(payload_type), format(format) {}
AudioSendStream::Config::SendCodecSpec::~SendCodecSpec() = default;

std::string AudioSendStream::Config::SendCodecSpec::ToString() const {
  char buf[1024];
  rtc::SimpleStringBuilder ss(buf);
  ss << "{nack_enabled: " << (nack_enabled ? "true" : "false");
  ss << ", transport_cc_enabled: " << (transport_cc_enabled ? "true" : "false");
  ss << ", cng_payload_type: "
     << (cng_payload_type ? rtc::ToString(*cng_payload_type) : "<unset>");
  ss << ", payload_type: " << payload_type;
  ss << ", format: " << rtc::ToString(format);
  ss << '}';
  return ss.str();
}

bool AudioSendStream::Config::SendCodecSpec::operator==(
    const AudioSendStream::Config::SendCodecSpec& rhs) const {
  if (nack_enabled == rhs.nack_enabled &&
      transport_cc_enabled == rhs.transport_cc_enabled &&
      cng_payload_type == rhs.cng_payload_type &&
      payload_type == rhs.payload_type && format == rhs.format &&
      target_bitrate_bps == rhs.target_bitrate_bps) {
    return true;
  }
  return false;
}
}  // namespace webrtc
