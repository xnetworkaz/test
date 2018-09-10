/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This is EXPERIMENTAL interface for media transport.
//
// The goal is to refactor WebRTC code so that audio and video frames
// are sent / received through the media transport interface. This will
// enable different media transport implementations, including QUIC-based
// media transport.

#ifndef API_MEDIA_TRANSPORT_INTERFACE_H_
#define API_MEDIA_TRANSPORT_INTERFACE_H_

#include <memory>
#include <utility>
#include <vector>

#include "api/rtcerror.h"
#include "common_types.h"  // NOLINT(build/include)

namespace rtc {
class PacketTransportInternal;
class Thread;
}  // namespace rtc

namespace webrtc {

// Represents encoded audio frame in any encoding (type of encoding is opaque).
// To avoid copying of encoded data use move semantics when passing by value.
class MediaTransportEncodedAudioFrame {
 public:
  enum class FrameType {
    // Normal audio frame (equivalent to webrtc::kAudioFrameSpeech).
    kSpeech,

    // DTX frame (equivalent to webrtc::kAudioFrameCN).
    kDiscountinuousTransmission,
  };

  MediaTransportEncodedAudioFrame(
      // Audio sampling rate, for example 48000.
      int sampling_rate_hz,

      // Starting sample index of the frame, i.e. how many audio samples were
      // before this frame since the beginning of the call or beginning of time
      // in one channel (the starting point should not matter for NetEq). In
      // WebRTC it is used as a timestamp of the frame.
      // TODO(sukhanov): Starting_sample_index is currently adjusted on the
      // receiver side in RTP path. Non-RTP implementations should preserve it.
      // For NetEq initial offset should not matter so we should consider fixing
      // RTP path.
      int starting_sample_index,

      // Number of audio samples in audio frame in 1 channel.
      int samples_per_channel,

      // Sequence number of the frame in the order sent, it is currently
      // required by NetEq, but we can fix NetEq, because starting_sample_index
      // should be enough.
      int sequence_number,

      // If audio frame is a speech or discontinued transmission.
      FrameType frame_type,

      // Opaque payload type. In RTP codepath payload type is stored in RTP
      // header. In other implementations it should be simply passed through the
      // wire -- it's needed for decoder.
      uint8_t payload_type,

      // Vector with opaque encoded data.
      std::vector<uint8_t> encoded_data)
      : sampling_rate_hz_(sampling_rate_hz),
        starting_sample_index_(starting_sample_index),
        samples_per_channel_(samples_per_channel),
        sequence_number_(sequence_number),
        frame_type_(frame_type),
        payload_type_(payload_type),
        encoded_data_(std::move(encoded_data)) {}

  // Getters.
  int sampling_rate_hz() const { return sampling_rate_hz_; }
  int starting_sample_index() const { return starting_sample_index_; }
  int samples_per_channel() const { return samples_per_channel_; }
  int sequence_number() const { return sequence_number_; }

  uint8_t payload_type() const { return payload_type_; }
  FrameType frame_type() const { return frame_type_; }

  rtc::ArrayView<const uint8_t> encoded_data() const { return encoded_data_; }

 private:
  int sampling_rate_hz_;
  int starting_sample_index_;
  int sample_count_;

  // TODO(sukhanov): Refactor NetEq so we don't need sequence number.
  // Having sample_index and sample_count should be enough.
  int sequence_number_;

  FrameType frame_type_;

  // TODO(sukhanov): Consider enumerating allowed encodings and store enum
  // instead of uint payload_type.
  uint8_t payload_type_;

  std::vector<uint8_t> encoded_data_;
};

// Interface for receiving encoded audio frames from MediaTransportInterface
// implementations.
class MediaTransportAudioSinkInterface {
 public:
  virtual ~MediaTransportAudioSinkInterface() = default;

  // Called when new encoded audio frame is received.
  virtual void OnData(uint64_t channel_id,
                      MediaTransportEncodedAudioFrame frame) = 0;
};

// Represents encoded video frame, along with the codec information.
class MediaTransportEncodedVideoFrame {
 public:
  MediaTransportEncodedVideoFrame(int frame_id,
                                  std::vector<int> referenced_frame_ids,
                                  VideoCodecType codec_type,
                                  const webrtc::EncodedImage& encoded_image)
      : codec_type_(codec_type),
        encoded_image_(encoded_image),
        frame_id_(frame_id),
        referenced_frame_ids_(std::move(referenced_frame_ids)) {}

