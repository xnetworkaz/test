/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_DEFINITION_H_
#define LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_DEFINITION_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/rtc_event_field_encoding.h"
#include "logging/rtc_event_log/events/rtc_event_field_encoding_parser.h"
#include "logging/rtc_event_log/events/rtc_event_field_extraction.h"
#include "rtc_base/logging.h"

namespace webrtc {

template <typename EventType, typename LoggedType, typename T>
struct RtcEventFieldDefinition {
  const T EventType::*event_member;
  T LoggedType::*logged_member;
  FieldParameters params;
};

// Base case
template <typename EventType, typename LoggedType, typename... Ts>
class RtcEventDefinitionImpl {
 public:
  void EncodeImpl(EventEncoder&, rtc::ArrayView<const RtcEvent*>) const {}
  RtcEventLogParseStatus ParseImpl(EventParser&,
                                   rtc::ArrayView<LoggedType>) const {
    return RtcEventLogParseStatus::Success();
  }
};

// Recursive case
template <typename EventType, typename LoggedType, typename T, typename... Ts>
class RtcEventDefinitionImpl<EventType, LoggedType, T, Ts...> {
 public:
  constexpr RtcEventDefinitionImpl(
      RtcEventFieldDefinition<EventType, LoggedType, T> field,
      RtcEventFieldDefinition<EventType, LoggedType, Ts>... rest)
      : field_(field), rest_(rest...) {}

  void EncodeImpl(EventEncoder& encoder,
                  rtc::ArrayView<const RtcEvent*> batch) const {
    auto values = ExtractRtcEventMember(batch, field_.event_member);
    encoder.EncodeField(field_.params, values);
    rest_.EncodeImpl(encoder, batch);
  }

  RtcEventLogParseStatus ParseImpl(
      EventParser& parser,
      rtc::ArrayView<LoggedType> output_batch) const {
    RtcEventLogParseStatusOr<rtc::ArrayView<uint64_t>> result =
        parser.ParseNumericField(field_.params);
    if (!result.ok())
      return result.status();
    auto status = PopulateRtcEventMember(result.value(), field_.logged_member,
                                         output_batch);
    if (!status.ok())
      return status;

    return rest_.ParseImpl(parser, output_batch);
  }

 private:
  RtcEventFieldDefinition<EventType, LoggedType, T> field_;
  RtcEventDefinitionImpl<EventType, LoggedType, Ts...> rest_;
};

template <typename EventType, typename LoggedType, typename... Ts>
class RtcEventDefinition {
 public:
  constexpr RtcEventDefinition(
      EventParameters params,
      RtcEventFieldDefinition<EventType, LoggedType, Ts>... fields)
      : params_(params), fields_(fields...) {}

  std::string EncodeBatch(rtc::ArrayView<const RtcEvent*> batch) const {
    EventEncoder encoder(params_, batch);
    fields_.EncodeImpl(encoder, batch);
    return encoder.AsString();
  }

  RtcEventLogParseStatus ParseBatch(absl::string_view s,
                                    bool batched,
                                    std::vector<LoggedType>& output) const {
    EventParser parser;
    auto status = parser.Initialize(s, batched);
    if (!status.ok())
      return status;

    rtc::ArrayView<LoggedType> output_batch =
        ExtendLoggedBatch(output, parser.NumEventsInBatch());

    constexpr FieldParameters timestamp_params{"timestamp_ms",
                                               FieldParameters::kTimestampField,
                                               FieldType::kVarInt, 64};
    RtcEventLogParseStatusOr<rtc::ArrayView<uint64_t>> result =
        parser.ParseNumericField(timestamp_params);
    if (!result.ok())
      return result.status();
    status = PopulateRtcEventTimestamp(result.value(), &LoggedType::timestamp,
                                       output_batch);
    if (!status.ok())
      return status;

    return fields_.ParseImpl(parser, output_batch);
  }

 private:
  EventParameters params_;
  RtcEventDefinitionImpl<EventType, LoggedType, Ts...> fields_;
};

}  // namespace webrtc

#endif  // LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_DEFINITION_H_
