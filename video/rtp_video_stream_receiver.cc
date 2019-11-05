/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/rtp_video_stream_receiver.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/memory/memory.h"
#include "media/base/media_constants.h"
#include "modules/pacing/packet_router.h"
#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtp_cvo.h"
#include "modules/rtp_rtcp/include/rtp_rtcp.h"
#include "modules/rtp_rtcp/include/ulpfec_receiver.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/source/rtp_video_depacketizer_generic.h"
#include "modules/rtp_rtcp/source/rtp_video_depacketizer_h264.h"
#include "modules/rtp_rtcp/source/rtp_video_depacketizer_raw.h"
#include "modules/rtp_rtcp/source/rtp_video_depacketizer_vp8.h"
#include "modules/utility/include/process_thread.h"
#include "modules/video_coding/frame_object.h"
#include "modules/video_coding/nack_module.h"
#include "modules/video_coding/packet_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/location.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/system/fallthrough.h"
#include "system_wrappers/include/field_trial.h"
#include "system_wrappers/include/metrics.h"
#include "video/receive_statistics_proxy.h"

namespace webrtc {

namespace {
// TODO(philipel): Change kPacketBufferStartSize back to 32 in M63 see:
//                 crbug.com/752886
constexpr int kPacketBufferStartSize = 512;
constexpr int kPacketBufferMaxSize = 2048;

int PacketBufferMaxSize() {
  // The group here must be a positive power of 2, in which case that is used as
  // size. All other values shall result in the default value being used.
  const std::string group_name =
      webrtc::field_trial::FindFullName("WebRTC-PacketBufferMaxSize");
  int packet_buffer_max_size = kPacketBufferMaxSize;
  if (!group_name.empty() &&
      (sscanf(group_name.c_str(), "%d", &packet_buffer_max_size) != 1 ||
       packet_buffer_max_size <= 0 ||
       // Verify that the number is a positive power of 2.
       (packet_buffer_max_size & (packet_buffer_max_size - 1)) != 0)) {
    RTC_LOG(LS_WARNING) << "Invalid packet buffer max size: " << group_name;
    packet_buffer_max_size = kPacketBufferMaxSize;
  }
  return packet_buffer_max_size;
}

void UpdateVideoHeader(const RtpPacketReceived& first_packet,
                       const RtpPacketReceived& last_packet,
                       RTPVideoHeader* video_header) {
  RtpGenericFrameDescriptor gfd;
  if (first_packet.GetExtension<RtpGenericFrameDescriptorExtension01>(&gfd) ||
      first_packet.GetExtension<RtpGenericFrameDescriptorExtension00>(&gfd)) {
    video_header->generic.emplace();
    video_header->generic->frame_id = gfd.FrameId();  // may be unwrap.
    video_header->width = gfd.Width();
    // ...
  }

  last_packet.GetExtension<VideoContentTypeExtension>(
      &video_header->content_type);
  last_packet.GetExtension<VideoTimingExtension>(&video_header->video_timing);
  //... all other rtp header extensions common for all payload types.
}

}  // namespace

std::unique_ptr<RtpRtcp> CreateRtpRtcpModule(
    Clock* clock,
    ReceiveStatistics* receive_statistics,
    Transport* outgoing_transport,
    RtcpRttStats* rtt_stats,
    RtcpPacketTypeCounterObserver* rtcp_packet_type_counter_observer,
    uint32_t local_ssrc) {
  RtpRtcp::Configuration configuration;
  configuration.clock = clock;
  configuration.audio = false;
  configuration.receiver_only = true;
  configuration.receive_statistics = receive_statistics;
  configuration.outgoing_transport = outgoing_transport;
  configuration.rtt_stats = rtt_stats;
  configuration.rtcp_packet_type_counter_observer =
      rtcp_packet_type_counter_observer;
  configuration.local_media_ssrc = local_ssrc;

  std::unique_ptr<RtpRtcp> rtp_rtcp = RtpRtcp::Create(configuration);
  rtp_rtcp->SetRTCPStatus(RtcpMode::kCompound);

  return rtp_rtcp;
}

static const int kPacketLogIntervalMs = 10000;

RtpVideoStreamReceiver::RtcpFeedbackBuffer::RtcpFeedbackBuffer(
    KeyFrameRequestSender* key_frame_request_sender,
    NackSender* nack_sender,
    LossNotificationSender* loss_notification_sender)
    : key_frame_request_sender_(key_frame_request_sender),
      nack_sender_(nack_sender),
      loss_notification_sender_(loss_notification_sender),
      request_key_frame_(false) {
  RTC_DCHECK(key_frame_request_sender_);
  RTC_DCHECK(nack_sender_);
  RTC_DCHECK(loss_notification_sender_);
}

void RtpVideoStreamReceiver::RtcpFeedbackBuffer::RequestKeyFrame() {
  rtc::CritScope lock(&cs_);
  request_key_frame_ = true;
}

void RtpVideoStreamReceiver::RtcpFeedbackBuffer::SendNack(
    const std::vector<uint16_t>& sequence_numbers,
    bool buffering_allowed) {
  RTC_DCHECK(!sequence_numbers.empty());
  rtc::CritScope lock(&cs_);
  nack_sequence_numbers_.insert(nack_sequence_numbers_.end(),
                                sequence_numbers.cbegin(),
                                sequence_numbers.cend());
  if (!buffering_allowed) {
    // Note that while *buffering* is not allowed, *batching* is, meaning that
    // previously buffered messages may be sent along with the current message.
    SendBufferedRtcpFeedback();
  }
}

void RtpVideoStreamReceiver::RtcpFeedbackBuffer::SendLossNotification(
    uint16_t last_decoded_seq_num,
    uint16_t last_received_seq_num,
    bool decodability_flag,
    bool buffering_allowed) {
  RTC_DCHECK(buffering_allowed);
  rtc::CritScope lock(&cs_);
  RTC_DCHECK(!lntf_state_)
      << "SendLossNotification() called twice in a row with no call to "
         "SendBufferedRtcpFeedback() in between.";
  lntf_state_ = absl::make_optional<LossNotificationState>(
      last_decoded_seq_num, last_received_seq_num, decodability_flag);
}

void RtpVideoStreamReceiver::RtcpFeedbackBuffer::SendBufferedRtcpFeedback() {
  bool request_key_frame = false;
  std::vector<uint16_t> nack_sequence_numbers;
  absl::optional<LossNotificationState> lntf_state;

  {
    rtc::CritScope lock(&cs_);
    std::swap(request_key_frame, request_key_frame_);
    std::swap(nack_sequence_numbers, nack_sequence_numbers_);
    std::swap(lntf_state, lntf_state_);
  }

  if (lntf_state) {
    // If either a NACK or a key frame request is sent, we should buffer
    // the LNTF and wait for them (NACK or key frame request) to trigger
    // the compound feedback message.
    // Otherwise, the LNTF should be sent out immediately.
    const bool buffering_allowed =
        request_key_frame || !nack_sequence_numbers.empty();

    loss_notification_sender_->SendLossNotification(
        lntf_state->last_decoded_seq_num, lntf_state->last_received_seq_num,
        lntf_state->decodability_flag, buffering_allowed);
  }

  if (request_key_frame) {
    key_frame_request_sender_->RequestKeyFrame();
  } else if (!nack_sequence_numbers.empty()) {
    nack_sender_->SendNack(nack_sequence_numbers, true);
  }
}

RtpVideoStreamReceiver::RtpVideoStreamReceiver(
    Clock* clock,
    Transport* transport,
    RtcpRttStats* rtt_stats,
    PacketRouter* packet_router,
    const VideoReceiveStream::Config* config,
    ReceiveStatistics* rtp_receive_statistics,
    ReceiveStatisticsProxy* receive_stats_proxy,
    ProcessThread* process_thread,
    NackSender* nack_sender,
    KeyFrameRequestSender* keyframe_request_sender,
    video_coding::OnCompleteFrameCallback* complete_frame_callback,
    rtc::scoped_refptr<FrameDecryptorInterface> frame_decryptor)
    : clock_(clock),
      config_(*config),
      packet_router_(packet_router),
      process_thread_(process_thread),
      ntp_estimator_(clock),
      rtp_header_extensions_(config_.rtp.extensions),
      rtp_receive_statistics_(rtp_receive_statistics),
      ulpfec_receiver_(UlpfecReceiver::Create(config->rtp.remote_ssrc,
                                              this,
                                              config->rtp.extensions)),
      receiving_(false),
      last_packet_log_ms_(-1),
      rtp_rtcp_(CreateRtpRtcpModule(clock,
                                    rtp_receive_statistics_,
                                    transport,
                                    rtt_stats,
                                    receive_stats_proxy,
                                    config_.rtp.local_ssrc)),
      complete_frame_callback_(complete_frame_callback),
      keyframe_request_sender_(keyframe_request_sender),
      // TODO(bugs.webrtc.org/10336): Let |rtcp_feedback_buffer_| communicate
      // directly with |rtp_rtcp_|.
      rtcp_feedback_buffer_(this, nack_sender, this),
      packet_buffer_(kPacketBufferStartSize, PacketBufferMaxSize()),
      has_received_frame_(false),
      frames_decryptable_(false) {
  constexpr bool remb_candidate = true;
  if (packet_router_)
    packet_router_->AddReceiveRtpModule(rtp_rtcp_.get(), remb_candidate);

  RTC_DCHECK(config_.rtp.rtcp_mode != RtcpMode::kOff)
      << "A stream should not be configured with RTCP disabled. This value is "
         "reserved for internal usage.";
  // TODO(pbos): What's an appropriate local_ssrc for receive-only streams?
  RTC_DCHECK(config_.rtp.local_ssrc != 0);
  RTC_DCHECK(config_.rtp.remote_ssrc != config_.rtp.local_ssrc);

  rtp_rtcp_->SetRTCPStatus(config_.rtp.rtcp_mode);
  rtp_rtcp_->SetRemoteSSRC(config_.rtp.remote_ssrc);

  static const int kMaxPacketAgeToNack = 450;
  const int max_reordering_threshold = (config_.rtp.nack.rtp_history_ms > 0)
                                           ? kMaxPacketAgeToNack
                                           : kDefaultMaxReorderingThreshold;
  rtp_receive_statistics_->SetMaxReorderingThreshold(config_.rtp.remote_ssrc,
                                                     max_reordering_threshold);
  // TODO(nisse): For historic reasons, we applied the above
  // max_reordering_threshold also for RTX stats, which makes little sense since
  // we don't NACK rtx packets. Consider deleting the below block, and rely on
  // the default threshold.
  if (config_.rtp.rtx_ssrc) {
    rtp_receive_statistics_->SetMaxReorderingThreshold(
        config_.rtp.rtx_ssrc, max_reordering_threshold);
  }
  if (config_.rtp.rtcp_xr.receiver_reference_time_report)
    rtp_rtcp_->SetRtcpXrRrtrStatus(true);

  // Stats callback for CNAME changes.
  rtp_rtcp_->RegisterRtcpCnameCallback(receive_stats_proxy);

  process_thread_->RegisterModule(rtp_rtcp_.get(), RTC_FROM_HERE);

  if (config_.rtp.lntf.enabled) {
    loss_notification_controller_ =
        std::make_unique<LossNotificationController>(&rtcp_feedback_buffer_,
                                                     &rtcp_feedback_buffer_);
  }

  if (config_.rtp.nack.rtp_history_ms != 0) {
    nack_module_ = std::make_unique<NackModule>(clock_, &rtcp_feedback_buffer_,
                                                &rtcp_feedback_buffer_);
    process_thread_->RegisterModule(nack_module_.get(), RTC_FROM_HERE);
  }

  reference_finder_ =
      std::make_unique<video_coding::RtpFrameReferenceFinder>(this);

  // Only construct the encrypted receiver if frame encryption is enabled.
  if (config_.crypto_options.sframe.require_frame_encryption) {
    buffered_frame_decryptor_ =
        std::make_unique<BufferedFrameDecryptor>(this, this);
    if (frame_decryptor != nullptr) {
      buffered_frame_decryptor_->SetFrameDecryptor(std::move(frame_decryptor));
    }
  }
}

RtpVideoStreamReceiver::~RtpVideoStreamReceiver() {
  RTC_DCHECK(secondary_sinks_.empty());

  if (nack_module_) {
    process_thread_->DeRegisterModule(nack_module_.get());
  }

  process_thread_->DeRegisterModule(rtp_rtcp_.get());

  if (packet_router_)
    packet_router_->RemoveReceiveRtpModule(rtp_rtcp_.get());
  UpdateHistograms();
}

void RtpVideoStreamReceiver::AddReceiveCodec(
    const VideoCodec& video_codec,
    const std::map<std::string, std::string>& codec_params,
    bool raw_payload) {
  absl::optional<VideoCodecType> video_type;
  if (!raw_payload) {
    video_type = video_codec.codecType;
  }
  payload_type_map_.emplace(video_codec.plType, video_type);
  pt_codec_params_.emplace(video_codec.plType, codec_params);

  std::unique_ptr<RtpVideoDepacketizer> depacketizer;
  if (raw_payload) {
    depacketizer = std::make_unique<RtpVideoDepacketizerRaw>();
  } else {
    // TODO(danilchap): Prefer to ask video decoder factory to create.
    // To support injectable packetizers and to avoid including unused code.
    switch (video_codec.codecType) {
      case VideoCodecType::kVideoCodecH264: {
        absl::string_view sprop_base64;
        auto sprop_base64_it =
            codec_params.find(cricket::kH264FmtpSpropParameterSets);
        if (sprop_base64_it != codec_params.end()) {
          sprop_base64 = sprop_base64_it->second;
        }
        depacketizer = std::make_unique<H264RtpDepacketizer>(sprop_base64);
      } break;
      case VideoCodecType::kVideoCodecVP8:
        depacketizer = std::make_unique<Vp8RtpDepacketizer>();
        break;
        /*
            case
           VideoCodecType::kVideoCodecVP9: depacketizer =
           std::make_unique<Vp9RtpDepacketizer>(); break;
        */
      case VideoCodecType::kVideoCodecGeneric:
      default:
        depacketizer = std::make_unique<GenericVideoRtpDepacketizer>();
        break;
    }
  }
  RTC_DCHECK(depacketizer);
  auto inserted =
      depacketizers_.emplace(video_codec.plType, std::move(depacketizer));
  RTC_DCHECK(inserted.second);
}

absl::optional<Syncable::Info> RtpVideoStreamReceiver::GetSyncInfo() const {
  Syncable::Info info;
  if (rtp_rtcp_->RemoteNTP(&info.capture_time_ntp_secs,
                           &info.capture_time_ntp_frac, nullptr, nullptr,
                           &info.capture_time_source_clock) != 0) {
    return absl::nullopt;
  }
  {
    rtc::CritScope lock(&sync_info_lock_);
    if (!last_received_rtp_timestamp_ || !last_received_rtp_system_time_ms_) {
      return absl::nullopt;
    }
    info.latest_received_capture_timestamp = *last_received_rtp_timestamp_;
    info.latest_receive_time_ms = *last_received_rtp_system_time_ms_;
  }

  // Leaves info.current_delay_ms uninitialized.
  return info;
}

void RtpVideoStreamReceiver::OnReceivedPayloadData(
    rtc::ArrayView<const uint8_t> codec_payload,
    const RtpPacketReceived& rtp_packet,
    const RTPVideoHeader& video) {}

void RtpVideoStreamReceiver::OnRecoveredPacket(const uint8_t* rtp_packet,
                                               size_t rtp_packet_length) {
  RtpPacketReceived packet;
  if (!packet.Parse(rtp_packet, rtp_packet_length))
    return;
  if (packet.PayloadType() == config_.rtp.red_payload_type) {
    RTC_LOG(LS_WARNING) << "Discarding recovered packet with RED encapsulation";
    return;
  }

  packet.IdentifyExtensions(rtp_header_extensions_);
  packet.set_payload_type_frequency(kVideoPayloadTypeFrequency);
  // TODO(nisse): UlpfecReceiverImpl::ProcessReceivedFec passes both
  // original (decapsulated) media packets and recovered packets to
  // this callback. We need a way to distinguish, for setting
  // packet.recovered() correctly. Ideally, move RED decapsulation out
  // of the Ulpfec implementation.

  ReceivePacket(packet);
}

// This method handles both regular RTP packets and packets recovered
// via FlexFEC.
void RtpVideoStreamReceiver::OnRtpPacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);

  if (!receiving_) {
    return;
  }

  if (!packet.recovered()) {
    // TODO(nisse): Exclude out-of-order packets?
    int64_t now_ms = clock_->TimeInMilliseconds();
    {
      rtc::CritScope cs(&sync_info_lock_);
      last_received_rtp_timestamp_ = packet.Timestamp();
      last_received_rtp_system_time_ms_ = now_ms;
    }
    // Periodically log the RTP header of incoming packets.
    if (now_ms - last_packet_log_ms_ > kPacketLogIntervalMs) {
      rtc::StringBuilder ss;
      ss << "Packet received on SSRC: " << packet.Ssrc()
         << " with payload type: " << static_cast<int>(packet.PayloadType())
         << ", timestamp: " << packet.Timestamp()
         << ", sequence number: " << packet.SequenceNumber()
         << ", arrival time: " << packet.arrival_time_ms();
      int32_t time_offset;
      if (packet.GetExtension<TransmissionOffset>(&time_offset)) {
        ss << ", toffset: " << time_offset;
      }
      uint32_t send_time;
      if (packet.GetExtension<AbsoluteSendTime>(&send_time)) {
        ss << ", abs send time: " << send_time;
      }
      RTC_LOG(LS_INFO) << ss.str();
      last_packet_log_ms_ = now_ms;
    }
  }

  ReceivePacket(packet);

  // Update receive statistics after ReceivePacket.
  // Receive statistics will be reset if the payload type changes (make sure
  // that the first packet is included in the stats).
  if (!packet.recovered()) {
    rtp_receive_statistics_->OnRtpPacket(packet);
  }

  for (RtpPacketSinkInterface* secondary_sink : secondary_sinks_) {
    secondary_sink->OnRtpPacket(packet);
  }
}

