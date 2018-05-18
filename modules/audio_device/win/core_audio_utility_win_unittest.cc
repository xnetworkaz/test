/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_device/win/core_audio_utility_win.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/logging.h"
#include "test/gtest.h"

using Microsoft::WRL::ComPtr;
using webrtc::AudioDeviceName;

namespace webrtc_win {
namespace {

#define ABORT_TEST_IF_NOT(requirements_satisfied)                        \
  do {                                                                   \
    bool fail = false;                                                   \
    if (ShouldAbortTest(requirements_satisfied, #requirements_satisfied, \
                        &fail)) {                                        \
      if (fail)                                                          \
        FAIL();                                                          \
      else                                                               \
        return;                                                          \
    }                                                                    \
  } while (false)

bool ShouldAbortTest(bool requirements_satisfied,
                     const char* requirements_expression,
                     bool* should_fail) {
  if (!requirements_satisfied) {
    RTC_LOG(LS_ERROR) << "Requirement(s) not satisfied ("
                      << requirements_expression << ")";
    // TODO(henrika): improve hard-coded condition to determine if test should
    // fail or be ignored. Could use e.g. a command-line argument here to
    // determine if the test should fail or not.
    *should_fail = false;
    return true;
  }
  *should_fail = false;
  return false;
}

}  // namespace

// CoreAudioUtilityWinTest test fixture.
class CoreAudioUtilityWinTest : public ::testing::Test {
 protected:
  CoreAudioUtilityWinTest() {
    // We must initialize the COM library on a thread before we calling any of
    // the library functions. All COM functions will return CO_E_NOTINITIALIZED
    // otherwise.
    EXPECT_TRUE(com_init_.Succeeded());

    // Configure logging.
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    rtc::LogMessage::LogTimestamps();
    rtc::LogMessage::LogThreads();
  }

  virtual ~CoreAudioUtilityWinTest() {}

  bool DevicesAvailable() {
    return core_audio_utility::IsSupported() &&
           core_audio_utility::NumberOfActiveDevices(eCapture) > 0 &&
           core_audio_utility::NumberOfActiveDevices(eRender) > 0;
  }

 private:
  ScopedCOMInitializer com_init_;
};

TEST_F(CoreAudioUtilityWinTest, NumberOfActiveDevices) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  int render_devices = core_audio_utility::NumberOfActiveDevices(eRender);
  EXPECT_GT(render_devices, 0);
  int capture_devices = core_audio_utility::NumberOfActiveDevices(eCapture);
  EXPECT_GT(capture_devices, 0);
  int total_devices = core_audio_utility::NumberOfActiveDevices(eAll);
  EXPECT_EQ(total_devices, render_devices + capture_devices);
}

TEST_F(CoreAudioUtilityWinTest, CreateDeviceEnumerator) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  ComPtr<IMMDeviceEnumerator> enumerator =
      core_audio_utility::CreateDeviceEnumerator();
  EXPECT_TRUE(enumerator.Get());
}

TEST_F(CoreAudioUtilityWinTest, GetDefaultInputDeviceID) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  std::string default_device_id = core_audio_utility::GetDefaultInputDeviceID();
  EXPECT_FALSE(default_device_id.empty());
}

TEST_F(CoreAudioUtilityWinTest, GetDefaultOutputDeviceID) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  std::string default_device_id =
      core_audio_utility::GetDefaultOutputDeviceID();
  EXPECT_FALSE(default_device_id.empty());
}

TEST_F(CoreAudioUtilityWinTest, GetCommunicationsInputDeviceID) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  std::string default_device_id =
      core_audio_utility::GetCommunicationsInputDeviceID();
  EXPECT_FALSE(default_device_id.empty());
}

TEST_F(CoreAudioUtilityWinTest, GetCommunicationsOutputDeviceID) {
  ABORT_TEST_IF_NOT(DevicesAvailable());
  std::string default_device_id =
      core_audio_utility::GetCommunicationsOutputDeviceID();
  EXPECT_FALSE(default_device_id.empty());
}

