/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_device/win/core_audio_input_win.h"

#include "absl/memory/memory.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/fine_audio_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"

using Microsoft::WRL::ComPtr;

namespace webrtc {
namespace webrtc_win {

enum AudioDeviceMessageType : uint32_t {
  kMessageInputStreamDisconnected,
};

CoreAudioInput::CoreAudioInput()
    : CoreAudioBase(CoreAudioBase::Direction::kInput,
                    [this](uint64_t freq) { return OnDataCallback(freq); },
                    [this](ErrorType err) { return OnErrorCallback(err); }) {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  thread_checker_audio_.DetachFromThread();
}

CoreAudioInput::~CoreAudioInput() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
}

int CoreAudioInput::Init() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  StopRecording();
  return 0;
}

int CoreAudioInput::Terminate() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return 0;
}

int CoreAudioInput::NumDevices() const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return core_audio_utility::NumberOfActiveDevices(eCapture);
}

int CoreAudioInput::SetDevice(int index) {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << index;
  return CoreAudioBase::SetDevice(index);
}

int CoreAudioInput::SetDevice(AudioDeviceModule::WindowsDeviceType device) {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << device;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return SetDevice((device == AudioDeviceModule::kDefaultDevice) ? 0 : 1);
}

int CoreAudioInput::DeviceName(int index,
                               std::string* name,
                               std::string* guid) {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << index;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(name);
  return CoreAudioBase::DeviceName(index, name, guid);
}

void CoreAudioInput::AttachAudioBuffer(AudioDeviceBuffer* audio_buffer) {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  audio_device_buffer_ = audio_buffer;
}

bool CoreAudioInput::RecordingIsInitialized() const {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << initialized_;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return initialized_;
}

int CoreAudioInput::InitRecording() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK(!initialized_);
  RTC_DCHECK(!Recording());
  RTC_DCHECK(!audio_capture_client_.Get());

  // Create an IAudioClient and store the valid interface pointer in
  // |audio_client_|. The base class will use optimal input parameters and do
  // an event driven shared mode initialization. The utilized format will be
  // stored in |format_| and can be used for configuration and allocation of
  // audio buffers.
  if (!CoreAudioBase::Init()) {
    return -1;
  }
  RTC_DCHECK(audio_client_.Get());

  // Configure the recording side of the audio device buffer using |format_|
  // after a trivial sanity check of the format structure.
  RTC_DCHECK(audio_device_buffer_);
  WAVEFORMATEX* format = &format_.Format;
  RTC_DCHECK_EQ(format->wFormatTag, WAVE_FORMAT_EXTENSIBLE);
  audio_device_buffer_->SetRecordingSampleRate(format->nSamplesPerSec);
  audio_device_buffer_->SetRecordingChannels(format->nChannels);

  // Create a modified audio buffer class which allows us to supply any number
  // of samples (and not only multiple of 10ms) to match the optimal buffer
  // size per callback used by Core Audio.
  // TODO(henrika): can we share one FineAudioBuffer?
  fine_audio_buffer_ = absl::make_unique<FineAudioBuffer>(audio_device_buffer_);

  // Create an IAudioCaptureClient for an initialized IAudioClient.
  // The IAudioCaptureClient interface enables a client to read input data from
  // a capture endpoint buffer.
  ComPtr<IAudioCaptureClient> audio_capture_client =
      core_audio_utility::CreateCaptureClient(audio_client_.Get());
  if (!audio_capture_client.Get()) {
    return -1;
  }

  // Query performance frequency.
  LARGE_INTEGER ticks_per_sec = {};
  qpc_to_100ns_.reset();
  if (::QueryPerformanceFrequency(&ticks_per_sec)) {
    double qpc_ticks_per_second =
        rtc::dchecked_cast<double>(ticks_per_sec.QuadPart);
    qpc_to_100ns_ = 10000000.0 / qpc_ticks_per_second;
  }

  // Store valid COM interfaces. Note that, |audio_client_| has already been
  // set in CoreAudioBase::Init().
  audio_capture_client_ = audio_capture_client;

  initialized_ = true;
  return 0;
}

int CoreAudioInput::StartRecording() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK(!Recording());
  if (!initialized_) {
    RTC_DLOG(LS_WARNING)
        << "Recording can not start since InitRecording must succeed first";
    return 0;
  }
  if (fine_audio_buffer_) {
    fine_audio_buffer_->ResetRecord();
  }

  if (!Start()) {
    return -1;
  }

  is_active_ = true;
  return 0;
}