void RtpVideoStreamReceiver::RequestKeyFrame() {
  // TODO(bugs.webrtc.org/10336): Allow the sender to ignore key frame requests
  // issued by anything other than the LossNotificationController if it (the
  // sender) is relying on LNTF alone.
  if (keyframe_request_sender_) {
    keyframe_request_sender_->RequestKeyFrame();
  } else {
    rtp_rtcp_->SendPictureLossIndication();
  }
}

void RtpVideoStreamReceiver::SendLossNotification(
    uint16_t last_decoded_seq_num,
    uint16_t last_received_seq_num,
    bool decodability_flag,
    bool buffering_allowed) {
  RTC_DCHECK(config_.rtp.lntf.enabled);
  rtp_rtcp_->SendLossNotification(last_decoded_seq_num, last_received_seq_num,
                                  decodability_flag, buffering_allowed);
}

bool RtpVideoStreamReceiver::IsUlpfecEnabled() const {
  return config_.rtp.ulpfec_payload_type != -1;
}

bool RtpVideoStreamReceiver::IsRetransmissionsEnabled() const {
  return config_.rtp.nack.rtp_history_ms > 0;
}

void RtpVideoStreamReceiver::RequestPacketRetransmit(
    const std::vector<uint16_t>& sequence_numbers) {
  rtp_rtcp_->SendNack(sequence_numbers);
}

