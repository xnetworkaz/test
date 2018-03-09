/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <utility>

#include "call/degraded_call.h"

#include "rtc_base/ptr_util.h"

namespace webrtc {
DegradedCall::DegradedCall(
    std::unique_ptr<Call> call,
    rtc::Optional<FakeNetworkPipe::Config> send_config,
    rtc::Optional<FakeNetworkPipe::Config> receive_config)
    : clock_(Clock::GetRealTimeClock()),
      call_(std::move(call)),
      send_config_(send_config),
      send_process_thread_(
          send_config_ ? ProcessThread::Create("DegradedSendThread") : nullptr),
      num_send_streams_(0),
      receive_config_(receive_config) {
  if (receive_config_) {
    receive_pipe_ =
        rtc::MakeUnique<webrtc::FakeNetworkPipe>(clock_, *receive_config_);
    receive_pipe_->SetReceiver(call_->Receiver());
  }
  if (send_process_thread_) {
    send_process_thread_->Start();
  }
}

DegradedCall::~DegradedCall() {
  if (send_pipe_) {
    send_process_thread_->DeRegisterModule(send_pipe_.get());
  }
  if (send_process_thread_) {
    send_process_thread_->Stop();
  }
}

AudioSendStream* DegradedCall::CreateAudioSendStream(
    const AudioSendStream::Config& config) {
  return call_->CreateAudioSendStream(config);
}

void DegradedCall::DestroyAudioSendStream(AudioSendStream* send_stream) {
  call_->DestroyAudioSendStream(send_stream);
}

AudioReceiveStream* DegradedCall::CreateAudioReceiveStream(
    const AudioReceiveStream::Config& config) {
  return call_->CreateAudioReceiveStream(config);
}

void DegradedCall::DestroyAudioReceiveStream(
    AudioReceiveStream* receive_stream) {
  call_->DestroyAudioReceiveStream(receive_stream);
}

VideoSendStream* DegradedCall::CreateVideoSendStream(
    VideoSendStream::Config config,
    VideoEncoderConfig encoder_config) {
  if (send_config_ && !send_pipe_) {
    send_pipe_ = rtc::MakeUnique<FakeNetworkPipe>(clock_, *send_config_,
                                                  config.send_transport);
    config.send_transport = this;
    send_process_thread_->RegisterModule(send_pipe_.get(), RTC_FROM_HERE);
  }
  ++num_send_streams_;
  return call_->CreateVideoSendStream(std::move(config),
                                      std::move(encoder_config));
}

VideoSendStream* DegradedCall::CreateVideoSendStream(
    VideoSendStream::Config config,
    VideoEncoderConfig encoder_config,
    std::unique_ptr<FecController> fec_controller) {
  return call_->CreateVideoSendStream(
      std::move(config), std::move(encoder_config), std::move(fec_controller));
}

void DegradedCall::DestroyVideoSendStream(VideoSendStream* send_stream) {
  if (send_pipe_ && num_send_streams_ > 0) {
    --num_send_streams_;
    if (num_send_streams_ == 0) {
      send_process_thread_->DeRegisterModule(send_pipe_.get());
      send_pipe_.reset();
    }
  }
  call_->DestroyVideoSendStream(send_stream);
}

VideoReceiveStream* DegradedCall::CreateVideoReceiveStream(
    VideoReceiveStream::Config configuration) {
  return call_->CreateVideoReceiveStream(std::move(configuration));
}

void DegradedCall::DestroyVideoReceiveStream(
    VideoReceiveStream* receive_stream) {
  call_->DestroyVideoReceiveStream(receive_stream);
}

FlexfecReceiveStream* DegradedCall::CreateFlexfecReceiveStream(
    const FlexfecReceiveStream::Config& config) {
  return call_->CreateFlexfecReceiveStream(config);
}

void DegradedCall::DestroyFlexfecReceiveStream(
    FlexfecReceiveStream* receive_stream) {
  call_->DestroyFlexfecReceiveStream(receive_stream);
}

PacketReceiver* DegradedCall::Receiver() {
  if (receive_config_) {
    return this;
  }
  return call_->Receiver();
}

RtpTransportControllerSendInterface*
DegradedCall::GetTransportControllerSend() {
  return call_->GetTransportControllerSend();
}

Call::Stats DegradedCall::GetStats() const {
  return call_->GetStats();
}

void DegradedCall::SetBitrateAllocationStrategy(
    std::unique_ptr<rtc::BitrateAllocationStrategy>
        bitrate_allocation_strategy) {
  call_->SetBitrateAllocationStrategy(std::move(bitrate_allocation_strategy));
}

void DegradedCall::SignalChannelNetworkState(MediaType media,
                                             NetworkState state) {
  call_->SignalChannelNetworkState(media, state);
}

void DegradedCall::OnTransportOverheadChanged(
    MediaType media,
    int transport_overhead_per_packet) {
  call_->OnTransportOverheadChanged(media, transport_overhead_per_packet);
}

void DegradedCall::OnSentPacket(const rtc::SentPacket& sent_packet) {
  if (send_config_) {
    // If we have a degraded send-transport, we have already notified call
    // about the supposed network send time. Discard the actual network send
    // time in order to properly fool the BWE.
    return;
  }
  call_->OnSentPacket(sent_packet);
}

bool DegradedCall::SendRtp(const uint8_t* packet,
                           size_t length,
                           const PacketOptions& options) {
  // A call here comes from the RTP stack (probably pacer). We intercept it and
  // put it in the fake network pipe instead, but report to Call that is has
  // been sent, so that the bandwidth estimator sees the delay we add.
  send_pipe_->SendRtp(packet, length, options);
  if (options.packet_id != -1) {
    rtc::SentPacket packet_info;
    packet_info.packet_id = options.packet_id;
    packet_info.send_time_ms = clock_->TimeInMilliseconds();
    call_->OnSentPacket(packet_info);
  }
  return true;
}

bool DegradedCall::SendRtcp(const uint8_t* packet, size_t length) {
  send_pipe_->SendRtcp(packet, length);
  return true;
}

PacketReceiver::DeliveryStatus DegradedCall::DeliverPacket(
    MediaType media_type,
    rtc::CopyOnWriteBuffer packet,
    const PacketTime& packet_time) {
  PacketReceiver::DeliveryStatus status =
      receive_pipe_->DeliverPacket(media_type, std::move(packet), packet_time);
  // This is not optimal, but without it we make a mess of the expectations of
  // which thread is delivering the packets...
  receive_pipe_->Process();
  return status;
}

}  // namespace webrtc
