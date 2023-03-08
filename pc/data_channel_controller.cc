/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/data_channel_controller.h"

#include <utility>

#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "pc/peer_connection_internal.h"
#include "pc/sctp_utils.h"
#include "rtc_base/logging.h"

namespace webrtc {

DataChannelController::~DataChannelController() {}

bool DataChannelController::HasDataChannels() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return !sctp_data_channels_.empty();
}

bool DataChannelController::SendData(int sid,
                                     const SendDataParams& params,
                                     const rtc::CopyOnWriteBuffer& payload,
                                     cricket::SendDataResult* result) {
  if (data_channel_transport())
    return DataChannelSendData(sid, params, payload, result);
  RTC_LOG(LS_ERROR) << "SendData called before transport is ready";
  return false;
}

bool DataChannelController::ConnectDataChannel(
    SctpDataChannel* webrtc_data_channel) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!data_channel_transport()) {
    // Don't log an error here, because DataChannels are expected to call
    // ConnectDataChannel in this state. It's the only way to initially tell
    // whether or not the underlying transport is ready.
    return false;
  }
  SignalDataChannelTransportWritable_s.connect(
      webrtc_data_channel, &SctpDataChannel::OnTransportReady);
  SignalDataChannelTransportReceivedData_s.connect(
      webrtc_data_channel, &SctpDataChannel::OnDataReceived);
  SignalDataChannelTransportChannelClosing_s.connect(
      webrtc_data_channel, &SctpDataChannel::OnClosingProcedureStartedRemotely);
  SignalDataChannelTransportChannelClosed_s.connect(
      webrtc_data_channel, &SctpDataChannel::OnClosingProcedureComplete);
  return true;
}

void DataChannelController::DisconnectDataChannel(
    SctpDataChannel* webrtc_data_channel) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!data_channel_transport()) {
    RTC_LOG(LS_ERROR)
        << "DisconnectDataChannel called when sctp_transport_ is NULL.";
    return;
  }
  SignalDataChannelTransportWritable_s.disconnect(webrtc_data_channel);
  SignalDataChannelTransportReceivedData_s.disconnect(webrtc_data_channel);
  SignalDataChannelTransportChannelClosing_s.disconnect(webrtc_data_channel);
  SignalDataChannelTransportChannelClosed_s.disconnect(webrtc_data_channel);
}

void DataChannelController::AddSctpDataStream(int sid) {
  if (data_channel_transport()) {
    network_thread()->BlockingCall([this, sid] {
      if (data_channel_transport()) {
        data_channel_transport()->OpenChannel(sid);
      }
    });
  }
}

void DataChannelController::RemoveSctpDataStream(int sid) {
  if (data_channel_transport()) {
    network_thread()->BlockingCall([this, sid] {
      if (data_channel_transport()) {
        data_channel_transport()->CloseChannel(sid);
      }
    });
  }
}

bool DataChannelController::ReadyToSendData() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return (data_channel_transport() && data_channel_transport_ready_to_send_);
}

void DataChannelController::OnChannelStateChanged(
    SctpDataChannel* channel,
    DataChannelInterface::DataState state) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (state == DataChannelInterface::DataState::kClosed)
    OnSctpDataChannelClosed(channel);

  pc_->OnSctpDataChannelStateChanged(channel, state);
}

void DataChannelController::OnDataReceived(
    int channel_id,
    DataMessageType type,
    const rtc::CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(network_thread());
  cricket::ReceiveDataParams params;
  params.sid = channel_id;
  params.type = type;

  if (HandleOpenMessage_n(params, buffer))
    return;

  signaling_thread()->PostTask(
      SafeTask(signaling_safety_.flag(), [this, params, buffer] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        // TODO(bugs.webrtc.org/11547): The data being received should be
        // delivered on the network thread (change
        // SignalDataChannelTransportReceivedData_s to
        // SignalDataChannelTransportReceivedData_n).
        SignalDataChannelTransportReceivedData_s(params, buffer);
      }));
}

void DataChannelController::OnChannelClosing(int channel_id) {
  RTC_DCHECK_RUN_ON(network_thread());
  signaling_thread()->PostTask(
      SafeTask(signaling_safety_.flag(), [this, channel_id] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        SignalDataChannelTransportChannelClosing_s(channel_id);
      }));
}

void DataChannelController::OnChannelClosed(int channel_id) {
  RTC_DCHECK_RUN_ON(network_thread());
  signaling_thread()->PostTask(
      SafeTask(signaling_safety_.flag(), [this, channel_id] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        SignalDataChannelTransportChannelClosed_s(channel_id);
      }));
}

void DataChannelController::OnReadyToSend() {
  RTC_DCHECK_RUN_ON(network_thread());
  signaling_thread()->PostTask(SafeTask(signaling_safety_.flag(), [this] {
    RTC_DCHECK_RUN_ON(signaling_thread());
    data_channel_transport_ready_to_send_ = true;
    SignalDataChannelTransportWritable_s(data_channel_transport_ready_to_send_);
  }));
}

