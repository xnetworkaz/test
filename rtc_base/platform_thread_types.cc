/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/platform_thread_types.h"

#include "absl/strings/string_view.h"

#if defined(WEBRTC_LINUX)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#if defined(WEBRTC_WIN)
#include "rtc_base/arraysize.h"

// The SetThreadDescription API was brought in version 1607 of Windows 10.
// For compatibility with various versions of winuser and avoid clashing with
// a potentially defined type, we use the RTC_ prefix.
typedef HRESULT(WINAPI* RTC_SetThreadDescription)(HANDLE hThread,
                                                  PCWSTR lpThreadDescription);
#endif

namespace rtc {

PlatformThreadId CurrentThreadId() {
#if defined(WEBRTC_WIN)
  return GetCurrentThreadId();
#elif defined(WEBRTC_POSIX)
#if defined(WEBRTC_MAC) || defined(WEBRTC_IOS)
  return pthread_mach_thread_np(pthread_self());
#elif defined(WEBRTC_ANDROID)
  return gettid();
#elif defined(WEBRTC_FUCHSIA)
  return zx_thread_self();
#elif defined(WEBRTC_LINUX)
  return syscall(__NR_gettid);
#elif defined(__EMSCRIPTEN__)
  return static_cast<PlatformThreadId>(pthread_self());
#else
  // Default implementation for nacl and solaris.
  return reinterpret_cast<PlatformThreadId>(pthread_self());
#endif
#endif  // defined(WEBRTC_POSIX)
}

PlatformThreadRef CurrentThreadRef() {
#if defined(WEBRTC_WIN)
  return GetCurrentThreadId();
#elif defined(WEBRTC_FUCHSIA)
  return zx_thread_self();
#elif defined(WEBRTC_POSIX)
  return pthread_self();
#endif
}

bool IsThreadRefEqual(const PlatformThreadRef& a, const PlatformThreadRef& b) {
#if defined(WEBRTC_WIN) || defined(WEBRTC_FUCHSIA)
  return a == b;
#elif defined(WEBRTC_POSIX)
  return pthread_equal(a, b);
#endif
}

void SetCurrentThreadName(absl::string_view name) {
  std::string name_str = std::string(name);
#if defined(WEBRTC_WIN)
  // The SetThreadDescription API works even if no debugger is attached.
  // The names set with this API also show up in ETW traces. Very handy.
  static auto set_thread_description_func =
      reinterpret_cast<RTC_SetThreadDescription>(::GetProcAddress(
          ::GetModuleHandleA("Kernel32.dll"), "SetThreadDescription"));
  if (set_thread_description_func) {
    // Convert from ASCII to UTF-16.
    wchar_t wide_thread_name[64];
    for (size_t i = 0; i < arraysize(wide_thread_name) - 1; ++i) {
      wide_thread_name[i] = name[i];
      if (wide_thread_name[i] == L'\0')
        break;
    }
    // Guarantee null-termination.
    wide_thread_name[arraysize(wide_thread_name) - 1] = L'\0';
    set_thread_description_func(::GetCurrentThread(), wide_thread_name);
  }

  // For details see:
  // https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
#pragma pack(push, 8)
  struct {
    DWORD dwType;
    LPCSTR szName;
    DWORD dwThreadID;
    DWORD dwFlags;
  } threadname_info = {0x1000, name_str.c_str(), static_cast<DWORD>(-1), 0};
#pragma pack(pop)

#pragma warning(push)
#pragma warning(disable : 6320 6322)
  __try {
    ::RaiseException(0x406D1388, 0, sizeof(threadname_info) / sizeof(ULONG_PTR),
                     reinterpret_cast<ULONG_PTR*>(&threadname_info));
  } __except (EXCEPTION_EXECUTE_HANDLER) {  // NOLINT
  }
#pragma warning(pop)
#elif defined(WEBRTC_LINUX) || defined(WEBRTC_ANDROID)
  prctl(PR_SET_NAME,
        reinterpret_cast<unsigned long>(name_str.c_str()));  // NOLINT
#elif defined(WEBRTC_MAC) || defined(WEBRTC_IOS)
  pthread_setname_np(name_str.c_str());
#endif
}

}  // namespace rtc
