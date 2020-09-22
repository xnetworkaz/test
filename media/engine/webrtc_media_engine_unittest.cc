/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtc_media_engine.h"

#include <memory>
#include <utility>

#include "api/transport/field_trial_based_config.h"
#include "media/engine/webrtc_media_engine_defaults.h"
#include "test/field_trial.h"
#include "test/gtest.h"

using webrtc::RtpExtension;

namespace cricket {
namespace {

std::vector<RtpExtension> MakeUniqueExtensions() {
  std::vector<RtpExtension> result;
  char name[] = "a";
  for (int i = 0; i < 7; ++i) {
    result.push_back(RtpExtension(name, 1 + i));
    name[0]++;
    result.push_back(RtpExtension(name, 255 - i));
    name[0]++;
  }
  return result;
}

std::vector<RtpExtension> MakeRedundantExtensions() {
  std::vector<RtpExtension> result;
  char name[] = "a";
  for (int i = 0; i < 7; ++i) {
    result.push_back(RtpExtension(name, 1 + i));
    result.push_back(RtpExtension(name, 255 - i));
    name[0]++;
  }
  return result;
}

bool SupportedExtensions1(absl::string_view name) {
  return name == "c" || name == "i";
}

bool SupportedExtensions2(absl::string_view name) {
  return name != "a" && name != "n";
}

bool IsSorted(const std::vector<webrtc::RtpExtension>& extensions) {
  const std::string* last = nullptr;
  for (const auto& extension : extensions) {
    if (last && *last > extension.uri) {
      return false;
    }
    last = &extension.uri;
  }
  return true;
}
}  // namespace

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_EmptyList) {
  std::vector<RtpExtension> extensions;
  EXPECT_TRUE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_AllGood) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  EXPECT_TRUE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_OutOfRangeId_Low) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  extensions.push_back(RtpExtension("foo", 0));
  EXPECT_FALSE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_OutOfRangeId_High) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  extensions.push_back(RtpExtension("foo", 256));
  EXPECT_FALSE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_OverlappingIds_StartOfSet) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  extensions.push_back(RtpExtension("foo", 1));
  EXPECT_FALSE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, ValidateRtpExtensions_OverlappingIds_EndOfSet) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  extensions.push_back(RtpExtension("foo", 255));
  EXPECT_FALSE(ValidateRtpExtensions(extensions));
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_EmptyList) {
  std::vector<RtpExtension> extensions;
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions1, true, trials);
  EXPECT_EQ(0u, filtered.size());
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_IncludeOnlySupported) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions1, false, trials);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ("c", filtered[0].uri);
  EXPECT_EQ("i", filtered[1].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_SortedByName_1) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, false, trials);
  EXPECT_EQ(12u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_SortedByName_2) {
  std::vector<RtpExtension> extensions = MakeUniqueExtensions();
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(12u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_DontRemoveRedundant) {
  std::vector<RtpExtension> extensions = MakeRedundantExtensions();
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, false, trials);
  EXPECT_EQ(12u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
  EXPECT_EQ(filtered[0].uri, filtered[1].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundant) {
  std::vector<RtpExtension> extensions = MakeRedundantExtensions();
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(6u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
  EXPECT_NE(filtered[0].uri, filtered[1].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantEncrypted_1) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(webrtc::RtpExtension("b", 1));
  extensions.push_back(webrtc::RtpExtension("b", 2, true));
  extensions.push_back(webrtc::RtpExtension("c", 3));
  extensions.push_back(webrtc::RtpExtension("b", 4));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(3u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
  EXPECT_EQ(filtered[0].uri, filtered[1].uri);
  EXPECT_NE(filtered[0].encrypt, filtered[1].encrypt);
  EXPECT_NE(filtered[0].uri, filtered[2].uri);
  EXPECT_NE(filtered[1].uri, filtered[2].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantEncrypted_2) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(webrtc::RtpExtension("b", 1, true));
  extensions.push_back(webrtc::RtpExtension("b", 2));
  extensions.push_back(webrtc::RtpExtension("c", 3));
  extensions.push_back(webrtc::RtpExtension("b", 4));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(3u, filtered.size());
  EXPECT_TRUE(IsSorted(filtered));
  EXPECT_EQ(filtered[0].uri, filtered[1].uri);
  EXPECT_NE(filtered[0].encrypt, filtered[1].encrypt);
  EXPECT_NE(filtered[0].uri, filtered[2].uri);
  EXPECT_NE(filtered[1].uri, filtered[2].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantBwe_1) {
  webrtc::test::ScopedFieldTrials override_field_trials_(
      "WebRTC-FilterAbsSendTimeExtension/Enabled/");
  webrtc::FieldTrialBasedConfig trials;
  std::vector<RtpExtension> extensions;
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 3));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 9));
  extensions.push_back(RtpExtension(RtpExtension::kAbsSendTimeUri, 6));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 1));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 14));
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[0].uri);
}

