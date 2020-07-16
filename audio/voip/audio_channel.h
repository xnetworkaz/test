/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_VOIP_AUDIO_CHANNEL_H_
#define AUDIO_VOIP_AUDIO_CHANNEL_H_

#include <map>
#include <memory>
#include <queue>
#include <utility>

#include "api/task_queue/task_queue_factory.h"
#include "api/voip/voip_base.h"
#include "audio/voip/audio_egress.h"
#include "audio/voip/audio_ingress.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/utility/include/process_thread.h"
#include "rtc_base/ref_count.h"

namespace webrtc {

// AudioChannel represents a single media session and provides APIs over
// AudioIngress and AudioEgress. Note that a single RTP stack is shared with
// these two classes as it has both sending and receiving capabilities.
class AudioChannel : public rtc::RefCountInterface {
 public:
  AudioChannel(Transport* transport,
               uint32_t local_ssrc,
               TaskQueueFactory* task_queue_factory,
               ProcessThread* process_thread,
               AudioMixer* audio_mixer,
               rtc::scoped_refptr<AudioDecoderFactory> decoder_factory);
  ~AudioChannel() override;

  // Set and get ChannelId that this audio channel belongs for debugging and
  // logging purpose.
  void SetId(ChannelId id) { id_ = id; }
  ChannelId GetId() const { return id_; }

  // APIs to start/stop audio channel on each direction.
  void StartSend();
  void StopSend();
  void StartPlay();
  void StopPlay();

  // APIs relayed to AudioEgress.
  bool IsSendingMedia() const { return egress_->IsSending(); }
  AudioSender* GetAudioSender() { return egress_.get(); }
  void SetEncoder(int payload_type,
                  const SdpAudioFormat& encoder_format,
                  std::unique_ptr<AudioEncoder> encoder) {
    egress_->SetEncoder(payload_type, encoder_format, std::move(encoder));
  }
  absl::optional<SdpAudioFormat> GetEncoderFormat() const {
    return egress_->GetEncoderFormat();
  }

  // APIs relayed to AudioIngress.
  bool IsPlaying() const { return ingress_->IsPlaying(); }
  void ReceivedRTPPacket(rtc::ArrayView<const uint8_t> rtp_packet) {
    ingress_->ReceivedRTPPacket(rtp_packet);
  }
  void ReceivedRTCPPacket(rtc::ArrayView<const uint8_t> rtcp_packet) {
    ingress_->ReceivedRTCPPacket(rtcp_packet);
  }
  void SetReceiveCodecs(const std::map<int, SdpAudioFormat>& codecs) {
    ingress_->SetReceiveCodecs(codecs);
  }

 private:
  // ChannelId that this audio channel belongs for logging purpose.
  ChannelId id_;

  // Synchronization is handled internally by AudioMixer.
  AudioMixer* audio_mixer_;

  // Synchronization is handled internally by ProcessThread.
  ProcessThread* process_thread_;

  // Listed in order for safe destruction of AudioChannel object.
  // Synchronization for these are handled internally.
  std::unique_ptr<ReceiveStatistics> receive_statistics_;
  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_;
  std::unique_ptr<AudioIngress> ingress_;
  std::unique_ptr<AudioEgress> egress_;
};

}  // namespace webrtc

#endif  // AUDIO_VOIP_AUDIO_CHANNEL_H_
