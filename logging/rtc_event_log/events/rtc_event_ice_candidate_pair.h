/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_ICE_CANDIDATE_PAIR_H_
#define LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_ICE_CANDIDATE_PAIR_H_

#include <memory>
#include <string>

#include "logging/rtc_event_log/events/rtc_event.h"

namespace webrtc {

enum class IceCandidatePairEventType {
  kCheckSent,
  kCheckReceived,
  kCheckResponseSent,
  kCheckResponseReceived,
  kNumValues,
};

class RtcEventIceCandidatePair final : public RtcEvent {
 public:
  RtcEventIceCandidatePair(IceCandidatePairEventType type,
                           uint32_t candidate_pair_id,
                           const std::string& transaction_id);

  ~RtcEventIceCandidatePair() override;

  Type GetType() const override;

  bool IsConfigEvent() const override;

  std::unique_ptr<RtcEventIceCandidatePair> Copy() const;

  const IceCandidatePairEventType type_;
  const uint32_t candidate_pair_id_;
  const std::string transaction_id_;

 private:
  RtcEventIceCandidatePair(const RtcEventIceCandidatePair& other);
};

}  // namespace webrtc

#endif  // LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_ICE_CANDIDATE_PAIR_H_