TEST_F(CoreAudioUtilityWinTest, CreateDefaultDevice) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  struct {
    EDataFlow flow;
    ERole role;
  } data[] = {{eRender, eConsole},         {eRender, eCommunications},
              {eRender, eMultimedia},      {eCapture, eConsole},
              {eCapture, eCommunications}, {eCapture, eMultimedia}};

  // Create default devices for all flow/role combinations above.
  ComPtr<IMMDevice> audio_device;
  for (size_t i = 0; i < arraysize(data); ++i) {
    audio_device = core_audio_utility::CreateDevice(
        AudioDeviceName::kDefaultDeviceId, data[i].flow, data[i].role);
    EXPECT_TRUE(audio_device.Get());
    EXPECT_EQ(data[i].flow,
              core_audio_utility::GetDataFlow(audio_device.Get()));
  }

  // Only eRender and eCapture are allowed as flow parameter.
  audio_device = core_audio_utility::CreateDevice(
      AudioDeviceName::kDefaultDeviceId, eAll, eConsole);
  EXPECT_FALSE(audio_device.Get());
}

TEST_F(CoreAudioUtilityWinTest, CreateDevice) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  // Get name and ID of default device used for playback.
  ComPtr<IMMDevice> default_render_device = core_audio_utility::CreateDevice(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole);
  AudioDeviceName default_render_name =
      core_audio_utility::GetDeviceName(default_render_device.Get());
  EXPECT_TRUE(default_render_name.IsValid());

  // Use the unique ID as input to CreateDevice() and create a corresponding
  // IMMDevice. The data-flow direction and role parameters are ignored for
  // this scenario.
  ComPtr<IMMDevice> audio_device = core_audio_utility::CreateDevice(
      default_render_name.unique_id, EDataFlow(), ERole());
  EXPECT_TRUE(audio_device.Get());

  // Verify that the two IMMDevice interfaces represents the same endpoint
  // by comparing their unique IDs.
  AudioDeviceName device_name =
      core_audio_utility::GetDeviceName(audio_device.Get());
  EXPECT_EQ(default_render_name.unique_id, device_name.unique_id);
}

TEST_F(CoreAudioUtilityWinTest, GetDefaultDeviceName) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  struct {
    EDataFlow flow;
    ERole role;
  } data[] = {{eRender, eConsole},
              {eRender, eCommunications},
              {eCapture, eConsole},
              {eCapture, eCommunications}};

  // Get name and ID of default devices for all flow/role combinations above.
  ComPtr<IMMDevice> audio_device;
  AudioDeviceName device_name;
  for (size_t i = 0; i < arraysize(data); ++i) {
    audio_device = core_audio_utility::CreateDevice(
        AudioDeviceName::kDefaultDeviceId, data[i].flow, data[i].role);
    device_name = core_audio_utility::GetDeviceName(audio_device.Get());
    EXPECT_TRUE(device_name.IsValid());
  }
}

TEST_F(CoreAudioUtilityWinTest, GetFriendlyName) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  // Get name and ID of default device used for recording.
  ComPtr<IMMDevice> audio_device = core_audio_utility::CreateDevice(
      AudioDeviceName::kDefaultDeviceId, eCapture, eConsole);
  AudioDeviceName device_name =
      core_audio_utility::GetDeviceName(audio_device.Get());
  EXPECT_TRUE(device_name.IsValid());

  // Use unique ID as input to GetFriendlyName() and compare the result
  // with the already obtained friendly name for the default capture device.
  std::string friendly_name = core_audio_utility::GetFriendlyName(
      device_name.unique_id, eCapture, eConsole);
  EXPECT_EQ(friendly_name, device_name.device_name);

  // Same test as above but for playback.
  audio_device = core_audio_utility::CreateDevice(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole);
  device_name = core_audio_utility::GetDeviceName(audio_device.Get());
  friendly_name = core_audio_utility::GetFriendlyName(device_name.unique_id,
                                                      eRender, eConsole);
  EXPECT_EQ(friendly_name, device_name.device_name);
}

TEST_F(CoreAudioUtilityWinTest, GetInputDeviceNames) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  webrtc::AudioDeviceNames device_names;
  EXPECT_TRUE(core_audio_utility::GetInputDeviceNames(&device_names));
  // Number of elements in the list should be two more than the number of
  // active devices since we always add default and default communication
  // devices on index 0 and 1.
  EXPECT_EQ(static_cast<int>(device_names.size()),
            2 + core_audio_utility::NumberOfActiveDevices(eCapture));
}

