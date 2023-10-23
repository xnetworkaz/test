/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/test/create_time_controller.h"

#include <memory>

#include "api/connection_environment_builder.h"
#include "call/call.h"
#include "call/rtp_transport_config.h"
#include "call/rtp_transport_controller_send_factory_interface.h"
#include "test/time_controller/external_time_controller.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {

std::unique_ptr<TimeController> CreateTimeController(
    ControlledAlarmClock* alarm) {
  return std::make_unique<ExternalTimeController>(alarm);
}

std::unique_ptr<TimeController> CreateSimulatedTimeController() {
  return std::make_unique<GlobalSimulatedTimeController>(
      Timestamp::Seconds(10000));
}

std::unique_ptr<CallFactoryInterface> CreateTimeControllerBasedCallFactory(
    TimeController* time_controller) {
  class TimeControllerBasedCallFactory : public CallFactoryInterface {
   public:
    explicit TimeControllerBasedCallFactory(TimeController* time_controller)
        : time_controller_(time_controller) {}
    std::unique_ptr<Call> CreateCall(const CallConfig& config) override {
      CallConfig c = config;
      c.env = ConnectionEnvironmentBuilder(c.env)
                  .With(time_controller_->GetClock())
                  .Build();
      return Call::Create(config,
                          config.rtp_transport_controller_send_factory->Create(
                              config.ExtractTransportConfig()));
    }

   private:
    TimeController* time_controller_;
  };
  return std::make_unique<TimeControllerBasedCallFactory>(time_controller);
}

}  // namespace webrtc
