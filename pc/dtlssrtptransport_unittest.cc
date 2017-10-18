/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/dtlssrtptransport.h"

#include <memory>
#include <utility>

#include "media/base/fakertp.h"
#include "p2p/base/dtlstransportinternal.h"
#include "p2p/base/fakedtlstransport.h"
#include "p2p/base/fakepackettransport.h"
#include "p2p/base/p2pconstants.h"
#include "pc/rtptransport.h"
#include "pc/rtptransporttestutil.h"
#include "rtc_base/asyncpacketsocket.h"
#include "rtc_base/gunit.h"
#include "rtc_base/ptr_util.h"
#include "rtc_base/sslstreamadapter.h"

using cricket::FakeDtlsTransport;
using cricket::FakeIceTransport;
using webrtc::DtlsSrtpTransport;
using webrtc::SrtpTransport;
using webrtc::RtpTransport;

const int kRtpAuthTagLen = 10;

class TransportObserver : public sigslot::has_slots<> {
 public:
  void OnPacketReceived(bool rtcp,
                        rtc::CopyOnWriteBuffer* packet,
                        const rtc::PacketTime& packet_time) {
    rtcp ? last_recv_rtcp_packet_ = *packet : last_recv_rtp_packet_ = *packet;
  }

  void OnReadyToSend(bool ready) {
    LOG(INFO) << "Got signal";
    ready_to_send_ = ready;
  }

  rtc::CopyOnWriteBuffer last_recv_rtp_packet() {
    return last_recv_rtp_packet_;
  }

  rtc::CopyOnWriteBuffer last_recv_rtcp_packet() {
    return last_recv_rtcp_packet_;
  }

  bool ready_to_send() { return ready_to_send_; }

 private:
  rtc::CopyOnWriteBuffer last_recv_rtp_packet_;
  rtc::CopyOnWriteBuffer last_recv_rtcp_packet_;
  bool ready_to_send_ = false;
};