void DataChannelController::OnTransportClosed(RTCError error) {
  RTC_DCHECK_RUN_ON(network_thread());
  signaling_thread()->PostTask(
      SafeTask(signaling_safety_.flag(), [this, error] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        OnTransportChannelClosed(error);
      }));
}

void DataChannelController::SetupDataChannelTransport_n() {
  RTC_DCHECK_RUN_ON(network_thread());

  // There's a new data channel transport.  This needs to be signaled to the
  // `sctp_data_channels_` so that they can reopen and reconnect.  This is
  // necessary when bundling is applied.
  NotifyDataChannelsOfTransportCreated();
}

void DataChannelController::TeardownDataChannelTransport_n() {
  RTC_DCHECK_RUN_ON(network_thread());
  if (data_channel_transport()) {
    data_channel_transport()->SetDataSink(nullptr);
  }
  set_data_channel_transport(nullptr);
}

void DataChannelController::OnTransportChanged(
    DataChannelTransportInterface* new_data_channel_transport) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (data_channel_transport() &&
      data_channel_transport() != new_data_channel_transport) {
    // Changed which data channel transport is used for `sctp_mid_` (eg. now
    // it's bundled).
    data_channel_transport()->SetDataSink(nullptr);
    set_data_channel_transport(new_data_channel_transport);
    if (new_data_channel_transport) {
      new_data_channel_transport->SetDataSink(this);

      // There's a new data channel transport.  This needs to be signaled to the
      // `sctp_data_channels_` so that they can reopen and reconnect.  This is
      // necessary when bundling is applied.
      NotifyDataChannelsOfTransportCreated();
    }
  }
}

std::vector<DataChannelStats> DataChannelController::GetDataChannelStats()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<DataChannelStats> stats;
  stats.reserve(sctp_data_channels_.size());
  for (const auto& channel : sctp_data_channels_)
    stats.push_back(channel->GetStats());
  return stats;
}

bool DataChannelController::HandleOpenMessage_n(
    const cricket::ReceiveDataParams& params,
    const rtc::CopyOnWriteBuffer& buffer) {
  if (params.type != DataMessageType::kControl || !IsOpenMessage(buffer))
    return false;

  // Received OPEN message; parse and signal that a new data channel should
  // be created.
  std::string label;
  InternalDataChannelInit config;
  config.id = params.sid;
  if (!ParseDataChannelOpenMessage(buffer, &label, &config)) {
    RTC_LOG(LS_WARNING) << "Failed to parse the OPEN message for sid "
                        << params.sid;
  } else {
    config.open_handshake_role = InternalDataChannelInit::kAcker;
    signaling_thread()->PostTask(
        SafeTask(signaling_safety_.flag(),
                 [this, label = std::move(label), config = std::move(config)] {
                   RTC_DCHECK_RUN_ON(signaling_thread());
                   OnDataChannelOpenMessage(label, config);
                 }));
  }
  return true;
}

void DataChannelController::OnDataChannelOpenMessage(
    const std::string& label,
    const InternalDataChannelInit& config) {
  rtc::scoped_refptr<DataChannelInterface> channel(
      InternalCreateDataChannelWithProxy(label, &config));
  if (!channel.get()) {
    RTC_LOG(LS_ERROR) << "Failed to create DataChannel from the OPEN message.";
    return;
  }

  pc_->Observer()->OnDataChannel(std::move(channel));
  pc_->NoteDataAddedEvent();
}

rtc::scoped_refptr<DataChannelInterface>
DataChannelController::InternalCreateDataChannelWithProxy(
    const std::string& label,
    const InternalDataChannelInit* config) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (pc_->IsClosed()) {
    return nullptr;
  }

  rtc::scoped_refptr<SctpDataChannel> channel =
      InternalCreateSctpDataChannel(label, config);
  if (channel) {
    return SctpDataChannel::CreateProxy(channel);
  }

  return nullptr;
}

rtc::scoped_refptr<SctpDataChannel>
DataChannelController::InternalCreateSctpDataChannel(
    const std::string& label,
    const InternalDataChannelInit* config) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  InternalDataChannelInit new_config =
      config ? (*config) : InternalDataChannelInit();
  SctpSid sid(new_config.id);
  if (!sid.IsValid()) {
    rtc::SSLRole role;
    if (pc_->GetSctpSslRole(&role) && !sid_allocator_.AllocateSid(role, &sid)) {
      RTC_LOG(LS_ERROR) << "No id can be allocated for the SCTP data channel.";
      return nullptr;
    }
  } else if (!sid_allocator_.ReserveSid(sid)) {
    RTC_LOG(LS_ERROR) << "Failed to create a SCTP data channel "
                         "because the id is already in use or out of range.";
    return nullptr;
  }
  // `sid` has been allocated/validated. Update `new_config` accordingly.
  new_config.id = sid.value();
  rtc::scoped_refptr<SctpDataChannel> channel(
      SctpDataChannel::Create(weak_factory_.GetWeakPtr(), label, new_config,
                              signaling_thread(), network_thread()));
  if (!channel) {
    sid_allocator_.ReleaseSid(sid);
    return nullptr;
  }
  sctp_data_channels_.push_back(channel);
  return channel;
}