TEST_F(CoreAudioUtilityWinTest, GetOutputDeviceNames) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  webrtc::AudioDeviceNames device_names;
  EXPECT_TRUE(core_audio_utility::GetOutputDeviceNames(&device_names));
  // Number of elements in the list should be two more than the number of
  // active devices since we always add default and default communication
  // devices on index 0 and 1.
  EXPECT_EQ(static_cast<int>(device_names.size()),
            2 + core_audio_utility::NumberOfActiveDevices(eRender));
}

TEST_F(CoreAudioUtilityWinTest, CreateClient) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  // Obtain reference to an IAudioClient interface for a default audio endpoint
  // device specified by two different data flows and the |eConsole| role.
  for (size_t i = 0; i < arraysize(data); ++i) {
    ComPtr<IAudioClient> client = core_audio_utility::CreateClient(
        AudioDeviceName::kDefaultDeviceId, data[i], eConsole);
    EXPECT_TRUE(client.Get());
  }
}

// TODO(henrik): enable when support for Windows version querying is added.
TEST_F(CoreAudioUtilityWinTest, DISABLED_CreateClient2) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  // Obtain reference to an IAudioClient2 interface for a default audio endpoint
  // device specified by two different data flows and the |eConsole| role.
  for (size_t i = 0; i < arraysize(data); ++i) {
    ComPtr<IAudioClient2> client = core_audio_utility::CreateClient2(
        AudioDeviceName::kDefaultDeviceId, data[i], eConsole);
    EXPECT_TRUE(client.Get());
  }
}

TEST_F(CoreAudioUtilityWinTest, SetClientProperties) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  ComPtr<IAudioClient2> client = core_audio_utility::CreateClient2(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole);
  EXPECT_TRUE(client.Get());

  EXPECT_TRUE(SUCCEEDED(core_audio_utility::SetClientProperties(client.Get())));
}

TEST_F(CoreAudioUtilityWinTest, GetSharedModeMixFormat) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  ComPtr<IAudioClient> client = core_audio_utility::CreateClient(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole);
  EXPECT_TRUE(client.Get());

  // Perform a simple sanity test of the acquired format structure.
  WAVEFORMATPCMEX format;
  EXPECT_TRUE(SUCCEEDED(
      core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));
  EXPECT_GE(format.Format.nChannels, 1);
  EXPECT_GE(format.Format.nSamplesPerSec, 8000u);
  EXPECT_GE(format.Format.wBitsPerSample, 16);
  EXPECT_GE(format.Samples.wValidBitsPerSample, 16);
  EXPECT_EQ(format.Format.wFormatTag, WAVE_FORMAT_EXTENSIBLE);
}

TEST_F(CoreAudioUtilityWinTest, IsFormatSupported) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  // Create a default render client.
  ComPtr<IAudioClient> client = core_audio_utility::CreateClient(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole);
  EXPECT_TRUE(client.Get());

  // Get the default, shared mode, mixing format.
  WAVEFORMATEXTENSIBLE format;
  EXPECT_TRUE(SUCCEEDED(
      core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));

  // In shared mode, the audio engine always supports the mix format.
  EXPECT_TRUE(core_audio_utility::IsFormatSupported(
      client.Get(), AUDCLNT_SHAREMODE_SHARED, &format));

  // Use an invalid format and verify that it is not supported.
  format.Format.nSamplesPerSec += 1;
  EXPECT_FALSE(core_audio_utility::IsFormatSupported(
      client.Get(), AUDCLNT_SHAREMODE_SHARED, &format));
}

TEST_F(CoreAudioUtilityWinTest, GetDevicePeriod) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  // Verify that the device periods are valid for the default render and
  // capture devices.
  for (size_t i = 0; i < arraysize(data); ++i) {
    ComPtr<IAudioClient> client;
    REFERENCE_TIME shared_time_period = 0;
    REFERENCE_TIME exclusive_time_period = 0;
    client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                              data[i], eConsole);
    EXPECT_TRUE(client.Get());
    EXPECT_TRUE(SUCCEEDED(core_audio_utility::GetDevicePeriod(
        client.Get(), AUDCLNT_SHAREMODE_SHARED, &shared_time_period)));
    EXPECT_GT(shared_time_period, 0);
    EXPECT_TRUE(SUCCEEDED(core_audio_utility::GetDevicePeriod(
        client.Get(), AUDCLNT_SHAREMODE_EXCLUSIVE, &exclusive_time_period)));
    EXPECT_GT(exclusive_time_period, 0);
    EXPECT_LE(exclusive_time_period, shared_time_period);
  }
}