  VideoCodecType codec_type() const { return codec_type_; }
  const webrtc::EncodedImage& encoded_image() const { return encoded_image_; }

  int frame_id() const { return frame_id_; }
  const std::vector<int>& referenced_frame_ids() const {
    return referenced_frame_ids_;
  }

 private:
  VideoCodecType codec_type_;

  webrtc::EncodedImage encoded_image_;

  // Frame id uniquely identifies a frame in a stream.
  // It is required by a remote jitter buffer.
  int frame_id_;

  // A single frame might depend on other frames. This is set of identifiers on
  // which the current frame depends.
  std::vector<int> referenced_frame_ids_;
};

// Interface for receiving encoded video frames from MediaTransportInterface
// implementations.
class MediaTransportVideoSinkInterface {
 public:
  virtual ~MediaTransportVideoSinkInterface() = default;

  // Called when new encoded video frame is received.
  virtual void OnData(uint64_t channel_id,
                      MediaTransportEncodedVideoFrame frame) = 0;

  // Called when the request for keyframe is received.
  virtual void OnKeyFrameRequested(uint64_t channel_id) = 0;
};

// Callbacks related to the state of the entire Media Transport, and not related
// to the individual stream types (video/audio).
class MediaTransportStateSinkInterface {
 public:
  virtual ~MediaTransportStateSinkInterface() = default;

  // Each time writable state is changed, the |OnWritableChanged| is invoked.
  // The |OnWritableChanged| will be invoked immediately when setting the
  // callback, if the media transport is writable to begin with.
  virtual void OnWritableChanged(bool is_writable) = 0;
};

// Media transport interface for sending / receiving encoded audio/video frames
// and receiving bandwidth estimate update from congestion control.
class MediaTransportInterface {
 public:
  virtual ~MediaTransportInterface() = default;

  // Start asynchronous send of audio frame.
  virtual RTCError SendAudioFrame(uint64_t channel_id,
                                  MediaTransportEncodedAudioFrame frame) = 0;

  // Start asynchronous send of video frame.
  virtual RTCError SendVideoFrame(
      uint64_t channel_id,
      const MediaTransportEncodedVideoFrame& frame) = 0;

  // Requests the keyframe for the particular channel (stream).
  virtual RTCError RequestKeyFrame(uint64_t channel_id) = 0;

  // Sets audio sink. Sink must be unset by calling SetAudioSink(nullptr)
  // before the media transport is destroyed or before new sink is set.
  virtual void SetReceiveAudioSink(MediaTransportAudioSinkInterface* sink) = 0;

  // Registers a video sink. Before destruction of media transport, you must
  // pass a nullptr.
  virtual void SetReceiveVideoSink(MediaTransportVideoSinkInterface* sink) = 0;

  // Registers callbacks related to the media transport interface itself (e.g.
  // whether it's writable).
  virtual void SetStateSink(MediaTransportStateSinkInterface* callbacks) = 0;

  // TODO(sukhanov): RtcEventLogs.
  // TODO(sukhanov): Bandwidth updates.
};

// If media transport factory is set in peer connection factory, it will be
// used to create media transport for sending/receiving encoded frames and
// this transport will be used instead of default RTP/SRTP transport.
//
// Currently Media Transport negotiation is not supported in SDP.
// If application is using media transport, it must negotiate it before
// setting media transport factory in peer connection.
class MediaTransportFactory {
 public:
  virtual ~MediaTransportFactory() = default;

  // Creates media transport.
  // - Does not take ownership of packet_transport or network_thread.
  // - Does not support group calls, in 1:1 call one side must set
  //   is_caller = true and another is_caller = false.
  virtual RTCErrorOr<std::unique_ptr<MediaTransportInterface>>
  CreateMediaTransport(rtc::PacketTransportInternal* packet_transport,
                       rtc::Thread* network_thread,
                       bool is_caller) = 0;
};

}  // namespace webrtc
#endif  // API_MEDIA_TRANSPORT_INTERFACE_H_
