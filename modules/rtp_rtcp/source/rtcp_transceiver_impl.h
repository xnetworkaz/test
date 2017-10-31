/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_TRANSCEIVER_IMPL_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_TRANSCEIVER_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/array_view.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtcp_transceiver_config.h"
#include "rtc_base/constructormagic.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {
//
// Manage incoming and outgoing rtcp messages for multiple BUNDLED streams.
//
// This class is not thread-safe.
class RtcpTransceiverImpl {
 public:
  explicit RtcpTransceiverImpl(const RtcpTransceiverConfig& config);
  ~RtcpTransceiverImpl();

  // Handles incoming rtcp packet.
  void ReceivePacket(rtc::ArrayView<const uint8_t> packet);

  // Sends RTCP packets starting with a sender or receiver report.
  void SendCompoundPacket();

 private:
  struct LastSenderReport {
    int64_t local_received_time_us;
    uint32_t remote_sent_time_compact_ntp;
  };

  void ReschedulePeriodicCompoundPackets(int64_t delay_ms);
  // Sends RTCP packets.
  void SendPacket();
  std::vector<rtcp::ReportBlock> CreateReportBlocks();

  const RtcpTransceiverConfig config_;

  std::map<uint32_t, LastSenderReport> remote_senders_;
  rtc::WeakPtrFactory<RtcpTransceiverImpl> ptr_factory_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(RtcpTransceiverImpl);
};

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTCP_TRANSCEIVER_IMPL_H_