TEST_F(CoreAudioUtilityWinTest, GetPreferredAudioParameters) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  // Verify that the preferred audio parameters are OK for the default render
  // and capture devices.
  for (size_t i = 0; i < arraysize(data); ++i) {
    webrtc::AudioParameters params;
    EXPECT_TRUE(SUCCEEDED(core_audio_utility::GetPreferredAudioParameters(
        AudioDeviceName::kDefaultDeviceId, data[i] == eRender, &params)));
    EXPECT_TRUE(params.is_valid());
    EXPECT_TRUE(params.is_complete());
  }

  // Verify that the preferred audio parameters are OK for the default
  // communication devices.
  for (size_t i = 0; i < arraysize(data); ++i) {
    webrtc::AudioParameters params;
    EXPECT_TRUE(SUCCEEDED(core_audio_utility::GetPreferredAudioParameters(
        AudioDeviceName::kDefaultCommunicationsDeviceId, data[i] == eRender,
        &params)));
    EXPECT_TRUE(params.is_valid());
    EXPECT_TRUE(params.is_complete());
  }
}

TEST_F(CoreAudioUtilityWinTest, SharedModeInitialize) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  ComPtr<IAudioClient> client;
  client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                            eRender, eConsole);
  EXPECT_TRUE(client.Get());

  WAVEFORMATPCMEX format;
  EXPECT_TRUE(SUCCEEDED(
      core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));

  // Perform a shared-mode initialization without event-driven buffer handling.
  uint32_t endpoint_buffer_size = 0;
  HRESULT hr = core_audio_utility::SharedModeInitialize(
      client.Get(), &format, nullptr, &endpoint_buffer_size);
  EXPECT_TRUE(SUCCEEDED(hr));
  EXPECT_GT(endpoint_buffer_size, 0u);

  // It is only possible to create a client once.
  hr = core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                                &endpoint_buffer_size);
  EXPECT_FALSE(SUCCEEDED(hr));
  EXPECT_EQ(hr, AUDCLNT_E_ALREADY_INITIALIZED);

  // Verify that it is possible to reinitialize the client after releasing it
  // and then creating a new client.
  client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                            eRender, eConsole);
  EXPECT_TRUE(client.Get());
  hr = core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                                &endpoint_buffer_size);
  EXPECT_TRUE(SUCCEEDED(hr));
  EXPECT_GT(endpoint_buffer_size, 0u);

  // Use a non-supported format and verify that initialization fails.
  // A simple way to emulate an invalid format is to use the shared-mode
  // mixing format and modify the preferred sample rate.
  client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                            eRender, eConsole);
  EXPECT_TRUE(client.Get());
  format.Format.nSamplesPerSec = format.Format.nSamplesPerSec + 1;
  EXPECT_FALSE(core_audio_utility::IsFormatSupported(
      client.Get(), AUDCLNT_SHAREMODE_SHARED, &format));
  hr = core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                                &endpoint_buffer_size);
  EXPECT_TRUE(FAILED(hr));
  EXPECT_EQ(hr, E_INVALIDARG);

  // Finally, perform a shared-mode initialization using event-driven buffer
  // handling. The event handle will be signaled when an audio buffer is ready
  // to be processed by the client (not verified here). The event handle should
  // be in the non-signaled state.
  ScopedHandle event_handle(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
  client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                            eRender, eConsole);
  EXPECT_TRUE(client.Get());
  EXPECT_TRUE(SUCCEEDED(
      core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));
  EXPECT_TRUE(core_audio_utility::IsFormatSupported(
      client.Get(), AUDCLNT_SHAREMODE_SHARED, &format));
  hr = core_audio_utility::SharedModeInitialize(
      client.Get(), &format, event_handle, &endpoint_buffer_size);
  EXPECT_TRUE(SUCCEEDED(hr));
  EXPECT_GT(endpoint_buffer_size, 0u);
}

