/* Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This is an experimental interface and is subject to change without notice.

#ifndef API_DATA_CHANNEL_TRANSPORT_INTERFACE_H_
#define API_DATA_CHANNEL_TRANSPORT_INTERFACE_H_

#include "absl/types/optional.h"
#include "api/rtc_error.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace webrtc {

// Supported types of application data messages.
enum class DataMessageType {
  // Application data buffer with the binary bit unset.
  kText,

  // Application data buffer with the binary bit set.
  kBinary,

  // Transport-agnostic control messages, such as open or open-ack messages.
  kControl,
};

// Parameters for sending data.  The parameters may change from message to
// message, even within a single channel.  For example, control messages may be
// sent reliably and in-order, even if the data channel is configured for
// unreliable delivery.
struct SendDataParams {
  SendDataParams();
  SendDataParams(const SendDataParams&);

  DataMessageType type = DataMessageType::kText;

  // Whether to deliver the message in order with respect to other ordered
  // messages with the same channel_id.
  bool ordered = false;

  // If set, the maximum number of times this message may be
  // retransmitted by the transport before it is dropped.
  // Setting this value to zero disables retransmission.
  // Must be non-negative. |max_rtx_count| and |max_rtx_ms| may not be set
  // simultaneously.
  absl::optional<int> max_rtx_count;

  // If set, the maximum number of milliseconds for which the transport
  // may retransmit this message before it is dropped.
  // Setting this value to zero disables retransmission.
  // Must be non-negative. |max_rtx_count| and |max_rtx_ms| may not be set
  // simultaneously.
  absl::optional<int> max_rtx_ms;
};

// Sink for callbacks related to a data channel.
class DataChannelSink {
 public:
  virtual ~DataChannelSink() = default;

  // Callback issued when data is received by the transport.
  virtual void OnDataReceived(int channel_id,
                              DataMessageType type,
                              const rtc::CopyOnWriteBuffer& buffer) = 0;

  // Callback issued when a remote data channel begins the closing procedure.
  // Messages sent after the closing procedure begins will not be transmitted.
  virtual void OnChannelClosing(int channel_id) = 0;

  // Callback issued when a (remote or local) data channel completes the closing
  // procedure.  Closing channels become closed after all pending data has been
  // transmitted.
  virtual void OnChannelClosed(int channel_id) = 0;
};

// Transport for data channels.
class DataChannelTransportInterface {
 public:
  virtual ~DataChannelTransportInterface() = default;

  // Opens a data |channel_id| for sending.  May return an error if the
  // specified |channel_id| is unusable.  Must be called before |SendData|.
  virtual RTCError OpenChannel(int channel_id);

  // Sends a data buffer to the remote endpoint using the given send parameters.
  // |buffer| may not be larger than 256 KiB. Returns an error if the send
  // fails.
  virtual RTCError SendData(int channel_id,
                            const SendDataParams& params,
                            const rtc::CopyOnWriteBuffer& buffer);

  // Closes |channel_id| gracefully.  Returns an error if |channel_id| is not
  // open.  Data sent after the closing procedure begins will not be
  // transmitted. The channel becomes closed after pending data is transmitted.
  virtual RTCError CloseChannel(int channel_id);

  // Sets a sink for data messages and channel state callbacks. Before media
  // transport is destroyed, the sink must be unregistered by setting it to
  // nullptr.
  virtual void SetDataSink(DataChannelSink* sink);
};

}  // namespace webrtc

#endif  // API_DATA_CHANNEL_TRANSPORT_INTERFACE_H_
