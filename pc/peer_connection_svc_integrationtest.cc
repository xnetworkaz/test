/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Integration tests for PeerConnection.
// These tests exercise a full stack for the SVC extension.

#include <stdint.h>

#include <vector>

#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "pc/test/integration_test_helpers.h"
#include "rtc_base/gunit.h"
#include "rtc_base/helpers.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

class PeerConnectionSVCIntegrationTest
    : public PeerConnectionIntegrationBaseTest {
 protected:
  PeerConnectionSVCIntegrationTest()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kUnifiedPlan) {}

  RTCError SetCodecPreferences(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver,
      absl::string_view codec_name) {
    webrtc::RtpCapabilities capabilities =
        caller()->pc_factory()->GetRtpSenderCapabilities(
            cricket::MEDIA_TYPE_VIDEO);
    std::vector<RtpCodecCapability> codecs;
    for (const webrtc::RtpCodecCapability& codec_capability :
         capabilities.codecs) {
      if (codec_capability.name == codec_name)
        codecs.push_back(codec_capability);
    }
    return transceiver->SetCodecPreferences(codecs);
  }
};

TEST_F(PeerConnectionSVCIntegrationTest, AddTransceiverAcceptsL1T1) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  encoding_parameters.scalability_mode = "L1T1";
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  EXPECT_TRUE(transceiver_or_error.ok());
}

TEST_F(PeerConnectionSVCIntegrationTest, AddTransceiverAcceptsL3T3) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  encoding_parameters.scalability_mode = "L3T3";
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  EXPECT_TRUE(transceiver_or_error.ok());
}

TEST_F(PeerConnectionSVCIntegrationTest,
       AddTransceiverRejectsUnknownScalabilityMode) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  encoding_parameters.scalability_mode = "FOOBAR";
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  EXPECT_FALSE(transceiver_or_error.ok());
  EXPECT_EQ(transceiver_or_error.error().type(),
            webrtc::RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionSVCIntegrationTest, SetParametersAcceptsL1T3WithVP8) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpCapabilities capabilities =
      caller()->pc_factory()->GetRtpSenderCapabilities(
          cricket::MEDIA_TYPE_VIDEO);
  std::vector<RtpCodecCapability> vp8_codec;
  for (const webrtc::RtpCodecCapability& codec_capability :
       capabilities.codecs) {
    if (codec_capability.name == cricket::kVp8CodecName)
      vp8_codec.push_back(codec_capability);
  }

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(transceiver->SetCodecPreferences(vp8_codec).ok());

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L1T3";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_TRUE(result.ok());
}

TEST_F(PeerConnectionSVCIntegrationTest, SetParametersRejectsL3T3WithVP8) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(SetCodecPreferences(transceiver, cricket::kVp8CodecName).ok());

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.type(), webrtc::RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionSVCIntegrationTest,
       SetParametersAcceptsL1T3WithVP8AfterNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(SetCodecPreferences(transceiver, cricket::kVp8CodecName).ok());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE_WAIT(SignalingStateStable(), kDefaultTimeout);

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L1T3";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_TRUE(result.ok());
}

TEST_F(PeerConnectionSVCIntegrationTest,
       SetParametersAcceptsL3T3WithVP9AfterNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(SetCodecPreferences(transceiver, cricket::kVp9CodecName).ok());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE_WAIT(SignalingStateStable(), kDefaultTimeout);

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_TRUE(result.ok());
}

TEST_F(PeerConnectionSVCIntegrationTest,
       SetParametersRejectsL3T3WithVP8AfterNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(SetCodecPreferences(transceiver, cricket::kVp8CodecName).ok());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE_WAIT(SignalingStateStable(), kDefaultTimeout);

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "L3T3";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.type(), webrtc::RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(PeerConnectionSVCIntegrationTest,
       SetParametersRejectsInvalidModeWithVP9AfterNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  webrtc::RtpTransceiverInit init;
  webrtc::RtpEncodingParameters encoding_parameters;
  init.send_encodings.push_back(encoding_parameters);
  auto transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack(), init);
  ASSERT_TRUE(transceiver_or_error.ok());
  auto transceiver = transceiver_or_error.MoveValue();
  EXPECT_TRUE(SetCodecPreferences(transceiver, cricket::kVp9CodecName).ok());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE_WAIT(SignalingStateStable(), kDefaultTimeout);

  webrtc::RtpParameters parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  parameters.encodings[0].scalability_mode = "FOOBAR";
  auto result = transceiver->sender()->SetParameters(parameters);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.type(), webrtc::RTCErrorType::UNSUPPORTED_OPERATION);
}

}  // namespace

}  // namespace webrtc
