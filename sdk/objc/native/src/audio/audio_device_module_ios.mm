/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio_device_module_ios.h"

#include "api/task_queue/default_task_queue_factory.h"
#include "modules/audio_device/audio_device_config.h"
#include "modules/audio_device/audio_device_generic.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/ref_count.h"
#include "system_wrappers/include/metrics.h"

#if defined(WEBRTC_IOS)
#include "audio_device_ios.h"
#endif

#define CHECKinitialized_() \
  {                         \
    if (!initialized_) {    \
      return -1;            \
    };                      \
  }

#define CHECKinitialized__BOOL() \
  {                              \
    if (!initialized_) {         \
      return false;              \
    };                           \
  }

namespace webrtc {
namespace ios_adm {

AudioDeviceModuleIOS::AudioDeviceModuleIOS(bool bypass_voice_processing)
    : bypass_voice_processing_(bypass_voice_processing),
      task_queue_factory_(CreateDefaultTaskQueueFactory()) {
  RTC_LOG(LS_INFO) << "current platform is IOS";
  RTC_LOG(LS_INFO) << "iPhone Audio APIs will be utilized.";
}

  int32_t AudioDeviceModuleIOS::AttachAudioBuffer() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    audio_device_->AttachAudioBuffer(audio_device_buffer_.get());
    return 0;
  }

