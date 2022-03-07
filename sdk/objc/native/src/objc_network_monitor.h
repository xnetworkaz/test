/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SDK_OBJC_NATIVE_SRC_OBJC_NETWORK_MONITOR_H_
#define SDK_OBJC_NATIVE_SRC_OBJC_NETWORK_MONITOR_H_

#include <vector>

#include "api/sequence_checker.h"
#include "rtc_base/network_monitor.h"
#include "rtc_base/network_monitor_factory.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/task_utils/pending_task_safety_flag.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "sdk/objc/components/network/RTCNetworkMonitor+Private.h"
#include "sdk/objc/native/src/network_monitor_observer.h"

namespace webrtc {

class ObjCNetworkMonitorFactory : public rtc::NetworkMonitorFactory {
 public:
  ObjCNetworkMonitorFactory() = default;
  ~ObjCNetworkMonitorFactory() override = default;

  rtc::NetworkMonitorInterface* CreateNetworkMonitor() override;
};

class ObjCNetworkMonitor : public rtc::NetworkMonitorInterface,
                           public NetworkMonitorObserver {
 public:
  ObjCNetworkMonitor();
  ~ObjCNetworkMonitor() override;

  void Start() override;
  void Stop() override;

  rtc::AdapterType GetAdapterType(absl::string_view interface_name) override;
  rtc::AdapterType GetVpnUnderlyingAdapterType(
      absl::string_view interface_name) override;
  rtc::NetworkPreference GetNetworkPreference(
      absl::string_view interface_name) override;
  bool IsAdapterAvailable(absl::string_view interface_name) override;

  // NetworkMonitorObserver override.
  // Fans out updates to observers on the correct thread.
  void OnPathUpdate(
      std::map<std::string, rtc::AdapterType> adapter_type_by_name) override;

 private:
  rtc::Thread* thread_ = nullptr;
  bool started_ = false;
  std::map<std::string, rtc::AdapterType, rtc::AbslStringViewCmp>
      adapter_type_by_name_ RTC_GUARDED_BY(thread_);
  rtc::scoped_refptr<PendingTaskSafetyFlag> safety_flag_;
  RTCNetworkMonitor* network_monitor_ = nil;
};

}  // namespace webrtc

#endif  // SDK_OBJC_NATIVE_SRC_OBJC_NETWORK_MONITOR_H_