bool RtpVideoStreamReceiver::IsDecryptable() const {
  return frames_decryptable_.load();
}

void RtpVideoStreamReceiver::OnInsertedPacket(
    VideoRtpPacketBuffer::InsertResult result) {
  for (VideoRtpPacketBuffer::RtpFrame& packetized_frame : result.frames) {
    RTC_CHECK(!packetized_frame.rtp_packets.empty());
    int64_t min_recv_time = std::numeric_limits<int64_t>::max();
    int64_t max_recv_time = std::numeric_limits<int64_t>::min();
    RtpPacketInfos::vector_type packet_infos;
    std::vector<const RtpPacket*> rtp_packets;
    packet_infos.reserve(packetized_frame.rtp_packets.size());
    rtp_packets.reserve(packetized_frame.rtp_packets.size());
    for (const auto& rtp_packet : packetized_frame.rtp_packets) {
      min_recv_time = std::min(min_recv_time, rtp_packet->arrival_time_ms());
      max_recv_time = std::max(max_recv_time, rtp_packet->arrival_time_ms());
      packet_infos.emplace_back(
          rtp_packet->Ssrc(), rtp_packet->Csrcs(), rtp_packet->Timestamp(),
          /*audio_level=*/absl::nullopt,
          rtp_packet->GetExtension<AbsoluteCaptureTimeExtension>(),
          rtp_packet->arrival_time_ms());
      rtp_packets.push_back(rtp_packet.get());
    }

    const RtpPacketReceived& first_packet =
        *packetized_frame.rtp_packets.front();
    const RtpPacketReceived& last_packet = *packetized_frame.rtp_packets.back();

    int payload_id = first_packet.PayloadType();
    auto depacketize_it = depacketizers_.find(payload_id);
    if (depacketize_it == depacketizers_.end()) {
      continue;
    }
    auto assembled_frame = depacketize_it->second->AssembleFrame(rtp_packets);
    if (!assembled_frame) {
      continue;
    }
    UpdateVideoHeader(first_packet, last_packet,
                      &assembled_frame->video_header);

    absl::optional<RtpGenericFrameDescriptor> generic_descriptor;
    if ((generic_descriptor =
             first_packet
                 .GetExtension<RtpGenericFrameDescriptorExtension00>())) {
      generic_descriptor->SetByteRepresentation(
          first_packet.GetRawExtension<RtpGenericFrameDescriptorExtension00>());
    }

    OnAssembledFrame(std::make_unique<video_coding::RtpFrameObject>(
        first_packet.SequenceNumber(),      //
        last_packet.SequenceNumber(),       //
        last_packet.Marker(),               //
        packetized_frame.max_times_nacked,  //
        min_recv_time,                      //
        max_recv_time,                      //
        first_packet.Timestamp(),           //
        ntp_estimator_.Estimate(first_packet.Timestamp()),
        assembled_frame->video_header.video_timing,  //
        payload_id,                                  //
        assembled_frame->video_header.codec,         //
        assembled_frame->video_header.rotation,      //
        assembled_frame->video_header.content_type,  //
        assembled_frame->video_header,               //
        assembled_frame->video_header.color_space,   //
        generic_descriptor,                          //
        RtpPacketInfos(std::move(packet_infos)),     //
        std::move(assembled_frame->bitstream)));
  }
  if (result.buffer_cleared) {
    RequestKeyFrame();
  }
}

