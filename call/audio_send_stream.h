/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef CALL_AUDIO_SEND_STREAM_H_
#define CALL_AUDIO_SEND_STREAM_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/media_transport_interface.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "call/rtp_config.h"
#include "modules/audio_processing/include/audio_processing_statistics.h"
#include "stats/data/report_block_data.h"

namespace webrtc {

class AudioFrame;

class AudioSendStream {
 public:
  struct Stats {
    Stats();
    ~Stats();

    // TODO(solenberg): Harmonize naming and defaults with receive stream stats.
    uint32_t local_ssrc = 0;
    int64_t bytes_sent = 0;
    // https://w3c.github.io/webrtc-stats/#dom-rtcoutboundrtpstreamstats-retransmittedbytessent
    uint64_t retransmitted_bytes_sent = 0;
    int32_t packets_sent = 0;
    // https://w3c.github.io/webrtc-stats/#dom-rtcoutboundrtpstreamstats-retransmittedpacketssent
    uint64_t retransmitted_packets_sent = 0;
    int32_t packets_lost = -1;
    float fraction_lost = -1.0f;
    std::string codec_name;
    absl::optional<int> codec_payload_type;
    int32_t ext_seqnum = -1;
    int32_t jitter_ms = -1;
    int64_t rtt_ms = -1;
    int32_t audio_level = -1;
    // See description of "totalAudioEnergy" in the WebRTC stats spec:
    // https://w3c.github.io/webrtc-stats/#dom-rtcmediastreamtrackstats-totalaudioenergy
    double total_input_energy = 0.0;
    double total_input_duration = 0.0;
    bool typing_noise_detected = false;

    ANAStats ana_statistics;
    AudioProcessingStats apm_statistics;

    int64_t target_bitrate_bps = 0;
    // A snapshot of the most recent Report Block with additional data of
    // interest to statistics. Used to implement RTCRemoteInboundRtpStreamStats.
    // Within this list, the ReportBlockData::RTCPReportBlock::source_ssrc(),
    // which is the SSRC of the corresponding outbound RTP stream, is unique.
    std::vector<ReportBlockData> report_block_datas;
  };

  struct Config {
    Config() = delete;
    Config(Transport* send_transport, MediaTransportInterface* media_transport);
    explicit Config(Transport* send_transport);
    ~Config();
    std::string ToString() const;

    // Send-stream specific RTP settings.
    struct Rtp {
      Rtp();
      ~Rtp();
      std::string ToString() const;

      // Sender SSRC.
      uint32_t ssrc = 0;

      // The value to send in the RID RTP header extension if the extension is
      // included in the list of extensions.
      std::string rid;

      // The value to send in the MID RTP header extension if the extension is
      // included in the list of extensions.
      std::string mid;

      // Corresponds to the SDP attribute extmap-allow-mixed.
      bool extmap_allow_mixed = false;

      // RTP header extensions used for the sent stream.
      std::vector<RtpExtension> extensions;

      // RTCP CNAME, see RFC 3550.
      std::string c_name;
    } rtp;

    // Time interval between RTCP report for audio
    int rtcp_report_interval_ms = 5000;

    // Transport for outgoing packets. The transport is expected to exist for
    // the entire life of the AudioSendStream and is owned by the API client.
    Transport* send_transport = nullptr;

    MediaTransportInterface* media_transport = nullptr;

    // Bitrate limits used for variable audio bitrate streams. Set both to -1 to
    // disable audio bitrate adaptation.
    // Note: This is still an experimental feature and not ready for real usage.
    int min_bitrate_bps = -1;
    int max_bitrate_bps = -1;

    double bitrate_priority = 1.0;
    bool has_dscp = false;

    // Defines whether to turn on audio network adaptor, and defines its config
    // string.
    absl::optional<std::string> audio_network_adaptor_config;

    struct SendCodecSpec {
      SendCodecSpec(int payload_type, const SdpAudioFormat& format);
      ~SendCodecSpec();
      std::string ToString() const;

      bool operator==(const SendCodecSpec& rhs) const;
      bool operator!=(const SendCodecSpec& rhs) const {
        return !(*this == rhs);
      }

      int payload_type;
      SdpAudioFormat format;
      bool nack_enabled = false;
      bool transport_cc_enabled = false;
      absl::optional<int> cng_payload_type;
      // If unset, use the encoder's default target bitrate.
      absl::optional<int> target_bitrate_bps;
    };

    absl::optional<SendCodecSpec> send_codec_spec;
    rtc::scoped_refptr<AudioEncoderFactory> encoder_factory;
    absl::optional<AudioCodecPairId> codec_pair_id;

    // Track ID as specified during track creation.
    std::string track_id;

    // Per PeerConnection crypto options.
    webrtc::CryptoOptions crypto_options;

    // An optional custom frame encryptor that allows the entire frame to be
    // encryptor in whatever way the caller choses. This is not required by
    // default.
    rtc::scoped_refptr<webrtc::FrameEncryptorInterface> frame_encryptor;
  };

  virtual ~AudioSendStream() = default;

  virtual const webrtc::AudioSendStream::Config& GetConfig() const = 0;

  // Reconfigure the stream according to the Configuration.
  virtual void Reconfigure(const Config& config) = 0;

  // Starts stream activity.
  // When a stream is active, it can receive, process and deliver packets.
  virtual void Start() = 0;
  // Stops stream activity.
  // When a stream is stopped, it can't receive, process or deliver packets.
  virtual void Stop() = 0;

  // Encode and send audio.
  virtual void SendAudioData(
      std::unique_ptr<webrtc::AudioFrame> audio_frame) = 0;

  // TODO(solenberg): Make payload_type a config property instead.
  virtual bool SendTelephoneEvent(int payload_type,
                                  int payload_frequency,
                                  int event,
                                  int duration_ms) = 0;

  virtual void SetMuted(bool muted) = 0;

  virtual Stats GetStats() const = 0;
  virtual Stats GetStats(bool has_remote_tracks) const = 0;
};
}  // namespace webrtc

#endif  // CALL_AUDIO_SEND_STREAM_H_
