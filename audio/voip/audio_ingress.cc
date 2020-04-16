/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/voip/audio_ingress.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "api/audio_codecs/audio_format.h"
#include "audio/utility/audio_frame_operations.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_minmax.h"

namespace webrtc {

namespace {

AudioCodingModule::Config CreateAcmConfig(
    rtc::scoped_refptr<AudioDecoderFactory> decoder_factory) {
  AudioCodingModule::Config acm_config;
  acm_config.neteq_config.enable_muted_state = true;
  acm_config.decoder_factory = decoder_factory;
  return acm_config;
}

}  // namespace

AudioIngress::AudioIngress(
    RtpRtcp* rtp_rtcp,
    Clock* clock,
    rtc::scoped_refptr<AudioDecoderFactory> decoder_factory,
    std::unique_ptr<ReceiveStatistics> receive_statistics)
    : playing_(false),
      remote_ssrc_(0),
      rtp_receive_statistics_(std::move(receive_statistics)),
      first_rtp_timestamp_(-1),
      rtp_rtcp_(rtp_rtcp),
      acm_receiver_(CreateAcmConfig(decoder_factory)),
      ntp_estimator_(clock) {}

AudioIngress::~AudioIngress() = default;

void AudioIngress::StartPlay() {
  playing_ = true;
}

void AudioIngress::StopPlay() {
  playing_ = false;
  output_audio_level_.ResetLevelFullRange();
}

AudioMixer::Source::AudioFrameInfo AudioIngress::GetAudioFrameWithInfo(
    int sampling_rate,
    AudioFrame* audio_frame) {
  audio_frame->sample_rate_hz_ = sampling_rate;

  // Get 10ms raw PCM data from the ACM.
  bool muted = false;
  if (acm_receiver_.GetAudio(sampling_rate, audio_frame, &muted) == -1) {
    RTC_DLOG(LS_ERROR) << "GetAudio() failed!";
    // In all likelihood, the audio in this frame is garbage. We return an
    // error so that the audio mixer module doesn't add it to the mix. As
    // a result, it won't be played out and the actions skipped here are
    // irrelevant.
    return AudioMixer::Source::AudioFrameInfo::kError;
  }

  if (muted) {
    AudioFrameOperations::Mute(audio_frame);
  }

  // Measure audio level
  constexpr double kAudioSampleDurationSeconds = 0.01;
  output_audio_level_.ComputeLevel(*audio_frame, kAudioSampleDurationSeconds);

  // Set first rtp timestamp with first audio frame with valid timestamp.
  if (first_rtp_timestamp_ < 0 && audio_frame->timestamp_ != 0) {
    first_rtp_timestamp_ = audio_frame->timestamp_;
  }

  if (first_rtp_timestamp_ >= 0) {
    // Compute elapsed time.
    int64_t unwrap_timestamp =
        rtp_ts_wraparound_handler_.Unwrap(audio_frame->timestamp_);
    // For clock rate, default to the playout sampling rate if we haven't
    // received any packets yet.
    absl::optional<std::pair<int, SdpAudioFormat>> decoder =
        acm_receiver_.LastDecoder();
    int clock_rate = decoder ? decoder->second.clockrate_hz
                             : acm_receiver_.last_output_sample_rate_hz();
    RTC_DCHECK_GT(clock_rate, 0);
    audio_frame->elapsed_time_ms_ =
        (unwrap_timestamp - first_rtp_timestamp_) / (clock_rate / 1000);

    // Get ntp time.
    audio_frame->ntp_time_ms_ =
        ntp_estimator_.Estimate(audio_frame->timestamp_);
  }

  return muted ? AudioMixer::Source::AudioFrameInfo::kMuted
               : AudioMixer::Source::AudioFrameInfo::kNormal;
}

int AudioIngress::Ssrc() const {
  return rtc::dchecked_cast<int>(remote_ssrc_.load());
}

int AudioIngress::PreferredSampleRate() const {
  // Return the bigger of playout and receive frequency in the ACM.
  return std::max(acm_receiver_.last_packet_sample_rate_hz().value_or(0),
                  acm_receiver_.last_output_sample_rate_hz());
}

void AudioIngress::SetReceiveCodecs(
    const std::map<int, SdpAudioFormat>& codecs) {
  receive_codec_info_.SetCodecs(codecs);
  acm_receiver_.SetCodecs(codecs);
}

void AudioIngress::ReceivedRTPPacket(const uint8_t* data, size_t length) {
  if (!Playing()) {
    return;
  }

  RtpPacketReceived rtp_packet;
  rtp_packet.Parse(data, length);

  // Set payload type's sampling rate before we feed it into ReceiveStatistics.
  int sampling_rate =
      receive_codec_info_.GetSamplingRate(rtp_packet.PayloadType());
  if (sampling_rate == -1) {
    return;
  }
  rtp_packet.set_payload_type_frequency(sampling_rate);

  rtp_receive_statistics_->OnRtpPacket(rtp_packet);

  RTPHeader header;
  rtp_packet.GetHeader(&header);

  size_t packet_length = rtp_packet.size();
  if (packet_length < header.headerLength ||
      (packet_length - header.headerLength) < header.paddingLength) {
    RTC_DLOG(LS_ERROR) << "packet length(" << packet_length << ") header("
                       << header.headerLength << ") padding("
                       << header.paddingLength << ")";
    return;
  }

  const uint8_t* payload = rtp_packet.data() + header.headerLength;
  size_t payload_length = packet_length - header.headerLength;
  size_t payload_data_length = payload_length - header.paddingLength;

  // Push the incoming payload (parsed and ready for decoding) into the ACM.
  if (acm_receiver_.InsertPacket(
          header,
          rtc::ArrayView<const uint8_t>(payload, payload_data_length)) != 0) {
    RTC_DLOG(LS_ERROR) << "AudioIngress::ReceivedRTPPacket() unable to "
                          "push data to the ACM";
  }
}

void AudioIngress::ReceivedRTCPPacket(const uint8_t* data, size_t length) {
  // Deliver RTCP packet to RTP/RTCP module for parsing
  rtp_rtcp_->IncomingRtcpPacket(data, length);

  int64_t rtt = GetRoundTripTime();
  if (rtt == 0) {
    // Waiting for valid RTT.
    return;
  }

  uint32_t ntp_secs = 0, ntp_frac = 0, rtp_timestamp = 0;
  if (rtp_rtcp_->RemoteNTP(&ntp_secs, &ntp_frac, nullptr, nullptr,
                           &rtp_timestamp) != 0) {
    // Waiting for RTCP.
    return;
  }

  ntp_estimator_.UpdateRtcpTimestamp(rtt, ntp_secs, ntp_frac, rtp_timestamp);
}

int64_t AudioIngress::GetRoundTripTime() {
  std::vector<RTCPReportBlock> report_blocks;
  rtp_rtcp_->RemoteRTCPStat(&report_blocks);

  // If we do not have report block which means remote RTCP hasn't be received
  // yet, return 0 as to indicate uninitialized value.
  if (report_blocks.empty()) {
    return 0;
  }

  // We don't know in advance the remote ssrc used by the other end's receiver
  // reports, so use the SSRC of the first report block as remote ssrc for now.
  // TODO(natim@webrtc.org): handle the case where remote end is changing ssrc
  // and update accordingly here.
  uint32_t sender_ssrc = report_blocks[0].sender_ssrc;

  if (sender_ssrc != remote_ssrc_.load()) {
    remote_ssrc_.store(sender_ssrc);
    rtp_rtcp_->SetRemoteSSRC(sender_ssrc);
  }

  int64_t rtt = 0, avg_rtt = 0, max_rtt = 0, min_rtt = 0;
  rtp_rtcp_->RTT(sender_ssrc, &rtt, &avg_rtt, &min_rtt, &max_rtt);

  return rtt;
}

int AudioIngress::GetSpeechOutputLevelFullRange() const {
  return output_audio_level_.LevelFullRange();
}

bool AudioIngress::Playing() const {
  return playing_;
}

NetworkStatistics AudioIngress::GetNetworkStatistics() const {
  NetworkStatistics stats;
  acm_receiver_.GetNetworkStatistics(&stats);
  return stats;
}

AudioDecodingCallStats AudioIngress::GetDecodingStatistics() const {
  AudioDecodingCallStats stats;
  acm_receiver_.GetDecodingCallStatistics(&stats);
  return stats;
}

AudioIngress::NtpEstimator::NtpEstimator(Clock* clock)
    : ntp_estimator_(clock) {}

bool AudioIngress::NtpEstimator::UpdateRtcpTimestamp(int64_t rtt,
                                                     uint32_t ntp_secs,
                                                     uint32_t ntp_frac,
                                                     uint32_t rtp_timestamp) {
  rtc::CritScope lock(&lock_);
  return ntp_estimator_.UpdateRtcpTimestamp(rtt, ntp_secs, ntp_frac,
                                            rtp_timestamp);
}

int64_t AudioIngress::NtpEstimator::Estimate(uint32_t rtp_timestamp) {
  rtc::CritScope lock(&lock_);
  return ntp_estimator_.Estimate(rtp_timestamp);
}

void AudioIngress::ReceiveCodecInfo::SetCodecs(
    const std::map<int, SdpAudioFormat>& codecs) {
  rtc::CritScope lock(&lock_);
  for (const auto& kv : codecs) {
    payload_type_sampling_rate_[kv.first] = kv.second.clockrate_hz;
  }
}

int AudioIngress::ReceiveCodecInfo::GetSamplingRate(int payload_type) {
  rtc::CritScope lock(&lock_);
  const auto& it = payload_type_sampling_rate_.find(payload_type);
  if (it == payload_type_sampling_rate_.end()) {
    return -1;
  }
  return it->second;
}

}  // namespace webrtc