void RtpVideoStreamReceiver::OnAssembledFrame(
    std::unique_ptr<video_coding::RtpFrameObject> frame) {
  RTC_DCHECK_RUN_ON(&network_tc_);
  RTC_DCHECK(frame);

  absl::optional<RtpGenericFrameDescriptor> descriptor =
      frame->GetGenericFrameDescriptor();

  if (loss_notification_controller_ && descriptor) {
    loss_notification_controller_->OnAssembledFrame(
        frame->first_seq_num(), descriptor->FrameId(),
        descriptor->Discardable().value_or(false),
        descriptor->FrameDependenciesDiffs());
  }

  // If frames arrive before a key frame, they would not be decodable.
  // In that case, request a key frame ASAP.
  if (!has_received_frame_) {
    if (frame->FrameType() != VideoFrameType::kVideoFrameKey) {
      // |loss_notification_controller_|, if present, would have already
      // requested a key frame when the first packet for the non-key frame
      // had arrived, so no need to replicate the request.
      if (!loss_notification_controller_) {
        RequestKeyFrame();
      }
    }
    has_received_frame_ = true;
  }

  rtc::CritScope lock(&reference_finder_lock_);
  // Reset |reference_finder_| if |frame| is new and the codec have changed.
  if (current_codec_) {
    bool frame_is_newer =
        AheadOf(frame->Timestamp(), last_assembled_frame_rtp_timestamp_);

    if (frame->codec_type() != current_codec_) {
      if (frame_is_newer) {
        // When we reset the |reference_finder_| we don't want new picture ids
        // to overlap with old picture ids. To ensure that doesn't happen we
        // start from the |last_completed_picture_id_| and add an offset in case
        // of reordering.
        reference_finder_ =
            std::make_unique<video_coding::RtpFrameReferenceFinder>(
                this, last_completed_picture_id_ +
                          std::numeric_limits<uint16_t>::max());
        current_codec_ = frame->codec_type();
      } else {
        // Old frame from before the codec switch, discard it.
        return;
      }
    }

    if (frame_is_newer) {
      last_assembled_frame_rtp_timestamp_ = frame->Timestamp();
    }
  } else {
    current_codec_ = frame->codec_type();
    last_assembled_frame_rtp_timestamp_ = frame->Timestamp();
  }

  if (buffered_frame_decryptor_ == nullptr) {
    reference_finder_->ManageFrame(std::move(frame));
  } else {
    buffered_frame_decryptor_->ManageEncryptedFrame(std::move(frame));
  }
}

