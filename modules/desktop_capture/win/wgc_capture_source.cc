/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/win/wgc_capture_source.h"

#include <windows.graphics.capture.interop.h>
#include <utility>

#include "modules/desktop_capture/win/screen_capture_utils.h"
#include "modules/desktop_capture/win/window_capture_utils.h"
#include "rtc_base/win/get_activation_factory.h"

using Microsoft::WRL::ComPtr;
namespace WGC = ABI::Windows::Graphics::Capture;

namespace webrtc {

WgcCaptureSource::WgcCaptureSource(DesktopCapturer::SourceId source_id)
    : source_id_(source_id) {}
WgcCaptureSource::~WgcCaptureSource() = default;

HRESULT WgcCaptureSource::GetCaptureItem(
    ComPtr<WGC::IGraphicsCaptureItem>* result) {
  HRESULT hr = S_OK;
  if (!item_)
    hr = CreateCaptureItem(&item_);

  *result = item_;
  return hr;
}

bool WgcCaptureSource::IsCapturable() {
  // If we can create a capture item, then we can capture it. Unfortunately,
  // we can't cache this item because it may be created in a different COM
  // apartment than where capture will eventually start from.
  ComPtr<WGC::IGraphicsCaptureItem> item;
  return SUCCEEDED(CreateCaptureItem(&item));
}

WgcCaptureSourceFactory::~WgcCaptureSourceFactory() = default;

WgcWindowSourceFactory::WgcWindowSourceFactory() = default;
WgcWindowSourceFactory::~WgcWindowSourceFactory() = default;

std::unique_ptr<WgcCaptureSource> WgcWindowSourceFactory::CreateCaptureSource(
    DesktopCapturer::SourceId source_id) {
  return std::make_unique<WgcWindowSource>(source_id);
}

WgcScreenSourceFactory::WgcScreenSourceFactory() = default;
WgcScreenSourceFactory::~WgcScreenSourceFactory() = default;

std::unique_ptr<WgcCaptureSource> WgcScreenSourceFactory::CreateCaptureSource(
    DesktopCapturer::SourceId source_id) {
  return std::make_unique<WgcScreenSource>(source_id);
}

WgcWindowSource::WgcWindowSource(DesktopCapturer::SourceId source_id)
    : WgcCaptureSource(source_id) {}
WgcWindowSource::~WgcWindowSource() = default;

bool WgcWindowSource::IsCapturable() {
  if (!IsWindowValidAndVisible(reinterpret_cast<HWND>(GetSourceId())))
    return false;

  return WgcCaptureSource::IsCapturable();
}

HRESULT WgcWindowSource::CreateCaptureItem(
    ComPtr<WGC::IGraphicsCaptureItem>* result) {
  if (!ResolveCoreWinRTDelayload())
    return E_FAIL;

  ComPtr<IGraphicsCaptureItemInterop> interop;
  HRESULT hr = GetActivationFactory<
      IGraphicsCaptureItemInterop,
      RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem>(&interop);
  if (FAILED(hr))
    return hr;

  ComPtr<WGC::IGraphicsCaptureItem> item;
  hr = interop->CreateForWindow(reinterpret_cast<HWND>(GetSourceId()),
                                IID_PPV_ARGS(&item));
  if (FAILED(hr))
    return hr;

  if (!item)
    return E_HANDLE;

  *result = std::move(item);
  return hr;
}

WgcScreenSource::WgcScreenSource(DesktopCapturer::SourceId source_id)
    : WgcCaptureSource(source_id) {}
WgcScreenSource::~WgcScreenSource() = default;

bool WgcScreenSource::IsCapturable() {
  if (!hmonitor_) {
    HMONITOR hmon;
    if (!GetHmonitorFromDeviceIndex(GetSourceId(), &hmon))
      return false;

    hmonitor_ = hmon;
  }

  if (!IsMonitorValid(*hmonitor_))
    return false;

  return WgcCaptureSource::IsCapturable();
}

HRESULT WgcScreenSource::CreateCaptureItem(
    ComPtr<WGC::IGraphicsCaptureItem>* result) {
  if (!hmonitor_)
    return E_ABORT;

  if (!ResolveCoreWinRTDelayload())
    return E_FAIL;

  ComPtr<IGraphicsCaptureItemInterop> interop;
  HRESULT hr = GetActivationFactory<
      IGraphicsCaptureItemInterop,
      RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem>(&interop);
  if (FAILED(hr))
    return hr;

  ComPtr<WGC::IGraphicsCaptureItem> item;
  hr = interop->CreateForMonitor(*hmonitor_, IID_PPV_ARGS(&item));
  if (FAILED(hr))
    return hr;

  if (!item)
    return E_HANDLE;

  *result = std::move(item);
  return hr;
}

}  // namespace webrtc
