/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/scenario/scenario.h"

#include <algorithm>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "rtc_base/flags.h"
#include "test/testsupport/fileutils.h"

DEFINE_bool(scenario_logs, false, "Save logs from sscenario framework.");

namespace webrtc {
namespace test {
namespace {
int64_t kMicrosPerSec = 1000000;
}

RepeatedActivity::RepeatedActivity(TimeDelta interval,
                                   std::function<void(TimeDelta)> function)
    : interval_(interval), function_(function) {}

void RepeatedActivity::Stop() {
  interval_ = TimeDelta::PlusInfinity();
}

void RepeatedActivity::Poll(Timestamp time) {
  RTC_DCHECK(last_update_.IsFinite());
  if (time >= last_update_ + interval_) {
    function_(time - last_update_);
    last_update_ = time;
  }
}

void RepeatedActivity::SetStartTime(Timestamp time) {
  last_update_ = time;
}

Timestamp RepeatedActivity::NextTime() {
  RTC_DCHECK(last_update_.IsFinite());
  return last_update_ + interval_;
}

Scenario::Scenario() : Scenario("", true) {}

Scenario::Scenario(std::string file_name) : Scenario(file_name, true) {}

Scenario::Scenario(std::string file_name, bool real_time)
    : real_time_mode_(real_time),
      sim_clock_(100000 * kMicrosPerSec),
      clock_(real_time ? Clock::GetRealTimeClock() : &sim_clock_),
      audio_decoder_factory_(CreateBuiltinAudioDecoderFactory()),
      audio_encoder_factory_(CreateBuiltinAudioEncoderFactory()) {
  if (FLAG_scenario_logs && !file_name.empty()) {
    CreateDir(OutputPath() + "output_data");
    for (size_t i = 0; i < file_name.size(); ++i) {
      if (file_name[i] == '/')
        CreateDir(OutputPath() + "output_data/" + file_name.substr(0, i));
    }
    base_filename_ = OutputPath() + "output_data/" + file_name;
    RTC_LOG(LS_INFO) << "Saving scenario logs to: " << base_filename_;
  }
  if (!real_time_mode_) {
    rtc::SetClockForTesting(&event_log_fake_clock_);
    event_log_fake_clock_.SetTimeNanos(sim_clock_.TimeInMicroseconds() * 1000);
  }
}

Scenario::~Scenario() {
  if (!real_time_mode_)
    rtc::SetClockForTesting(nullptr);
}

ColumnPrinter Scenario::TimePrinter() {
  return ColumnPrinter::Lambda("time",
                               [this](rtc::SimpleStringBuilder& sb) {
                                 sb.AppendFormat("%.3lf",
                                                 Now().seconds<double>());
                               },
                               32);
}

StatesPrinter* Scenario::CreatePrinter(std::string name,
                                       TimeDelta interval,
                                       std::vector<ColumnPrinter> printers) {
  std::vector<ColumnPrinter> all_printers{TimePrinter()};
  for (auto& printer : printers)
    all_printers.push_back(printer);
  StatesPrinter* printer =
      new StatesPrinter(GetFullPathOrEmpty(name), all_printers);
  printers_.emplace_back(printer);
  printer->PrintHeaders();
  if (interval.IsFinite())
    Every(interval, [printer] { printer->PrintRow(); });
  return printer;
}

CallClient* Scenario::CreateClient(std::string name, CallClientConfig config) {
  CallClient* client = new CallClient(clock_, GetFullPathOrEmpty(name), config);
  if (config.transport.state_log_interval.IsFinite()) {
    Every(config.transport.state_log_interval, [this, client]() {
      client->network_controller_factory_.LogCongestionControllerStats(Now());
    });
  }
  clients_.emplace_back(client);
  return client;
}

CallClient* Scenario::CreateClient(
    std::string name,
    std::function<void(CallClientConfig*)> config_modifier) {
  CallClientConfig config;
  config_modifier(&config);
  return CreateClient(name, config);
}

SimulationNode* Scenario::CreateSimulationNode(
    std::function<void(NetworkNodeConfig*)> config_modifier) {
  NetworkNodeConfig config;
  config_modifier(&config);
  return CreateSimulationNode(config);
}

SimulationNode* Scenario::CreateSimulationNode(NetworkNodeConfig config) {
  auto network_node = SimulationNode::Create(config);
  SimulationNode* sim_node = network_node.get();
  network_nodes_.emplace_back(std::move(network_node));
  Every(config.update_frequency,
        [this, sim_node] { sim_node->Process(Now()); });
  return sim_node;
}

NetworkNode* Scenario::CreateNetworkNode(
    NetworkNodeConfig config,
    std::unique_ptr<NetworkSimulationInterface> simulation) {
  network_nodes_.emplace_back(new NetworkNode(config, std::move(simulation)));
  return network_nodes_.back().get();
}

void Scenario::TriggerBufferBloat(std::vector<NetworkNode*> over_nodes,
                                  size_t num_packets,
                                  size_t packet_size) {
  int64_t receiver_id = next_receiver_id_++;
  NetworkNode::Route(receiver_id, &null_receiver_, over_nodes);
  for (size_t i = 0; i < num_packets; ++i)
    over_nodes[0]->TryDeliverPacket(rtc::CopyOnWriteBuffer(packet_size),
                                    receiver_id, Now());
}

void Scenario::NetworkDelayedAction(std::vector<NetworkNode*> over_nodes,
                                    size_t packet_size,
                                    std::function<void()> action) {
  int64_t receiver_id = next_receiver_id_++;
  action_receivers_.emplace_back(new ActionReceiver(action));
  NetworkNode::Route(receiver_id, action_receivers_.back().get(), over_nodes);
  over_nodes[0]->TryDeliverPacket(rtc::CopyOnWriteBuffer(packet_size),
                                  receiver_id, Now());
}

CrossTrafficSource* Scenario::CreateCrossTraffic(
    std::vector<NetworkNode*> over_nodes,
    std::function<void(CrossTrafficConfig*)> config_modifier) {
  CrossTrafficConfig cross_config;
  config_modifier(&cross_config);
  return CreateCrossTraffic(over_nodes, cross_config);
}

CrossTrafficSource* Scenario::CreateCrossTraffic(
    std::vector<NetworkNode*> over_nodes,
    CrossTrafficConfig config) {
  int64_t receiver_id = next_receiver_id_++;
  cross_traffic_sources_.emplace_back(
      new CrossTrafficSource(over_nodes.front(), receiver_id, config));
  CrossTrafficSource* node = cross_traffic_sources_.back().get();
  NetworkNode::Route(receiver_id, &null_receiver_, over_nodes);
  Every(config.min_packet_interval,
        [this, node](TimeDelta delta) { node->Process(Now(), delta); });
  return node;
}

VideoStreamPair* Scenario::CreateVideoStream(
    CallClient* sender,
    std::vector<NetworkNode*> send_link,
    CallClient* receiver,
    std::vector<NetworkNode*> return_link,
    std::function<void(VideoStreamConfig*)> config_modifier) {
  VideoStreamConfig config;
  config_modifier(&config);
  return CreateVideoStream(sender, send_link, receiver, return_link, config);
}

VideoStreamPair* Scenario::CreateVideoStream(
    CallClient* sender,
    std::vector<NetworkNode*> send_link,
    CallClient* receiver,
    std::vector<NetworkNode*> return_link,
    VideoStreamConfig config) {
  uint64_t send_receiver_id = next_receiver_id_++;
  uint64_t return_receiver_id = next_receiver_id_++;

  video_streams_.emplace_back(
      new VideoStreamPair(sender, send_link, send_receiver_id, receiver,
                          return_link, return_receiver_id, config));
  return video_streams_.back().get();
}

AudioStreamPair* Scenario::CreateAudioStream(
    CallClient* sender,
    std::vector<NetworkNode*> send_link,
    CallClient* receiver,
    std::vector<NetworkNode*> return_link,
    std::function<void(AudioStreamConfig*)> config_modifier) {
  AudioStreamConfig config;
  config_modifier(&config);
  return CreateAudioStream(sender, send_link, receiver, return_link, config);
}

AudioStreamPair* Scenario::CreateAudioStream(
    CallClient* sender,
    std::vector<NetworkNode*> send_link,
    CallClient* receiver,
    std::vector<NetworkNode*> return_link,
    AudioStreamConfig config) {
  uint64_t send_receiver_id = next_receiver_id_++;
  uint64_t return_receiver_id = next_receiver_id_++;

  audio_streams_.emplace_back(new AudioStreamPair(
      sender, send_link, send_receiver_id, audio_encoder_factory_, receiver,
      return_link, return_receiver_id, audio_decoder_factory_, config));
  return audio_streams_.back().get();
}

RepeatedActivity* Scenario::Every(TimeDelta interval,
                                  std::function<void(TimeDelta)> function) {
  repeated_activities_.emplace_back(new RepeatedActivity(interval, function));
  return repeated_activities_.back().get();
}

RepeatedActivity* Scenario::Every(TimeDelta interval,
                                  std::function<void()> function) {
  auto function_with_argument = [function](TimeDelta) { function(); };
  repeated_activities_.emplace_back(
      new RepeatedActivity(interval, function_with_argument));
  return repeated_activities_.back().get();
}

void Scenario::At(TimeDelta offset, std::function<void()> function) {
  pending_activities_.emplace_back(new PendingActivity{offset, function});
}

void Scenario::RunFor(TimeDelta duration) {
  RunUntil(duration, TimeDelta::PlusInfinity(), []() { return false; });
}

void Scenario::RunUntil(TimeDelta max_duration,
                        TimeDelta poll_interval,
                        std::function<bool()> exit_function) {
  start_time_ = Timestamp::us(clock_->TimeInMicroseconds());
  for (auto& activity : repeated_activities_) {
    activity->SetStartTime(start_time_);
  }

  for (auto& stream_pair : video_streams_)
    stream_pair->receive()->receive_stream_->Start();
  for (auto& stream_pair : audio_streams_)
    stream_pair->receive()->receive_stream_->Start();
  for (auto& stream_pair : video_streams_) {
    if (stream_pair->config_.autostart) {
      stream_pair->send()->Start();
    }
  }
  for (auto& stream_pair : audio_streams_) {
    if (stream_pair->config_.autostart) {
      stream_pair->send()->Start();
    }
  }
  for (auto& call : clients_) {
    call->call_->SignalChannelNetworkState(MediaType::AUDIO, kNetworkUp);
    call->call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
  }

  rtc::Event done_(false, false);
  while (!exit_function() && Duration() < max_duration) {
    Timestamp current_time = Now();
    TimeDelta duration = current_time - start_time_;
    Timestamp next_time = current_time + poll_interval;
    for (auto& activity : repeated_activities_) {
      activity->Poll(current_time);
      next_time = std::min(next_time, activity->NextTime());
    }
    for (auto activity = pending_activities_.begin();
         activity < pending_activities_.end(); activity++) {
      if (duration > (*activity)->after_duration) {
        (*activity)->function();
        pending_activities_.erase(activity);
      }
    }
    TimeDelta wait_time = next_time - current_time;
    if (real_time_mode_) {
      done_.Wait(wait_time.ms<int>());
    } else {
      sim_clock_.AdvanceTimeMicroseconds(wait_time.us());
      event_log_fake_clock_.SetTimeNanos(sim_clock_.TimeInMicroseconds() *
                                         1000);
    }
  }
  for (auto& stream_pair : video_streams_) {
    stream_pair->send()->video_capturer_->Stop();
    stream_pair->send()->send_stream_->Stop();
  }
  for (auto& stream_pair : audio_streams_)
    stream_pair->send()->send_stream_->Stop();
  for (auto& stream_pair : video_streams_)
    stream_pair->receive()->receive_stream_->Stop();
  for (auto& stream_pair : audio_streams_)
    stream_pair->receive()->receive_stream_->Stop();
}

Timestamp Scenario::Now() {
  return Timestamp::us(clock_->TimeInMicroseconds());
}

TimeDelta Scenario::Duration() {
  return Now() - start_time_;
}

}  // namespace test
}  // namespace webrtc