void RtpVideoStreamReceiver::OnCompleteFrame(
    std::unique_ptr<video_coding::EncodedFrame> frame) {
  {
    rtc::CritScope lock(&last_seq_num_cs_);
    video_coding::RtpFrameObject* rtp_frame =
        static_cast<video_coding::RtpFrameObject*>(frame.get());
    last_seq_num_for_pic_id_[rtp_frame->id.picture_id] =
        rtp_frame->last_seq_num();
  }
  last_completed_picture_id_ =
      std::max(last_completed_picture_id_, frame->id.picture_id);
  complete_frame_callback_->OnCompleteFrame(std::move(frame));
}

void RtpVideoStreamReceiver::OnDecryptedFrame(
    std::unique_ptr<video_coding::RtpFrameObject> frame) {
  rtc::CritScope lock(&reference_finder_lock_);
  reference_finder_->ManageFrame(std::move(frame));
}

void RtpVideoStreamReceiver::OnDecryptionStatusChange(
    FrameDecryptorInterface::Status status) {
  frames_decryptable_.store(
      (status == FrameDecryptorInterface::Status::kOk) ||
      (status == FrameDecryptorInterface::Status::kRecoverable));
}

void RtpVideoStreamReceiver::SetFrameDecryptor(
    rtc::scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(&network_tc_);
  if (buffered_frame_decryptor_ == nullptr) {
    buffered_frame_decryptor_ =
        std::make_unique<BufferedFrameDecryptor>(this, this);
  }
  buffered_frame_decryptor_->SetFrameDecryptor(std::move(frame_decryptor));
}