class DtlsSrtpTransportTest : public testing::Test,
                              public sigslot::has_slots<> {
 protected:
  DtlsSrtpTransportTest() {}

  std::unique_ptr<DtlsSrtpTransport> MakeDtlsSrtpTransport(
      rtc::PacketTransportInternal* rtp_packet_transport,
      rtc::PacketTransportInternal* rtcp_packet_transport,
      bool rtcp_mux_enabled) {
    auto rtp_transport = rtc::MakeUnique<RtpTransport>(rtcp_mux_enabled);

    rtp_transport->SetRtpPacketTransport(rtp_packet_transport);
    rtp_transport->SetRtcpPacketTransport(rtcp_packet_transport);
    rtp_transport->AddHandledPayloadType(0x00);
    rtp_transport->AddHandledPayloadType(0xc9);

    auto srtp_transport =
        rtc::MakeUnique<SrtpTransport>(std::move(rtp_transport), "content");
    auto dtls_srtp_transport =
        rtc::MakeUnique<DtlsSrtpTransport>(std::move(srtp_transport));

    return dtls_srtp_transport;
  }

  void MakeDtlsSrtpTransports(FakeDtlsTransport* rtp1_dtls,
                              FakeDtlsTransport* rtcp1_dtls,
                              FakeDtlsTransport* rtp2_dtls,
                              FakeDtlsTransport* rtcp2_dtls,
                              bool rtcp_mux_enabled) {
    dtls_srtp_transport1_ =
        MakeDtlsSrtpTransport(rtp1_dtls, rtcp1_dtls, rtcp_mux_enabled);
    dtls_srtp_transport2_ =
        MakeDtlsSrtpTransport(rtp2_dtls, rtcp2_dtls, rtcp_mux_enabled);

    dtls_srtp_transport1_->SignalPacketReceived.connect(
        &transport_observer1_, &TransportObserver::OnPacketReceived);
    dtls_srtp_transport1_->SignalReadyToSend.connect(
        &transport_observer1_, &TransportObserver::OnReadyToSend);

    dtls_srtp_transport2_->SignalPacketReceived.connect(
        &transport_observer2_, &TransportObserver::OnPacketReceived);
    dtls_srtp_transport2_->SignalReadyToSend.connect(
        &transport_observer2_, &TransportObserver::OnReadyToSend);
  }

  void CompleteDtlsHandshake(FakeDtlsTransport* fake_dtls1,
                             FakeDtlsTransport* fake_dtls2) {
    auto cert1 = rtc::RTCCertificate::Create(std::unique_ptr<rtc::SSLIdentity>(
        rtc::SSLIdentity::Generate("session1", rtc::KT_DEFAULT)));
    fake_dtls1->SetLocalCertificate(cert1);
    auto cert2 = rtc::RTCCertificate::Create(std::unique_ptr<rtc::SSLIdentity>(
        rtc::SSLIdentity::Generate("session1", rtc::KT_DEFAULT)));
    fake_dtls2->SetLocalCertificate(cert2);
    fake_dtls1->SetDestination(fake_dtls2);
  }

  void SendRecvRtpPackets() {
    ASSERT_TRUE(dtls_srtp_transport1_);
    ASSERT_TRUE(dtls_srtp_transport2_);
    ASSERT_TRUE(dtls_srtp_transport1_->IsActive());
    ASSERT_TRUE(dtls_srtp_transport2_->IsActive());

    size_t rtp_len = sizeof(kPcmuFrame);
    size_t packet_size = rtp_len + kRtpAuthTagLen;
    rtc::Buffer rtp_packet_buffer(packet_size);
    char* rtp_packet_data = rtp_packet_buffer.data<char>();
    memcpy(rtp_packet_data, kPcmuFrame, rtp_len);
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    rtc::SetBE16(reinterpret_cast<uint8_t*>(rtp_packet_data) + 2,
                 ++sequence_number_);
    rtc::CopyOnWriteBuffer rtp_packet1to2(rtp_packet_data, rtp_len,
                                          packet_size);
    rtc::CopyOnWriteBuffer rtp_packet2to1(rtp_packet_data, rtp_len,
                                          packet_size);

    rtc::PacketOptions options;
    // Send a packet from |srtp_transport1_| to |srtp_transport2_| and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                     cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtp_packet().data(),
                        kPcmuFrame, rtp_len));
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                     cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtp_packet().data(),
                        kPcmuFrame, rtp_len));
  }

  void SendRecvRtcpPackets() {
    size_t rtcp_len = sizeof(kRtcpReport);
    size_t packet_size = rtcp_len + 4 + kRtpAuthTagLen;
    rtc::Buffer rtcp_packet_buffer(packet_size);

    rtc::CopyOnWriteBuffer rtcp_packet1to2(kRtcpReport, rtcp_len, packet_size);
    rtc::CopyOnWriteBuffer rtcp_packet2to1(kRtcpReport, rtcp_len, packet_size);

    rtc::PacketOptions options;
    // Send a packet from |srtp_transport1_| to |srtp_transport2_| and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtcpPacket(&rtcp_packet1to2, options,
                                                      cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtcp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtcp_packet().data(),
                        kRtcpReport, rtcp_len));

    // Do the same thing in the opposite direction;
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtcpPacket(&rtcp_packet2to1, options,
                                                      cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtcp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtcp_packet().data(),
                        kRtcpReport, rtcp_len));
  }

  void SendRecvRtpPacketsWithHeaderExtension(
      const std::vector<int>& encrypted_header_ids) {
    ASSERT_TRUE(dtls_srtp_transport1_);
    ASSERT_TRUE(dtls_srtp_transport2_);
    ASSERT_TRUE(dtls_srtp_transport1_->IsActive());
    ASSERT_TRUE(dtls_srtp_transport2_->IsActive());

    size_t rtp_len = sizeof(kPcmuFrameWithExtensions);
    size_t packet_size = rtp_len + kRtpAuthTagLen;
    rtc::Buffer rtp_packet_buffer(packet_size);
    char* rtp_packet_data = rtp_packet_buffer.data<char>();
    memcpy(rtp_packet_data, kPcmuFrameWithExtensions, rtp_len);
    // In order to be able to run this test function multiple times we can not
    // use the same sequence number twice. Increase the sequence number by one.
    rtc::SetBE16(reinterpret_cast<uint8_t*>(rtp_packet_data) + 2,
                 ++sequence_number_);
    rtc::CopyOnWriteBuffer rtp_packet1to2(rtp_packet_data, rtp_len,
                                          packet_size);
    rtc::CopyOnWriteBuffer rtp_packet2to1(rtp_packet_data, rtp_len,
                                          packet_size);

    char original_rtp_data[sizeof(kPcmuFrameWithExtensions)];
    memcpy(original_rtp_data, rtp_packet_data, rtp_len);

    rtc::PacketOptions options;
    // Send a packet from |srtp_transport1_| to |srtp_transport2_| and verify
    // that the packet can be successfully received and decrypted.
    ASSERT_TRUE(dtls_srtp_transport1_->SendRtpPacket(&rtp_packet1to2, options,
                                                     cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer2_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer2_.last_recv_rtp_packet().data(),
                        original_rtp_data, rtp_len));
    // Get the encrypted packet from underneath packet transport and verify the
    // data and header extension are actually encrypted.
    auto fake_ice_transport = static_cast<FakeIceTransport*>(
        dtls_srtp_transport1_->rtp_dtls_transport()->ice_transport());
    EXPECT_NE(0, memcmp(fake_ice_transport->last_sent_packet().data(),
                        original_rtp_data, rtp_len));
    CompareHeaderExtensions(reinterpret_cast<const char*>(
                                fake_ice_transport->last_sent_packet().data()),
                            fake_ice_transport->last_sent_packet().size(),
                            original_rtp_data, rtp_len, encrypted_header_ids,
                            false);

    // Do the same thing in the opposite direction.
    ASSERT_TRUE(dtls_srtp_transport2_->SendRtpPacket(&rtp_packet2to1, options,
                                                     cricket::PF_SRTP_BYPASS));
    ASSERT_TRUE(transport_observer1_.last_recv_rtp_packet().data());
    EXPECT_EQ(0, memcmp(transport_observer1_.last_recv_rtp_packet().data(),
                        original_rtp_data, rtp_len));
    // Get the encrypted packet from underneath packet transport and verify the
    // data and header extension are actually encrypted.
    fake_ice_transport = static_cast<FakeIceTransport*>(
        dtls_srtp_transport1_->rtp_dtls_transport()->ice_transport());
    EXPECT_NE(0, memcmp(fake_ice_transport->last_sent_packet().data(),
                        original_rtp_data, rtp_len));
    CompareHeaderExtensions(reinterpret_cast<const char*>(
                                fake_ice_transport->last_sent_packet().data()),
                            fake_ice_transport->last_sent_packet().size(),
                            original_rtp_data, rtp_len, encrypted_header_ids,
                            false);
  }

  void SendRecvPackets() {
    SendRecvRtpPackets();
    SendRecvRtcpPackets();
  }

  std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport1_;
  std::unique_ptr<DtlsSrtpTransport> dtls_srtp_transport2_;
  TransportObserver transport_observer1_;
  TransportObserver transport_observer2_;

  int sequence_number_ = 0;
};

