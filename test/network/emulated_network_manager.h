/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_NETWORK_EMULATED_NETWORK_MANAGER_H_
#define TEST_NETWORK_EMULATED_NETWORK_MANAGER_H_

#include <memory>
#include <vector>

#include "api/test/network_emulation_manager.h"
#include "api/test/time_controller.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/network.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_checker.h"
#include "test/network/network_emulation.h"

namespace webrtc {
namespace test {

// Framework assumes that rtc::NetworkManager is called from network thread.
class EmulatedNetworkManager : public rtc::NetworkManagerBase,
                               public sigslot::has_slots<>,
                               public EmulatedNetworkManagerInterface {
 public:
  EmulatedNetworkManager(TimeController* time_controller,
                         TaskQueueForTest* task_queue,
                         EndpointsContainer* endpoints_container);

  void EnableEndpoint(EmulatedEndpointImpl* endpoint);
  void DisableEndpoint(EmulatedEndpointImpl* endpoint);

  // NetworkManager interface. All these methods are supposed to be called from
  // the same thread.
  void StartUpdating() override;
  void StopUpdating() override;

  // We don't support any address interfaces in the network emulation framework.
  void GetAnyAddressNetworks(NetworkList* networks) override {}

  // EmulatedNetworkManagerInterface API
  rtc::Thread* network_thread() override { return network_thread_.get(); }
  rtc::NetworkManager* network_manager() override { return this; }
  void GetStats(
      std::function<void(EmulatedNetworkStats)> stats_callback) const override;

 private:
  void UpdateNetworksOnce();
  void MaybeSignalNetworksChanged();

  TaskQueueForTest* const task_queue_;
  EndpointsContainer* const endpoints_container_;
  std::unique_ptr<rtc::Thread> network_thread_;

  bool sent_first_update_ RTC_GUARDED_BY(network_thread_);
  int start_count_ RTC_GUARDED_BY(network_thread_);
};

}  // namespace test
}  // namespace webrtc

#endif  // TEST_NETWORK_EMULATED_NETWORK_MANAGER_H_
