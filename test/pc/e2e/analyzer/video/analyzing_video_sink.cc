/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/pc/e2e/analyzer/video/analyzing_video_sink.h"

#include <memory>
#include <set>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/test/peerconnection_quality_test_fixture.h"
#include "api/test/video/video_frame_writer.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/synchronization/mutex.h"
#include "test/pc/e2e/analyzer/video/simulcast_dummy_buffer_helper.h"
#include "test/pc/e2e/analyzer/video/video_dumping.h"
#include "test/testsupport/fixed_fps_video_frame_writer_adapter.h"
#include "test/video_renderer.h"

namespace webrtc {
namespace webrtc_pc_e2e {

using VideoSubscription = ::webrtc::webrtc_pc_e2e::
    PeerConnectionE2EQualityTestFixture::VideoSubscription;
using VideoResolution = ::webrtc::webrtc_pc_e2e::
    PeerConnectionE2EQualityTestFixture::VideoResolution;
using VideoConfig =
    ::webrtc::webrtc_pc_e2e::PeerConnectionE2EQualityTestFixture::VideoConfig;

AnalyzingVideoSink::AnalyzingVideoSink(absl::string_view peer_name,
                                       Clock* clock,
                                       VideoQualityAnalyzerInterface& analyzer,
                                       AnalyzingVideoSinksHelper& sinks_helper,
                                       const VideoSubscription& subscription)
    : peer_name_(peer_name),
      clock_(clock),
      analyzer_(&analyzer),
      sinks_helper_(&sinks_helper),
      subscription_(subscription) {}

void AnalyzingVideoSink::UpdateSubscription(
    const VideoSubscription& subscription) {
  // For peers with changed resolutions we need to close current writers and
  // open new ones. This is done by removing existing sinks, which will force
  // creation of the new sinks when next frame will be received.
  std::set<test::VideoFrameWriter*> writers_to_close;
  {
    MutexLock lock(&mutex_);
    subscription_ = subscription;
    for (auto it = stream_sinks_.cbegin(); it != stream_sinks_.cend();) {
      absl::optional<VideoResolution> new_requested_resolution =
          subscription_.GetResolutionForPeer(it->second.sender_peer_name);
      if (!new_requested_resolution.has_value() ||
          (*new_requested_resolution != it->second.resolution)) {
        writers_to_close.insert(it->second.video_frame_writer);
        it = stream_sinks_.erase(it);
      } else {
        ++it;
      }
    }
  }
  sinks_helper_->CloseAndRemoveVideoWriters(writers_to_close);
}

void AnalyzingVideoSink::OnFrame(const VideoFrame& frame) {
  if (IsDummyFrame(frame)) {
    // This is dummy frame, so we  don't need to process it further.
    return;
  }
  // Copy entire video frame including video buffer to ensure that analyzer
  // won't hold any WebRTC internal buffers.
  VideoFrame frame_copy = frame;
  frame_copy.set_video_frame_buffer(
      I420Buffer::Copy(*frame.video_frame_buffer()->ToI420()));
  analyzer_->OnFrameRendered(peer_name_, frame_copy);

  if (frame.id() != VideoFrame::kNotSetId) {
    std::string stream_label = analyzer_->GetStreamLabel(frame.id());
    SinksDescriptor* sinks_descriptor = PopulateSinks(stream_label);
    if (sinks_descriptor == nullptr) {
      return;
    }
    for (auto& sink : sinks_descriptor->sinks) {
      sink->OnFrame(frame);
    }
  }
}

AnalyzingVideoSink::SinksDescriptor* AnalyzingVideoSink::PopulateSinks(
    absl::string_view stream_label) {
  // Fast pass: sinks already exists.
  MutexLock lock(&mutex_);
  auto sinks_it = stream_sinks_.find(std::string(stream_label));
  if (sinks_it != stream_sinks_.end()) {
    return &sinks_it->second;
  }

  // Slow pass: we need to create and save sinks
  absl::optional<std::pair<std::string, VideoConfig>> peer_and_config =
      sinks_helper_->GetPeerAndConfig(stream_label);
  RTC_CHECK(peer_and_config.has_value())
      << "No video config for stream " << stream_label;
  const std::string& sender_peer_name = peer_and_config->first;
  const VideoConfig& config = peer_and_config->second;

  absl::optional<VideoResolution> resolution =
      subscription_.GetResolutionForPeer(sender_peer_name);
  if (!resolution.has_value()) {
    RTC_LOG(LS_ERROR) << peer_name_ << " received stream " << stream_label
                      << " from " << sender_peer_name
                      << " for which they were not subscribed";
    resolution = config.GetResolution();
  }

  RTC_CHECK(resolution.has_value());

  SinksDescriptor sinks_descriptor(sender_peer_name, *resolution);
  if (config.output_dump_options.has_value()) {
    std::unique_ptr<test::VideoFrameWriter> writer =
        config.output_dump_options->CreateOutputDumpVideoFrameWriter(
            stream_label, peer_name_, *resolution);
    if (config.output_dump_use_fixed_framerate) {
      writer = std::make_unique<test::FixedFpsVideoFrameWriterAdapter>(
          resolution->fps(), clock_, std::move(writer));
    }
    sinks_descriptor.sinks.push_back(std::make_unique<VideoWriter>(
        writer.get(), config.output_dump_options->sampling_modulo()));
    sinks_descriptor.video_frame_writer =
        sinks_helper_->AddVideoWriter(std::move(writer));
  }
  if (config.show_on_screen) {
    sinks_descriptor.sinks.push_back(
        absl::WrapUnique(test::VideoRenderer::Create(
            (*config.stream_label + "-render").c_str(), resolution->width(),
            resolution->height())));
  }
  return &stream_sinks_.emplace(stream_label, std::move(sinks_descriptor))
              .first->second;
}

}  // namespace webrtc_pc_e2e
}  // namespace webrtc