void RtpVideoStreamReceiver::UpdateRtt(int64_t max_rtt_ms) {
  if (nack_module_)
    nack_module_->UpdateRtt(max_rtt_ms);
}

absl::optional<int64_t> RtpVideoStreamReceiver::LastReceivedPacketMs() const {
  return packet_buffer_.LastReceivedPacketMs();
}

absl::optional<int64_t> RtpVideoStreamReceiver::LastReceivedKeyframePacketMs()
    const {
  return packet_buffer_.LastReceivedKeyframePacketMs();
}

void RtpVideoStreamReceiver::AddSecondarySink(RtpPacketSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  RTC_DCHECK(!absl::c_linear_search(secondary_sinks_, sink));
  secondary_sinks_.push_back(sink);
}

void RtpVideoStreamReceiver::RemoveSecondarySink(
    const RtpPacketSinkInterface* sink) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  auto it = absl::c_find(secondary_sinks_, sink);
  if (it == secondary_sinks_.end()) {
    // We might be rolling-back a call whose setup failed mid-way. In such a
    // case, it's simpler to remove "everything" rather than remember what
    // has already been added.
    RTC_LOG(LS_WARNING) << "Removal of unknown sink.";
    return;
  }
  secondary_sinks_.erase(it);
}

void RtpVideoStreamReceiver::ReceivePacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  if (packet.payload_size() == 0) {
    // Padding or keep-alive packet.
    // TODO(nisse): Could drop empty packets earlier, but need to figure out how
    // they should be counted in stats.
    NotifyReceiverOfEmptyPacket(packet.SequenceNumber());
  }
  if (packet.PayloadType() == config_.rtp.red_payload_type) {
    ParseAndHandleEncapsulatingHeader(packet);
    return;
  }

  if (loss_notification_controller_) {
    //    loss_notification_controller_->OnReceivedPacket(packet);
  }

  absl::optional<RtpVideoDepacketizer::FrameBoundaries> boundaries =
      GetFrameBoundaries(packet);
  if (!boundaries) {
    // Look like an invalid packet.
    return;
  }

  int times_nacked = nack_module_ == nullptr
                         ? -1
                         : nack_module_->OnReceivedPacket(
                               packet.SequenceNumber(),
                               boundaries->begins_keyframe, packet.recovered());

  rtcp_feedback_buffer_.SendBufferedRtcpFeedback();
  frame_counter_.Add(packet.Timestamp());
  // Copy the packet to make it cheaply movable.
  auto packet_ptr = std::make_unique<RtpPacketReceived>(packet);
  packet_ptr->set_arrival_time_ms(clock_->TimeInMilliseconds());
  OnInsertedPacket(packet_buffer_.InsertPacket(std::move(packet_ptr),
                                               *boundaries, times_nacked));
}

