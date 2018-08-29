/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CAPTURE_WINDOWS_DEVICE_INFO_MF_H_
#define MODULES_VIDEO_CAPTURE_WINDOWS_DEVICE_INFO_MF_H_

#include "modules/video_capture/device_info_impl.h"

namespace webrtc {
namespace videocapturemodule {

// Provides video capture device information using the Media Foundation API.
class DeviceInfoMF : public DeviceInfoImpl {
 public:
  DeviceInfoMF();
  ~DeviceInfoMF() override;

  int32_t Init() override;
  uint32_t NumberOfDevices() override;

  int32_t GetDeviceName(uint32_t deviceNumber,
                        char* deviceNameUTF8,
                        uint32_t deviceNameLength,
                        char* deviceUniqueIdUTF8,
                        uint32_t deviceUniqueIdUTF8Length,
                        char* productUniqueIdUTF8,
                        uint32_t productUniqueIdUTF8Length) override;

  int32_t DisplayCaptureSettingsDialogBox(const char* deviceUniqueIdUTF8,
                                          const char* dialogTitleUTF8,
                                          void* parentWindow,
                                          uint32_t positionX,
                                          uint32_t positionY) override;
};

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // MODULES_VIDEO_CAPTURE_WINDOWS_DEVICE_INFO_MF_H_