  AudioDeviceModuleIOS::~AudioDeviceModuleIOS() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
  }

  int32_t AudioDeviceModuleIOS::ActiveAudioLayer(AudioLayer* audioLayer) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    AudioLayer activeAudio;
    if (audio_device_->ActiveAudioLayer(activeAudio) == -1) {
      return -1;
    }
    *audioLayer = activeAudio;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::Init() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    if (initialized_)
      return 0;

    audio_device_buffer_.reset(new webrtc::AudioDeviceBuffer(task_queue_factory_.get()));
    audio_device_.reset(new ios_adm::AudioDeviceIOS(bypass_voice_processing_));
    RTC_CHECK(audio_device_);

    this->AttachAudioBuffer();

    AudioDeviceGeneric::InitStatus status = audio_device_->Init();
    RTC_HISTOGRAM_ENUMERATION(
        "WebRTC.Audio.InitializationResult", static_cast<int>(status),
        static_cast<int>(AudioDeviceGeneric::InitStatus::NUM_STATUSES));
    if (status != AudioDeviceGeneric::InitStatus::OK) {
      RTC_LOG(LS_ERROR) << "Audio device initialization failed.";
      return -1;
    }
    initialized_ = true;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::Terminate() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    if (!initialized_)
      return 0;
    if (audio_device_->Terminate() == -1) {
      return -1;
    }
    initialized_ = false;
    return 0;
  }

  bool AudioDeviceModuleIOS::Initialized() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << ": " << initialized_;
    return initialized_;
  }

  int32_t AudioDeviceModuleIOS::InitSpeaker() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    return audio_device_->InitSpeaker();
  }

  int32_t AudioDeviceModuleIOS::InitMicrophone() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    return audio_device_->InitMicrophone();
  }

  bool AudioDeviceModuleIOS::SpeakerIsInitialized() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    bool isInitialized = audio_device_->SpeakerIsInitialized();
    RTC_DLOG(LS_INFO) << "output: " << isInitialized;
    return isInitialized;
  }

  bool AudioDeviceModuleIOS::MicrophoneIsInitialized() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    bool isInitialized = audio_device_->MicrophoneIsInitialized();
    RTC_DLOG(LS_INFO) << "output: " << isInitialized;
    return isInitialized;
  }

  int32_t AudioDeviceModuleIOS::StereoRecordingIsAvailable(
      bool* available) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool isAvailable = false;
    if (audio_device_->StereoRecordingIsAvailable(isAvailable) == -1) {
      return -1;
    }
    *available = isAvailable;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::SetStereoRecording(bool enable) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    CHECKinitialized_();
    if (enable) {
      RTC_LOG(LS_WARNING) << "recording in stereo is not supported";
    }
    return -1;
  }

  int32_t AudioDeviceModuleIOS::StereoRecording(bool* enabled) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool stereo = false;
    if (audio_device_->StereoRecording(stereo) == -1) {
      return -1;
    }
    *enabled = stereo;
    RTC_DLOG(LS_INFO) << "output: " << stereo;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::StereoPlayoutIsAvailable(bool* available) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool isAvailable = false;
    if (audio_device_->StereoPlayoutIsAvailable(isAvailable) == -1) {
      return -1;
    }
    *available = isAvailable;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::SetStereoPlayout(bool enable) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    CHECKinitialized_();
    if (audio_device_->PlayoutIsInitialized()) {
      RTC_LOG(LS_ERROR) << "unable to set stereo mode while playing side is initialized";
      return -1;
    }
    if (audio_device_->SetStereoPlayout(enable)) {
      RTC_LOG(LS_WARNING) << "stereo playout is not supported";
      return -1;
    }
    int8_t nChannels(1);
    if (enable) {
      nChannels = 2;
    }
    audio_device_buffer_.get()->SetPlayoutChannels(nChannels);
    return 0;
  }

  int32_t AudioDeviceModuleIOS::StereoPlayout(bool* enabled) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool stereo = false;
    if (audio_device_->StereoPlayout(stereo) == -1) {
      return -1;
    }
    *enabled = stereo;
    RTC_DLOG(LS_INFO) << "output: " << stereo;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::PlayoutIsAvailable(bool* available) {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool isAvailable = false;
    if (audio_device_->PlayoutIsAvailable(isAvailable) == -1) {
      return -1;
    }
    *available = isAvailable;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return 0;
  }

  int32_t AudioDeviceModuleIOS::RecordingIsAvailable(bool* available) {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    bool isAvailable = false;
    if (audio_device_->RecordingIsAvailable(isAvailable) == -1) {
      return -1;
    }
    *available = isAvailable;
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return 0;
  }

  int16_t AudioDeviceModuleIOS::PlayoutDevices() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    uint16_t nPlayoutDevices = audio_device_->PlayoutDevices();
    RTC_DLOG(LS_INFO) << "output: " << nPlayoutDevices;
    return (int16_t)(nPlayoutDevices);
  }

  int32_t AudioDeviceModuleIOS::SetPlayoutDevice(uint16_t index) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << index << ")";
    CHECKinitialized_();
    return audio_device_->SetPlayoutDevice(index);
  }

  int32_t AudioDeviceModuleIOS::SetPlayoutDevice(WindowsDeviceType device) {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    return audio_device_->SetPlayoutDevice(device);
  }

  int32_t AudioDeviceModuleIOS::PlayoutDeviceName(
      uint16_t index,
      char name[kAdmMaxDeviceNameSize],
      char guid[kAdmMaxGuidSize]) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << index << ", ...)";
    CHECKinitialized_();
    if (name == NULL) {
      return -1;
    }
    if (audio_device_->PlayoutDeviceName(index, name, guid) == -1) {
      return -1;
    }
    if (name != NULL) {
      RTC_DLOG(LS_INFO) << "output: name = " << name;
    }
    if (guid != NULL) {
      RTC_DLOG(LS_INFO) << "output: guid = " << guid;
    }
    return 0;
  }

  int32_t AudioDeviceModuleIOS::RecordingDeviceName(
      uint16_t index,
      char name[kAdmMaxDeviceNameSize],
      char guid[kAdmMaxGuidSize]) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << index << ", ...)";
    CHECKinitialized_();
    if (name == NULL) {
      return -1;
    }
    if (audio_device_->RecordingDeviceName(index, name, guid) == -1) {
      return -1;
    }
    if (name != NULL) {
      RTC_DLOG(LS_INFO) << "output: name = " << name;
    }
    if (guid != NULL) {
      RTC_DLOG(LS_INFO) << "output: guid = " << guid;
    }
    return 0;
  }

  int16_t AudioDeviceModuleIOS::RecordingDevices() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    uint16_t nRecordingDevices = audio_device_->RecordingDevices();
    RTC_DLOG(LS_INFO) << "output: " << nRecordingDevices;
    return (int16_t)nRecordingDevices;
  }

  int32_t AudioDeviceModuleIOS::SetRecordingDevice(uint16_t index) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << index << ")";
    CHECKinitialized_();
    return audio_device_->SetRecordingDevice(index);
  }

  int32_t AudioDeviceModuleIOS::SetRecordingDevice(WindowsDeviceType device) {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    return audio_device_->SetRecordingDevice(device);
  }

  int32_t AudioDeviceModuleIOS::InitPlayout() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    if (PlayoutIsInitialized()) {
      return 0;
    }
    int32_t result = audio_device_->InitPlayout();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.InitPlayoutSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  int32_t AudioDeviceModuleIOS::InitRecording() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    if (RecordingIsInitialized()) {
      return 0;
    }
    int32_t result = audio_device_->InitRecording();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.InitRecordingSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  bool AudioDeviceModuleIOS::PlayoutIsInitialized() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    return audio_device_->PlayoutIsInitialized();
  }

  bool AudioDeviceModuleIOS::RecordingIsInitialized() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    return audio_device_->RecordingIsInitialized();
  }

  int32_t AudioDeviceModuleIOS::StartPlayout() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    if (Playing()) {
      return 0;
    }
    audio_device_buffer_.get()->StartPlayout();
    int32_t result = audio_device_->StartPlayout();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.StartPlayoutSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  int32_t AudioDeviceModuleIOS::StopPlayout() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    int32_t result = audio_device_->StopPlayout();
    audio_device_buffer_.get()->StopPlayout();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.StopPlayoutSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  bool AudioDeviceModuleIOS::Playing() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    return audio_device_->Playing();
  }

  int32_t AudioDeviceModuleIOS::StartRecording() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    if (Recording()) {
      return 0;
    }
    audio_device_buffer_.get()->StartRecording();
    int32_t result = audio_device_->StartRecording();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.StartRecordingSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  int32_t AudioDeviceModuleIOS::StopRecording() {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized_();
    int32_t result = audio_device_->StopRecording();
    audio_device_buffer_.get()->StopRecording();
    RTC_DLOG(LS_INFO) << "output: " << result;
    RTC_HISTOGRAM_BOOLEAN("WebRTC.Audio.StopRecordingSuccess",
                          static_cast<int>(result == 0));
    return result;
  }

  bool AudioDeviceModuleIOS::Recording() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    return audio_device_->Recording();
  }

  int32_t AudioDeviceModuleIOS::RegisterAudioCallback(
      AudioTransport* audioCallback) {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    return audio_device_buffer_.get()->RegisterAudioCallback(audioCallback);
  }

  int32_t AudioDeviceModuleIOS::PlayoutDelay(uint16_t* delayMS) const {
    CHECKinitialized_();
    uint16_t delay = 0;
    if (audio_device_->PlayoutDelay(delay) == -1) {
      RTC_LOG(LS_ERROR) << "failed to retrieve the playout delay";
      return -1;
    }
    *delayMS = delay;
    return 0;
  }

  bool AudioDeviceModuleIOS::BuiltInAECIsAvailable() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    bool isAvailable = audio_device_->BuiltInAECIsAvailable();
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return isAvailable;
  }

  int32_t AudioDeviceModuleIOS::EnableBuiltInAEC(bool enable) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    CHECKinitialized_();
    int32_t ok = audio_device_->EnableBuiltInAEC(enable);
    RTC_DLOG(LS_INFO) << "output: " << ok;
    return ok;
  }

  bool AudioDeviceModuleIOS::BuiltInAGCIsAvailable() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    bool isAvailable = audio_device_->BuiltInAGCIsAvailable();
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return isAvailable;
  }

  int32_t AudioDeviceModuleIOS::EnableBuiltInAGC(bool enable) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    CHECKinitialized_();
    int32_t ok = audio_device_->EnableBuiltInAGC(enable);
    RTC_DLOG(LS_INFO) << "output: " << ok;
    return ok;
  }

  bool AudioDeviceModuleIOS::BuiltInNSIsAvailable() const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    CHECKinitialized__BOOL();
    bool isAvailable = audio_device_->BuiltInNSIsAvailable();
    RTC_DLOG(LS_INFO) << "output: " << isAvailable;
    return isAvailable;
  }

  int32_t AudioDeviceModuleIOS::EnableBuiltInNS(bool enable) {
    RTC_DLOG(LS_INFO) << __FUNCTION__ << "(" << enable << ")";
    CHECKinitialized_();
    int32_t ok = audio_device_->EnableBuiltInNS(enable);
    RTC_DLOG(LS_INFO) << "output: " << ok;
    return ok;
  }

  int32_t AudioDeviceModuleIOS::GetPlayoutUnderrunCount() const {
    // Don't log here, as this method can be called very often.
    CHECKinitialized_();
    int32_t ok = audio_device_->GetPlayoutUnderrunCount();
    return ok;
  }

#if defined(WEBRTC_IOS)
  int AudioDeviceModuleIOS::GetPlayoutAudioParameters(
      AudioParameters* params) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    int r = audio_device_->GetPlayoutAudioParameters(params);
    RTC_DLOG(LS_INFO) << "output: " << r;
    return r;
  }

  int AudioDeviceModuleIOS::GetRecordAudioParameters(
      AudioParameters* params) const {
    RTC_DLOG(LS_INFO) << __FUNCTION__;
    int r = audio_device_->GetRecordAudioParameters(params);
    RTC_DLOG(LS_INFO) << "output: " << r;
    return r;
  }
#endif  // WEBRTC_IOS
}
}
