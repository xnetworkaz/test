/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_CHANNEL_PROXY_H_
#define AUDIO_CHANNEL_PROXY_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_encoder.h"
#include "audio/channel.h"
#include "rtc_base/constructormagic.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/thread_checker.h"

namespace webrtc {

class AudioSinkInterface;
class PacketRouter;
class RtcEventLog;
class RtcpBandwidthObserver;
class RtcpRttStats;
class RtpPacketSender;
class RtpPacketReceived;
class RtpRtcp;
class RtpTransportControllerSendInterface;
class Transport;
class TransportFeedbackObserver;

namespace voe {

// This class provides the "view" of a voe::Channel that we need to implement
// webrtc::AudioSendStream and webrtc::AudioReceiveStream. It serves two
// purposes:
//  1. Allow mocking just the interfaces used, instead of the entire
//     voe::Channel class.
//  2. Provide a refined interface for the stream classes, including assumptions
//     on return values and input adaptation.
class ChannelProxy {
 public:
  ChannelProxy();
  explicit ChannelProxy(std::unique_ptr<Channel> channel);
  virtual ~ChannelProxy();

  virtual bool SetEncoder(int payload_type,
                          std::unique_ptr<AudioEncoder> encoder);
  virtual void ModifyEncoder(
      rtc::FunctionView<void(std::unique_ptr<AudioEncoder>*)> modifier);

  virtual void SetRTCPStatus(bool enable);
  virtual void SetLocalSSRC(uint32_t ssrc);
  virtual void SetMid(const std::string& mid, int extension_id);
  virtual void SetRTCP_CNAME(const std::string& c_name);
  virtual void SetNACKStatus(bool enable, int max_packets);
  virtual void SetSendAudioLevelIndicationStatus(bool enable, int id);
  virtual void EnableSendTransportSequenceNumber(int id);
  virtual void RegisterSenderCongestionControlObjects(
      RtpTransportControllerSendInterface* transport,
      RtcpBandwidthObserver* bandwidth_observer);
  virtual void ResetSenderCongestionControlObjects();
  virtual CallStatistics GetRTCPStatistics() const;
  virtual std::vector<ReportBlock> GetRemoteRTCPReportBlocks() const;
  virtual NetworkStatistics GetNetworkStatistics() const;
  virtual AudioDecodingCallStats GetDecodingCallStatistics() const;
  virtual ANAStats GetANAStatistics() const;
  virtual uint32_t GetDelayEstimate() const;
  virtual bool SetSendTelephoneEventPayloadType(int payload_type,
                                                int payload_frequency);
  virtual bool SendTelephoneEventOutband(int event, int duration_ms);
  virtual void SetBitrate(int bitrate_bps, int64_t probing_interval_ms);
  virtual void SetInputMute(bool muted);
  virtual void RegisterTransport(Transport* transport);

  virtual bool ReceivedRTCPPacket(const uint8_t* packet, size_t length);
  virtual int PreferredSampleRate() const;
  virtual void ProcessAndEncodeAudio(std::unique_ptr<AudioFrame> audio_frame);
  virtual void SetTransportOverhead(int transport_overhead_per_packet);
  virtual RtpRtcp* GetRtpRtcp() const;
  virtual int64_t GetRTT() const;

  virtual uint32_t GetPlayoutTimestamp() const;
  virtual void SetMinimumPlayoutDelay(int delay_ms);
  virtual void OnTwccBasedUplinkPacketLossRate(float packet_loss_rate);
  virtual void OnRecoverableUplinkPacketLossRate(
      float recoverable_packet_loss_rate);
  virtual void StartSend();
  virtual void StopSend();

 private:
  // Thread checkers document and lock usage of some methods on voe::Channel to
  // specific threads we know about. The goal is to eventually split up
  // voe::Channel into parts with single-threaded semantics, and thereby reduce
  // the need for locks.
  rtc::ThreadChecker worker_thread_checker_;
  rtc::ThreadChecker module_process_thread_checker_;
  // Methods accessed from audio and video threads are checked for sequential-
  // only access. We don't necessarily own and control these threads, so thread
  // checkers cannot be used. E.g. Chromium may transfer "ownership" from one
  // audio thread to another, but access is still sequential.
  rtc::RaceChecker audio_thread_race_checker_;
  rtc::RaceChecker video_capture_thread_race_checker_;
  std::unique_ptr<Channel> channel_;

  RTC_DISALLOW_COPY_AND_ASSIGN(ChannelProxy);
};
}  // namespace voe
}  // namespace webrtc

#endif  // AUDIO_CHANNEL_PROXY_H_