int CoreAudioInput::StopRecording() {
  RTC_DLOG(INFO) << __FUNCTION__;
  if (!initialized_) {
    return 0;
  }

  // Release resources allocated in InitRecording() and then return if this
  // method is called without any active input audio.
  if (!Recording()) {
    RTC_DLOG(WARNING) << "No input stream is active";
    SafeRelease();
    initialized_ = false;
    return 0;
  }

  if (!Stop()) {
    RTC_LOG(LS_ERROR) << "StopRecording failed";
    return -1;
  }

  // Release all allocated resources to allow for a restart without
  // intermediate destruction.
  SafeRelease();
  qpc_to_100ns_.reset();

  initialized_ = false;
  is_active_ = false;
  return 0;
}

bool CoreAudioInput::Recording() {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << is_active_;
  return is_active_;
}

// TODO(henrika): finalize support of audio session volume control. As is, we
// are not compatible with the old ADM implementation since it allows accessing
// the volume control with any active audio output stream.
int CoreAudioInput::VolumeIsAvailable(bool* available) {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return IsVolumeControlAvailable(available) ? 0 : -1;
}

int CoreAudioInput::RestartRecording() {
  RTC_DLOG(INFO) << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!Recording()) {
    return 0;
  }

  if (!Restart()) {
    RTC_LOG(LS_ERROR) << "RestartRecording failed";
    return -1;
  }
  return 0;
}

void CoreAudioInput::SafeRelease() {
  RTC_DLOG(INFO) << __FUNCTION__;
  CoreAudioBase::SafeRelease();
  if (audio_capture_client_.Get()) {
    audio_capture_client_.Reset();
  }
}

bool CoreAudioInput::OnDataCallback(uint64_t device_frequency) {
  RTC_DCHECK_RUN_ON(&thread_checker_audio_);
  UINT32 num_frames_in_next_packet = 0;
  _com_error error =
      audio_capture_client_->GetNextPacketSize(&num_frames_in_next_packet);
  if (error.Error() == AUDCLNT_E_DEVICE_INVALIDATED) {
    // Avoid breaking the thread loop implicitly by returning false and return
    // true instead for AUDCLNT_E_DEVICE_INVALIDATED even it is a valid error
    // message. We will use notifications about device changes instead to stop
    // data callbacks and attempt to restart streaming .
    RTC_DLOG(LS_ERROR) << "AUDCLNT_E_DEVICE_INVALIDATED";
    return true;
  }
  if (FAILED(error.Error())) {
    RTC_LOG(LS_ERROR) << "IAudioCaptureClient::GetNextPacketSize failed: "
                      << core_audio_utility::ErrorToString(error);
    return false;
  }

  // Drain the WASAPI capture buffer fully if audio has been recorded.
  while (num_frames_in_next_packet > 0) {
    uint8_t* audio_data;
    UINT32 num_frames_to_read = 0;
    DWORD flags = 0;
    UINT64 device_position_frames = 0;
    UINT64 capture_time_100ns = 0;
    error = audio_capture_client_->GetBuffer(&audio_data, &num_frames_to_read,
                                             &flags, &device_position_frames,
                                             &capture_time_100ns);
    if (error.Error() == AUDCLNT_S_BUFFER_EMPTY) {
      // The call succeeded but no capture data is available to be read.
      // Return and start waiting for new capture event
      RTC_DCHECK_EQ(num_frames_to_read, 0u);
      return true;
    }
    if (FAILED(error.Error())) {
      RTC_LOG(LS_ERROR) << "IAudioCaptureClient::GetBuffer failed: "
                        << core_audio_utility::ErrorToString(error);
      return false;
    }

    // TODO(henrika): only update the latency estimate N times per second to
    // save resources.
    // TODO(henrika): note that FineAudioBuffer adds latency as well.
    auto opt_record_delay_ms = EstimateLatencyMillis(capture_time_100ns);

    // The data in the packet is not correlated with the previous packet's
    // device position; possibly due to a stream state transition or timing
    // glitch. The behavior of the AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY flag
    // is undefined on the application's first call to GetBuffer after Start.
    if (device_position_frames != 0 &&
        flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
      RTC_DLOG(LS_WARNING) << "AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY";
    }
    // The time at which the device's stream position was recorded is uncertain.
    // Thus, the client might be unable to accurately set a time stamp for the
    // current data packet.
    if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
      RTC_DLOG(LS_WARNING) << "AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR";
    }

    // Treat all of the data in the packet as silence and ignore the actual
    // data values when AUDCLNT_BUFFERFLAGS_SILENT is set.
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
      rtc::ExplicitZeroMemory(audio_data,
                              format_.Format.nBlockAlign * num_frames_to_read);
      RTC_DLOG(LS_WARNING) << "Captured audio is replaced by silence";
    } else {
      // Copy recorded audio in |audio_data| to the WebRTC sink using the
      // FineAudioBuffer object.
      // TODO(henrika): fix delay estimation.
      int record_delay_ms = 0;
      if (opt_record_delay_ms) {
        record_delay_ms = *opt_record_delay_ms;
        // RTC_DLOG(INFO) << "record_delay_ms: " << record_delay_ms;
      }
      fine_audio_buffer_->DeliverRecordedData(
          rtc::MakeArrayView(reinterpret_cast<const int16_t*>(audio_data),
                             format_.Format.nChannels * num_frames_to_read),

          record_delay_ms);
    }

    error = audio_capture_client_->ReleaseBuffer(num_frames_to_read);
    if (FAILED(error.Error())) {
      RTC_LOG(LS_ERROR) << "IAudioCaptureClient::ReleaseBuffer failed: "
                        << core_audio_utility::ErrorToString(error);
      return false;
    }

    error =
        audio_capture_client_->GetNextPacketSize(&num_frames_in_next_packet);
    if (FAILED(error.Error())) {
      RTC_LOG(LS_ERROR) << "IAudioCaptureClient::GetNextPacketSize failed: "
                        << core_audio_utility::ErrorToString(error);
      return false;
    }
  }

  return true;
}

