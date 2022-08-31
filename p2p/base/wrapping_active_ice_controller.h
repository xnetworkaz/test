/*
 *  Copyright 2022 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_BASE_WRAPPING_ACTIVE_ICE_CONTROLLER_H_
#define P2P_BASE_WRAPPING_ACTIVE_ICE_CONTROLLER_H_

#include <memory>

#include "absl/types/optional.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "p2p/base/active_ice_controller_interface.h"
#include "p2p/base/connection.h"
#include "p2p/base/ice_agent_interface.h"
#include "p2p/base/ice_controller_interface.h"
#include "p2p/base/ice_controller_observer.h"
#include "p2p/base/ice_switch_reason.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/transport_description.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"

namespace cricket {

// WrappingActiveIceController provides the functionality of a passive ICE
// Controller but packaged as an active ICE Controller. This mean that right
// now, ActiveIceControllerAdapter + WrappingActiveIceController =
// LegacyIceControllerAdapter.
class WrappingActiveIceController : public ActiveIceControllerInterface {
 public:
  // Does not take ownership of the ICE agent, which must refer to a valid
  // object that outlives the ICE controller.
  explicit WrappingActiveIceController(
      IceAgentInterface* ice_agent,
      IceControllerObserver* observer,
      std::unique_ptr<IceControllerInterface> wrapped);
  virtual ~WrappingActiveIceController();

  void SetIceConfig(const IceConfig& config) override;
  bool GetUseCandidateAttribute(const Connection* connection,
                                NominationMode mode,
                                IceMode remote_ice_mode) const override;
  rtc::ArrayView<const Connection*> Connections() const override;

  void OnConnectionAdded(const Connection* connection) override;
  void OnConnectionPinged(const Connection* connection) override;
  void OnConnectionReport(const Connection* connection) override;
  void OnConnectionSwitched(const Connection* connection) override;
  void OnConnectionDestroyed(const Connection* connection) override;

  void OnStartPingingRequest() override;

  void OnSortAndSwitchRequest(IceSwitchReason reason) override;
  void OnImmediateSortAndSwitchRequest(IceSwitchReason reason) override;
  bool OnImmediateSwitchRequest(IceSwitchReason reason,
                                const Connection* selected) override;

  // Only for unit tests
  const Connection* FindNextPingableConnection() override;

 private:
  void PingBestConnection();
  void HandlePingResult(IceControllerInterface::PingResult result);
  void SwitchToBestConnectionAndPrune(IceSwitchReason reason);
  void HandleSwitchResult(IceSwitchReason reason_for_switch,
                          IceControllerInterface::SwitchResult result);

  rtc::Thread* const network_thread_;
  webrtc::ScopedTaskSafety task_safety_;

  bool started_pinging_ RTC_GUARDED_BY(network_thread_) = false;
  bool sort_pending_ RTC_GUARDED_BY(network_thread_) = false;

  std::unique_ptr<IceControllerInterface> wrapped_
      RTC_GUARDED_BY(network_thread_);
  IceAgentInterface& agent_ RTC_GUARDED_BY(network_thread_);
  IceControllerObserver* observer_ RTC_GUARDED_BY(network_thread_);
};

}  // namespace cricket

#endif  // P2P_BASE_WRAPPING_ACTIVE_ICE_CONTROLLER_H_