TEST(WebRtcMediaEngineTest,
     FilterRtpExtensions_RemoveRedundantBwe_1_KeepAbsSendTime) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 3));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 9));
  extensions.push_back(RtpExtension(RtpExtension::kAbsSendTimeUri, 6));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 1));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 14));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[0].uri);
  EXPECT_EQ(RtpExtension::kAbsSendTimeUri, filtered[1].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantBweEncrypted_1) {
  webrtc::test::ScopedFieldTrials override_field_trials_(
      "WebRTC-FilterAbsSendTimeExtension/Enabled/");
  webrtc::FieldTrialBasedConfig trials;
  std::vector<RtpExtension> extensions;
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 3));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 4, true));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 9));
  extensions.push_back(RtpExtension(RtpExtension::kAbsSendTimeUri, 6));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 1));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 2, true));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 14));
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[0].uri);
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[1].uri);
  EXPECT_NE(filtered[0].encrypt, filtered[1].encrypt);
}

TEST(WebRtcMediaEngineTest,
     FilterRtpExtensions_RemoveRedundantBweEncrypted_1_KeepAbsSendTime) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 3));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 4, true));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 9));
  extensions.push_back(RtpExtension(RtpExtension::kAbsSendTimeUri, 6));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 1));
  extensions.push_back(
      RtpExtension(RtpExtension::kTransportSequenceNumberUri, 2, true));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 14));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(3u, filtered.size());
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[0].uri);
  EXPECT_EQ(RtpExtension::kTransportSequenceNumberUri, filtered[1].uri);
  EXPECT_EQ(RtpExtension::kAbsSendTimeUri, filtered[2].uri);
  EXPECT_NE(filtered[0].encrypt, filtered[1].encrypt);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantBwe_2) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 1));
  extensions.push_back(RtpExtension(RtpExtension::kAbsSendTimeUri, 14));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 7));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(RtpExtension::kAbsSendTimeUri, filtered[0].uri);
}

TEST(WebRtcMediaEngineTest, FilterRtpExtensions_RemoveRedundantBwe_3) {
  std::vector<RtpExtension> extensions;
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 2));
  extensions.push_back(RtpExtension(RtpExtension::kTimestampOffsetUri, 14));
  webrtc::FieldTrialBasedConfig trials;
  std::vector<webrtc::RtpExtension> filtered =
      FilterRtpExtensions(extensions, SupportedExtensions2, true, trials);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(RtpExtension::kTimestampOffsetUri, filtered[0].uri);
}

TEST(WebRtcMediaEngineTest, Create) {
  MediaEngineDependencies deps;
  webrtc::SetMediaEngineDefaults(&deps);
  webrtc::FieldTrialBasedConfig trials;
  deps.trials = &trials;

  std::unique_ptr<MediaEngineInterface> engine =
      CreateMediaEngine(std::move(deps));

  EXPECT_TRUE(engine);
}

}  // namespace cricket