TEST_F(CoreAudioUtilityWinTest, CreateRenderAndCaptureClients) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  WAVEFORMATPCMEX format;
  uint32_t endpoint_buffer_size = 0;

  for (size_t i = 0; i < arraysize(data); ++i) {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render_client;
    ComPtr<IAudioCaptureClient> capture_client;

    // Create a default client for the given data-flow direction.
    client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                              data[i], eConsole);
    EXPECT_TRUE(client.Get());
    EXPECT_TRUE(SUCCEEDED(
        core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));
    if (data[i] == eRender) {
      // It is not possible to create a render client using an unitialized
      // client interface.
      render_client = core_audio_utility::CreateRenderClient(client.Get());
      EXPECT_FALSE(render_client.Get());

      // Do a proper initialization and verify that it works this time.
      core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                               &endpoint_buffer_size);
      render_client = core_audio_utility::CreateRenderClient(client.Get());
      EXPECT_TRUE(render_client.Get());
      EXPECT_GT(endpoint_buffer_size, 0u);
    } else if (data[i] == eCapture) {
      // It is not possible to create a capture client using an unitialized
      // client interface.
      capture_client = core_audio_utility::CreateCaptureClient(client.Get());
      EXPECT_FALSE(capture_client.Get());

      // Do a proper initialization and verify that it works this time.
      core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                               &endpoint_buffer_size);
      capture_client = core_audio_utility::CreateCaptureClient(client.Get());
      EXPECT_TRUE(capture_client.Get());
      EXPECT_GT(endpoint_buffer_size, 0u);
    }
  }
}

TEST_F(CoreAudioUtilityWinTest, CreateAudioClock) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  EDataFlow data[] = {eRender, eCapture};

  WAVEFORMATPCMEX format;
  uint32_t endpoint_buffer_size = 0;

  for (size_t i = 0; i < arraysize(data); ++i) {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioClock> audio_clock;

    // Create a default client for the given data-flow direction.
    client = core_audio_utility::CreateClient(AudioDeviceName::kDefaultDeviceId,
                                              data[i], eConsole);
    EXPECT_TRUE(client.Get());
    EXPECT_TRUE(SUCCEEDED(
        core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));

    // It is not possible to create an audio clock using an unitialized client
    // interface.
    audio_clock = core_audio_utility::CreateAudioClock(client.Get());
    EXPECT_FALSE(audio_clock.Get());

    // Do a proper initialization and verify that it works this time.
    core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                             &endpoint_buffer_size);
    audio_clock = core_audio_utility::CreateAudioClock(client.Get());
    EXPECT_TRUE(audio_clock.Get());
    EXPECT_GT(endpoint_buffer_size, 0u);

    // Use the audio clock and verify that querying the device frequency works.
    UINT64 frequency = 0;
    EXPECT_TRUE(SUCCEEDED(audio_clock->GetFrequency(&frequency)));
    EXPECT_GT(frequency, 0u);
  }
}

TEST_F(CoreAudioUtilityWinTest, FillRenderEndpointBufferWithSilence) {
  ABORT_TEST_IF_NOT(DevicesAvailable());

  // Create default clients using the default mixing format for shared mode.
  ComPtr<IAudioClient> client(core_audio_utility::CreateClient(
      AudioDeviceName::kDefaultDeviceId, eRender, eConsole));
  EXPECT_TRUE(client.Get());

  WAVEFORMATPCMEX format;
  uint32_t endpoint_buffer_size = 0;
  EXPECT_TRUE(SUCCEEDED(
      core_audio_utility::GetSharedModeMixFormat(client.Get(), &format)));
  core_audio_utility::SharedModeInitialize(client.Get(), &format, nullptr,
                                           &endpoint_buffer_size);
  EXPECT_GT(endpoint_buffer_size, 0u);

  ComPtr<IAudioRenderClient> render_client(
      core_audio_utility::CreateRenderClient(client.Get()));
  EXPECT_TRUE(render_client.Get());

  // The endpoint audio buffer should not be filled up by default after being
  // created.
  UINT32 num_queued_frames = 0;
  client->GetCurrentPadding(&num_queued_frames);
  EXPECT_EQ(num_queued_frames, 0u);

  // Fill it up with zeros and verify that the buffer is full.
  // It is not possible to verify that the actual data consists of zeros
  // since we can't access data that has already been sent to the endpoint
  // buffer.
  EXPECT_TRUE(core_audio_utility::FillRenderEndpointBufferWithSilence(
      client.Get(), render_client.Get()));
  client->GetCurrentPadding(&num_queued_frames);
  EXPECT_EQ(num_queued_frames, endpoint_buffer_size);
}

}  // namespace webrtc_win