absl::optional<RtpVideoDepacketizer::FrameBoundaries>
RtpVideoStreamReceiver::GetFrameBoundaries(
    const RtpPacketReceived& rtp_packet) {
  RtpVideoDepacketizer::FrameBoundaries boundaries;

  RtpGenericFrameDescriptor gfd;
  if (rtp_packet.GetExtension<RtpGenericFrameDescriptorExtension01>(&gfd) ||
      rtp_packet.GetExtension<RtpGenericFrameDescriptorExtension00>(&gfd)) {
    boundaries.begins_frame = gfd.FirstPacketInSubFrame();
    boundaries.ends_frame = gfd.LastPacketInSubFrame();
    boundaries.begins_keyframe =
        boundaries.begins_frame && gfd.FrameDependenciesDiffs().empty();
    return boundaries;
  }

  if (rtp_packet.payload_size() == 0) {
    boundaries.begins_frame = true;
    boundaries.ends_frame = true;
    boundaries.begins_keyframe = false;
    return boundaries;
  }

  auto depacketize_it = depacketizers_.find(rtp_packet.PayloadType());
  if (depacketize_it == depacketizers_.end()) {
    // Treat unknown payload same as empty payload (padding).
    boundaries.begins_frame = true;
    boundaries.ends_frame = true;
    boundaries.begins_keyframe = false;
    return boundaries;
  }

  return depacketize_it->second->GetFrameBoundaries(rtp_packet.payload());
}

void RtpVideoStreamReceiver::ParseAndHandleEncapsulatingHeader(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  if (packet.PayloadType() == config_.rtp.red_payload_type &&
      packet.payload_size() > 0) {
    if (packet.payload()[0] == config_.rtp.ulpfec_payload_type) {
      // Notify video_receiver about received FEC packets to avoid NACKing these
      // packets.
      NotifyReceiverOfEmptyPacket(packet.SequenceNumber());
    }
    if (!ulpfec_receiver_->AddReceivedRedPacket(
            packet, config_.rtp.ulpfec_payload_type)) {
      return;
    }
    ulpfec_receiver_->ProcessReceivedFec();
  }
}

// In the case of a video stream without picture ids and no rtx the
// RtpFrameReferenceFinder will need to know about padding to
// correctly calculate frame references.
void RtpVideoStreamReceiver::NotifyReceiverOfEmptyPacket(uint16_t seq_num) {
  {
    rtc::CritScope lock(&reference_finder_lock_);
    reference_finder_->PaddingReceived(seq_num);
  }
  if (nack_module_) {
    nack_module_->OnReceivedPacket(seq_num, /* is_keyframe = */ false,
                                   /* is _recovered = */ false);
  }
  if (loss_notification_controller_) {
    // TODO(bugs.webrtc.org/10336): Handle empty packets.
    RTC_LOG(LS_WARNING)
        << "LossNotificationController does not expect empty packets.";
  }
}