TEST_F(DtlsSrtpTransportTest, SetTransportsAfterHandshakeCompleteWithRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  bool rtcp_mux_enabled = true;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         rtcp_mux_enabled);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), nullptr);

  dtls_srtp_transport1_->SetRtcpMuxEnabled(true);
  dtls_srtp_transport2_->SetRtcpMuxEnabled(true);

  SendRecvPackets();
}

TEST_F(DtlsSrtpTransportTest,
       SetTransportsAfterHandshakeCompleteWithoutRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTCP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTCP);

  bool rtcp_mux_enabled = false;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), rtcp_mux_enabled);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  CompleteDtlsHandshake(rtcp_dtls1.get(), rtcp_dtls2.get());

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), rtcp_dtls1.get());
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), rtcp_dtls2.get());

  rtp_dtls1->SetWritable(true);
  rtcp_dtls1->SetWritable(true);
  rtp_dtls2->SetWritable(true);
  rtcp_dtls2->SetWritable(true);
  SendRecvPackets();
}

TEST_F(DtlsSrtpTransportTest, SetTransportsBeforeHandshakeCompleteWithRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  bool rtcp_mux_enabled = true;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         rtcp_mux_enabled);

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), nullptr);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  SendRecvPackets();
}

TEST_F(DtlsSrtpTransportTest,
       SetTransportsBeforeHandshakeCompleteWithoutRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTCP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTCP);

  bool rtcp_mux_enabled = false;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), rtcp_mux_enabled);

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), rtcp_dtls1.get());
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), rtcp_dtls2.get());

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  CompleteDtlsHandshake(rtcp_dtls1.get(), rtcp_dtls2.get());
  SendRecvPackets();
}