bool CoreAudioInput::OnErrorCallback(ErrorType error) {
  RTC_DLOG(INFO) << __FUNCTION__ << ": " << as_integer(error);
  RTC_DCHECK_RUN_ON(&thread_checker_audio_);
  if (error == CoreAudioBase::ErrorType::kStreamDisconnected) {
    HandleStreamDisconnected();
  } else {
    RTC_DLOG(WARNING) << "Unsupported error type";
  }
  return true;
}

absl::optional<int> CoreAudioInput::EstimateLatencyMillis(
    uint64_t capture_time_100ns) {
  if (!qpc_to_100ns_) {
    return absl::nullopt;
  }
  // Input parameter |capture_time_100ns| contains the performance counter at
  // the time that the audio endpoint device recorded the device position of
  // the first audio frame in the data packet converted into 100ns units.
  // We derive a delay estimate by:
  // - sampling the current performance counter (qpc_now_raw),
  // - converting it into 100ns time units (now_time_100ns), and
  // - subtracting |capture_time_100ns| from now_time_100ns.
  LARGE_INTEGER perf_counter_now = {};
  if (!::QueryPerformanceCounter(&perf_counter_now)) {
    return absl::nullopt;
  }
  uint64_t qpc_now_raw = perf_counter_now.QuadPart;
  uint64_t now_time_100ns = qpc_now_raw * (*qpc_to_100ns_);
  webrtc::TimeDelta delay_us =
      webrtc::TimeDelta::us(0.1 * (now_time_100ns - capture_time_100ns) + 0.5);
  return delay_us.ms();
}

// Called from OnErrorCallback() when error type is kStreamDisconnected.
// Note that this method is called on the audio thread and the internal restart
// sequence is also executed on that same thread. The audio thread is therefore
// not stopped during restart. Such a scheme also makes the restart process less
// complex.
// Note that, none of the called methods are thread checked since they can also
// be called on the main thread. Thread checkers are instead added on one layer
// above (in audio_device_module.cc) which ensures that the public API is thread
// safe.
// TODO(henrika): add more details.
bool CoreAudioInput::HandleStreamDisconnected() {
  RTC_DLOG(INFO) << "<<<--- " << __FUNCTION__;
  RTC_DCHECK_RUN_ON(&thread_checker_audio_);

  if (StopRecording() != 0) {
    return false;
  }

  RTC_DLOG(INFO) << "device_index=" << device_index_
                 << " => device_id: " << device_id_;

  // Ensure that at least one device exists and can be utilized. The most
  // probable cause for ending up here is that a device has been removed.
  if (core_audio_utility::NumberOfActiveDevices(eCapture) < 1) {
    RTC_DLOG(LS_ERROR) << "All input devices are disabled or removed";
    return false;
  }

  // Get the unique device ID for the index which is currently used. It seems
  // safe to assume that if the ID is the same as the existing device ID, then
  // the device configuration is the same as before.
  std::string device_id = GetDeviceID(device_index_);
  if (device_id != device_id_) {
    RTC_LOG(LS_WARNING)
        << "Device configuration has changed => changing device selection...";
    // TODO(henrika): depending on the current state and how we got here, we
    // must select a new device here.
    if (CoreAudioBase::SetDevice(0) == -1) {
      RTC_LOG(LS_WARNING) << "Failed to set new input device";
      return false;
    }
  } else {
    RTC_LOG(INFO)
        << "Device configuration has not changed => keeping selected device";
  }

  if (InitRecording() != 0) {
    return false;
  }
  if (StartRecording() != 0) {
    return false;
  }

  RTC_DLOG(INFO) << __FUNCTION__ << " --->>>";
  return true;
}

}  // namespace webrtc_win
}  // namespace webrtc