bool RtpVideoStreamReceiver::DeliverRtcp(const uint8_t* rtcp_packet,
                                         size_t rtcp_packet_length) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);

  if (!receiving_) {
    return false;
  }

  rtp_rtcp_->IncomingRtcpPacket(rtcp_packet, rtcp_packet_length);

  int64_t rtt = 0;
  rtp_rtcp_->RTT(config_.rtp.remote_ssrc, &rtt, nullptr, nullptr, nullptr);
  if (rtt == 0) {
    // Waiting for valid rtt.
    return true;
  }
  uint32_t ntp_secs = 0;
  uint32_t ntp_frac = 0;
  uint32_t rtp_timestamp = 0;
  uint32_t recieved_ntp_secs = 0;
  uint32_t recieved_ntp_frac = 0;
  if (rtp_rtcp_->RemoteNTP(&ntp_secs, &ntp_frac, &recieved_ntp_secs,
                           &recieved_ntp_frac, &rtp_timestamp) != 0) {
    // Waiting for RTCP.
    return true;
  }
  NtpTime recieved_ntp(recieved_ntp_secs, recieved_ntp_frac);
  int64_t time_since_recieved =
      clock_->CurrentNtpInMilliseconds() - recieved_ntp.ToMs();
  // Don't use old SRs to estimate time.
  if (time_since_recieved <= 1) {
    ntp_estimator_.UpdateRtcpTimestamp(rtt, ntp_secs, ntp_frac, rtp_timestamp);
  }

  return true;
}

void RtpVideoStreamReceiver::FrameContinuous(int64_t picture_id) {
  if (!nack_module_)
    return;

  int seq_num = -1;
  {
    rtc::CritScope lock(&last_seq_num_cs_);
    auto seq_num_it = last_seq_num_for_pic_id_.find(picture_id);
    if (seq_num_it != last_seq_num_for_pic_id_.end())
      seq_num = seq_num_it->second;
  }
  if (seq_num != -1)
    nack_module_->ClearUpTo(seq_num);
}

void RtpVideoStreamReceiver::FrameDecoded(int64_t picture_id) {
  int seq_num = -1;
  {
    rtc::CritScope lock(&last_seq_num_cs_);
    auto seq_num_it = last_seq_num_for_pic_id_.find(picture_id);
    if (seq_num_it != last_seq_num_for_pic_id_.end()) {
      seq_num = seq_num_it->second;
      last_seq_num_for_pic_id_.erase(last_seq_num_for_pic_id_.begin(),
                                     ++seq_num_it);
    }
  }
  if (seq_num != -1) {
    packet_buffer_.ClearTo(seq_num);
    rtc::CritScope lock(&reference_finder_lock_);
    reference_finder_->ClearTo(seq_num);
  }
}

void RtpVideoStreamReceiver::SignalNetworkState(NetworkState state) {
  rtp_rtcp_->SetRTCPStatus(state == kNetworkUp ? config_.rtp.rtcp_mode
                                               : RtcpMode::kOff);
}

void RtpVideoStreamReceiver::StartReceive() {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  receiving_ = true;
}

void RtpVideoStreamReceiver::StopReceive() {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  receiving_ = false;
}

void RtpVideoStreamReceiver::UpdateHistograms() {
  FecPacketCounter counter = ulpfec_receiver_->GetPacketCounter();
  if (counter.first_packet_time_ms == -1)
    return;

  int64_t elapsed_sec =
      (clock_->TimeInMilliseconds() - counter.first_packet_time_ms) / 1000;
  if (elapsed_sec < metrics::kMinRunTimeInSeconds)
    return;

  if (counter.num_packets > 0) {
    RTC_HISTOGRAM_PERCENTAGE(
        "WebRTC.Video.ReceivedFecPacketsInPercent",
        static_cast<int>(counter.num_fec_packets * 100 / counter.num_packets));
  }
  if (counter.num_fec_packets > 0) {
    RTC_HISTOGRAM_PERCENTAGE("WebRTC.Video.RecoveredMediaPacketsInPercentOfFec",
                             static_cast<int>(counter.num_recovered_packets *
                                              100 / counter.num_fec_packets));
  }
  if (config_.rtp.ulpfec_payload_type != -1) {
    RTC_HISTOGRAM_COUNTS_10000(
        "WebRTC.Video.FecBitrateReceivedInKbps",
        static_cast<int>(counter.num_bytes * 8 / elapsed_sec / 1000));
  }
}

}  // namespace webrtc
