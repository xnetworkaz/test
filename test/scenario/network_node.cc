/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/scenario/network_node.h"

#include <algorithm>
#include <vector>

namespace webrtc {
namespace test {
namespace {
SimulatedNetwork::Config CreateSimulationConfig(NetworkNodeConfig config) {
  SimulatedNetwork::Config sim_config;
  sim_config.link_capacity_kbps = config.simulation.bandwidth.kbps_or(0);
  sim_config.loss_percent = config.simulation.loss_rate * 100;
  sim_config.queue_delay_ms = config.simulation.delay.ms();
  sim_config.delay_standard_deviation_ms = config.simulation.delay_std_dev.ms();
  return sim_config;
}
}  // namespace

bool NullReceiver::TryDeliverPacket(rtc::CopyOnWriteBuffer packet,
                                    uint64_t receiver,
                                    Timestamp at_time) {
  return true;
}

ActionReceiver::ActionReceiver(std::function<void()> action)
    : action_(action) {}

bool ActionReceiver::TryDeliverPacket(rtc::CopyOnWriteBuffer packet,
                                      uint64_t receiver,
                                      Timestamp at_time) {
  action_();
  return true;
}

NetworkNode::~NetworkNode() = default;

NetworkNode::NetworkNode(NetworkNodeConfig config,
                         std::unique_ptr<NetworkSimulationInterface> simulation)
    : packet_overhead_(config.packet_overhead.bytes()),
      simulation_(std::move(simulation)) {}

void NetworkNode::SetRoute(uint64_t receiver, NetworkReceiverInterface* node) {
  routing_[receiver] = node;
}

void NetworkNode::ClearRoute(uint64_t receiver_id) {
  auto it = routing_.find(receiver_id);
  routing_.erase(it);
}

bool NetworkNode::TryDeliverPacket(rtc::CopyOnWriteBuffer packet,
                                   uint64_t receiver,
                                   Timestamp at_time) {
  if (routing_.find(receiver) == routing_.end())
    return false;
  uint64_t packet_id = next_packet_id_++;
  bool sent = simulation_->EnqueuePacket(PacketInFlightInfo(
      packet.size() + packet_overhead_, at_time.us(), packet_id));
  if (sent) {
    rtc::CritScope crit(&packets_lock_);
    packets_.emplace_back(StoredPacket{packet, receiver, packet_id, false});
  }
  return sent;
}

void NetworkNode::Process(Timestamp at_time) {
  absl::optional<int64_t> delivery_us = simulation_->NextDeliveryTimeUs();
  if (delivery_us && *delivery_us > at_time.us())
    return;

  std::vector<PacketDeliveryInfo> delivery_infos =
      simulation_->DequeueDeliverablePackets(at_time.us());

  rtc::CritScope crit(&packets_lock_);
  for (PacketDeliveryInfo& delivery_info : delivery_infos) {
    StoredPacket* packet = nullptr;
    for (StoredPacket& stored_packet : packets_) {
      if (stored_packet.id == delivery_info.packet_id) {
        packet = &stored_packet;
        break;
      }
    }
    RTC_CHECK(packet);
    RTC_DCHECK(!packet->removed);

    routing_[packet->receiver]->TryDeliverPacket(packet->packet_data,
                                                 packet->receiver, at_time);
    packet->removed = true;
    while (!packets_.empty() && packets_.front().removed) {
      packets_.pop_front();
    }
  }
}

void NetworkNode::Route(int64_t receiver_id,
                        NetworkReceiverInterface* receiver,
                        std::vector<NetworkNode*> nodes) {
  RTC_CHECK(!nodes.empty());
  for (size_t i = 0; i + 1 < nodes.size(); ++i)
    nodes[i]->SetRoute(receiver_id, nodes[i + 1]);
  nodes.back()->SetRoute(receiver_id, receiver);
}

void NetworkNode::ClearRoute(int64_t receiver_id,
                             std::vector<NetworkNode*> nodes) {
  for (NetworkNode* node : nodes)
    node->ClearRoute(receiver_id);
}

std::unique_ptr<SimulationNode> SimulationNode::Create(
    NetworkNodeConfig config) {
  RTC_DCHECK_EQ(config.mode, NetworkNodeConfig::TrafficMode::kSimulation);
  SimulatedNetwork::Config sim_config = CreateSimulationConfig(config);
  auto network = absl::make_unique<SimulatedNetwork>(sim_config);
  SimulatedNetwork* simulation_ptr = network.get();
  return std::unique_ptr<SimulationNode>(
      new SimulationNode(config, std::move(network), simulation_ptr));
}

void SimulationNode::UpdateConfig(
    std::function<void(NetworkNodeConfig*)> modifier) {
  modifier(&config_);
  SimulatedNetwork::Config sim_config = CreateSimulationConfig(config_);
  simulated_network_->SetConfig(sim_config);
}

void SimulationNode::TriggerDelay(Timestamp until) {
  simulated_network_->PauseTransmissionUntil(until.us());
}

ColumnPrinter SimulationNode::ConfigPrinter() const {
  return ColumnPrinter::Lambda("propagation_delay capacity loss_rate",
                               [this](rtc::SimpleStringBuilder& sb) {
                                 sb.AppendFormat(
                                     "%.3lf %.0lf %.2lf",
                                     config_.simulation.delay.seconds<double>(),
                                     config_.simulation.bandwidth.bps() / 8.0,
                                     config_.simulation.loss_rate);
                               });
}

SimulationNode::SimulationNode(
    NetworkNodeConfig config,
    std::unique_ptr<NetworkSimulationInterface> behavior,
    SimulatedNetwork* simulation)
    : NetworkNode(config, std::move(behavior)),
      simulated_network_(simulation),
      config_(config) {}

NetworkNodeTransport::NetworkNodeTransport(CallClient* sender,
                                           NetworkNode* send_net,
                                           uint64_t receiver,
                                           DataSize packet_overhead)
    : sender_(sender),
      send_net_(send_net),
      receiver_id_(receiver),
      packet_overhead_(packet_overhead) {}

NetworkNodeTransport::~NetworkNodeTransport() = default;

bool NetworkNodeTransport::SendRtp(const uint8_t* packet,
                                   size_t length,
                                   const PacketOptions& options) {
  sender_->call_->OnSentPacket(rtc::SentPacket(
      options.packet_id, sender_->clock_->TimeInMilliseconds()));
  Timestamp send_time = Timestamp::ms(sender_->clock_->TimeInMilliseconds());
  rtc::CopyOnWriteBuffer buffer(packet, length,
                                length + packet_overhead_.bytes());
  buffer.SetSize(length + packet_overhead_.bytes());
  return send_net_->TryDeliverPacket(buffer, receiver_id_, send_time);
}

bool NetworkNodeTransport::SendRtcp(const uint8_t* packet, size_t length) {
  rtc::CopyOnWriteBuffer buffer(packet, length);
  Timestamp send_time = Timestamp::ms(sender_->clock_->TimeInMilliseconds());
  buffer.SetSize(length + packet_overhead_.bytes());
  return send_net_->TryDeliverPacket(buffer, receiver_id_, send_time);
}

uint64_t NetworkNodeTransport::ReceiverId() const {
  return receiver_id_;
}

CrossTrafficSource::CrossTrafficSource(NetworkReceiverInterface* target,
                                       uint64_t receiver_id,
                                       CrossTrafficConfig config)
    : target_(target),
      receiver_id_(receiver_id),
      config_(config),
      random_(config.random_seed) {}

DataRate CrossTrafficSource::TrafficRate() const {
  return config_.peak_rate * intensity_;
}

void CrossTrafficSource::Process(Timestamp at_time, TimeDelta delta) {
  time_since_update_ += delta;
  if (config_.mode == CrossTrafficConfig::Mode::kRandomWalk) {
    if (time_since_update_ >= config_.random_walk.update_interval) {
      intensity_ += random_.Gaussian(config_.random_walk.bias,
                                     config_.random_walk.variance) *
                    time_since_update_.seconds<double>();
      intensity_ = std::max(0.0, std::min(1.0, intensity_));
      time_since_update_ = TimeDelta::Zero();
    }
  } else if (config_.mode == CrossTrafficConfig::Mode::kPulsedPeaks) {
    if (intensity_ == 0 && time_since_update_ >= config_.pulsed.hold_duration) {
      intensity_ = 1;
      time_since_update_ = TimeDelta::Zero();
    } else if (intensity_ == 1 &&
               time_since_update_ >= config_.pulsed.send_duration) {
      intensity_ = 0;
      time_since_update_ = TimeDelta::Zero();
    }
  }
  pending_size_ += TrafficRate() * delta;
  if (pending_size_ > config_.min_packet_size) {
    target_->TryDeliverPacket(rtc::CopyOnWriteBuffer(pending_size_.bytes()),
                              receiver_id_, at_time);
    pending_size_ = DataSize::Zero();
  }
}

ColumnPrinter CrossTrafficSource::StatsPrinter() {
  return ColumnPrinter::Lambda("cross_traffic_rate",
                               [this](rtc::SimpleStringBuilder& sb) {
                                 sb.AppendFormat("%.0lf",
                                                 TrafficRate().bps() / 8.0);
                               },
                               32);
}

}  // namespace test
}  // namespace webrtc