void DataChannelController::AllocateSctpSids(rtc::SSLRole role) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<rtc::scoped_refptr<SctpDataChannel>> channels_to_close;
  for (const auto& channel : sctp_data_channels_) {
    if (!channel->sid().IsValid()) {
      SctpSid sid;
      if (!sid_allocator_.AllocateSid(role, &sid)) {
        RTC_LOG(LS_ERROR) << "Failed to allocate SCTP sid, closing channel.";
        channels_to_close.push_back(channel);
        continue;
      }
      // TODO(bugs.webrtc.org/11547): This hides a blocking call to the network
      // thread via AddSctpDataStream. Maybe it's better to move the whole loop
      // to the network thread? Maybe even `sctp_data_channels_`?
      channel->SetSctpSid(sid);
    }
  }
  // Since closing modifies the list of channels, we have to do the actual
  // closing outside the loop.
  for (const auto& channel : channels_to_close) {
    channel->CloseAbruptlyWithDataChannelFailure("Failed to allocate SCTP SID");
  }
}

void DataChannelController::OnSctpDataChannelClosed(SctpDataChannel* channel) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  for (auto it = sctp_data_channels_.begin(); it != sctp_data_channels_.end();
       ++it) {
    if (it->get() == channel) {
      if (channel->sid().IsValid()) {
        // After the closing procedure is done, it's safe to use this ID for
        // another data channel.
        sid_allocator_.ReleaseSid(channel->sid());
      }
      // Since this method is triggered by a signal from the DataChannel,
      // we can't free it directly here; we need to free it asynchronously.
      sctp_data_channels_to_free_.push_back(*it);
      sctp_data_channels_.erase(it);
      signaling_thread()->PostTask(SafeTask(signaling_safety_.flag(), [this] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        sctp_data_channels_to_free_.clear();
      }));
      return;
    }
  }
}

void DataChannelController::OnTransportChannelClosed(RTCError error) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Use a temporary copy of the SCTP DataChannel list because the
  // DataChannel may callback to us and try to modify the list.
  std::vector<rtc::scoped_refptr<SctpDataChannel>> temp_sctp_dcs;
  temp_sctp_dcs.swap(sctp_data_channels_);
  for (const auto& channel : temp_sctp_dcs) {
    channel->OnTransportChannelClosed(error);
  }
}

DataChannelTransportInterface* DataChannelController::data_channel_transport()
    const {
  // TODO(bugs.webrtc.org/11547): Only allow this accessor to be called on the
  // network thread.
  // RTC_DCHECK_RUN_ON(network_thread());
  return data_channel_transport_;
}

void DataChannelController::set_data_channel_transport(
    DataChannelTransportInterface* transport) {
  RTC_DCHECK_RUN_ON(network_thread());
  data_channel_transport_ = transport;
}

bool DataChannelController::DataChannelSendData(
    int sid,
    const SendDataParams& params,
    const rtc::CopyOnWriteBuffer& payload,
    cricket::SendDataResult* result) {
  // TODO(bugs.webrtc.org/11547): Expect method to be called on the network
  // thread instead. Remove the BlockingCall() below and move associated state
  // to the network thread.
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(data_channel_transport());

  RTCError error = network_thread()->BlockingCall([this, sid, params, payload] {
    return data_channel_transport()->SendData(sid, params, payload);
  });

  if (error.ok()) {
    *result = cricket::SendDataResult::SDR_SUCCESS;
    return true;
  } else if (error.type() == RTCErrorType::RESOURCE_EXHAUSTED) {
    // SCTP transport uses RESOURCE_EXHAUSTED when it's blocked.
    // TODO(mellem):  Stop using RTCError here and get rid of the mapping.
    *result = cricket::SendDataResult::SDR_BLOCK;
    return false;
  }
  *result = cricket::SendDataResult::SDR_ERROR;
  return false;
}

void DataChannelController::NotifyDataChannelsOfTransportCreated() {
  RTC_DCHECK_RUN_ON(network_thread());
  signaling_thread()->PostTask(SafeTask(signaling_safety_.flag(), [this] {
    RTC_DCHECK_RUN_ON(signaling_thread());
    for (const auto& channel : sctp_data_channels_) {
      channel->OnTransportChannelCreated();
    }
  }));
}

rtc::Thread* DataChannelController::network_thread() const {
  return pc_->network_thread();
}
rtc::Thread* DataChannelController::signaling_thread() const {
  return pc_->signaling_thread();
}

}  // namespace webrtc