// Tests that if the DtlsTransport underneath is changed, the previous DTLS-SRTP
// context will be reset and will be re-setup once the new transports' handshake
// complete.
TEST_F(DtlsSrtpTransportTest, DtlsSrtpResetAfterDtlsTransportChange) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  bool rtcp_mux_enabled = true;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         rtcp_mux_enabled);
  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), nullptr);

  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  EXPECT_TRUE(dtls_srtp_transport1_->IsActive());
  EXPECT_TRUE(dtls_srtp_transport2_->IsActive());

  auto rtp_dtls3 = rtc::MakeUnique<FakeDtlsTransport>(
      "t3", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls4 = rtc::MakeUnique<FakeDtlsTransport>(
      "t4", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls3.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls4.get(), nullptr);
  EXPECT_FALSE(dtls_srtp_transport1_->IsActive());
  EXPECT_FALSE(dtls_srtp_transport2_->IsActive());

  CompleteDtlsHandshake(rtp_dtls3.get(), rtp_dtls4.get());
  SendRecvPackets();
}

// Tests that if only the RTP DTLS handshake complete, and then RTCP muxing is
// enabled, SRTP is set up.
TEST_F(DtlsSrtpTransportTest,
       RtcpMuxEnabledAfterRtpTransportHandshakeComplete) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTCP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTCP);

  bool rtcp_mux_enabled = false;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), rtcp_mux_enabled);

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), rtcp_dtls1.get());
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), rtcp_dtls2.get());
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());
  // Inactive because the RTCP transport handshake didn't complete.
  EXPECT_FALSE(dtls_srtp_transport1_->IsActive());
  EXPECT_FALSE(dtls_srtp_transport2_->IsActive());

  dtls_srtp_transport1_->SetRtcpMuxEnabled(true);
  dtls_srtp_transport2_->SetRtcpMuxEnabled(true);
  // The transports should be active and be able to send packets when the
  // RtcpMux is enabled.
  SendRecvPackets();
}

TEST_F(DtlsSrtpTransportTest, EncryptedHeaderExtensionIdUpdated) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  bool rtcp_mux_enabled = true;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         rtcp_mux_enabled);
  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), nullptr);
  CompleteDtlsHandshake(rtp_dtls1.get(), rtp_dtls2.get());

  std::vector<int> encrypted_headers;
  encrypted_headers.push_back(1);
  encrypted_headers.push_back(4);

  dtls_srtp_transport1_->SetSendEncryptedHeaderExtensionIds(encrypted_headers);
  dtls_srtp_transport1_->SetRecvEncryptedHeaderExtensionIds(encrypted_headers);
  dtls_srtp_transport2_->SetSendEncryptedHeaderExtensionIds(encrypted_headers);
  dtls_srtp_transport2_->SetRecvEncryptedHeaderExtensionIds(encrypted_headers);
  SendRecvRtpPacketsWithHeaderExtension(encrypted_headers);
}

TEST_F(DtlsSrtpTransportTest, SignalReadyToSendFiredWithRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);

  bool rtcp_mux_enabled = true;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), nullptr, rtp_dtls2.get(), nullptr,
                         rtcp_mux_enabled);
  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), nullptr);
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), nullptr);

  rtp_dtls1->SetDestination(rtp_dtls2.get());
  EXPECT_TRUE(transport_observer1_.ready_to_send());
  EXPECT_TRUE(transport_observer2_.ready_to_send());
}

TEST_F(DtlsSrtpTransportTest, SignalReadyToSendFiredWithoutRtcpMux) {
  auto rtp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls1 = rtc::MakeUnique<FakeDtlsTransport>(
      "t1", cricket::ICE_CANDIDATE_COMPONENT_RTCP);
  auto rtp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTP);
  auto rtcp_dtls2 = rtc::MakeUnique<FakeDtlsTransport>(
      "t2", cricket::ICE_CANDIDATE_COMPONENT_RTCP);

  bool rtcp_mux_enabled = false;
  MakeDtlsSrtpTransports(rtp_dtls1.get(), rtcp_dtls1.get(), rtp_dtls2.get(),
                         rtcp_dtls2.get(), rtcp_mux_enabled);

  dtls_srtp_transport1_->SetDtlsTransports(rtp_dtls1.get(), rtcp_dtls1.get());
  dtls_srtp_transport2_->SetDtlsTransports(rtp_dtls2.get(), rtcp_dtls2.get());

  rtp_dtls1->SetDestination(rtp_dtls2.get());
  EXPECT_FALSE(transport_observer1_.ready_to_send());
  EXPECT_FALSE(transport_observer2_.ready_to_send());

  rtcp_dtls1->SetDestination(rtcp_dtls2.get());
  EXPECT_TRUE(transport_observer1_.ready_to_send());
  EXPECT_TRUE(transport_observer2_.ready_to_send());
}
