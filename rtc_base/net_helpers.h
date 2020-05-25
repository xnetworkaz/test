/*
 *  Copyright 2008 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_NET_HELPERS_H_
#define RTC_BASE_NET_HELPERS_H_

#if defined(WEBRTC_POSIX)
#include <sys/socket.h>
#elif WEBRTC_WIN
#include <winsock2.h>  // NOLINT
#endif

#include <vector>

#include "api/scoped_refptr.h"
#include "rtc_base/async_resolver_interface.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/event.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/ref_count.h"
#include "rtc_base/ref_counter.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/synchronization/sequence_checker.h"
#include "rtc_base/system/rtc_export.h"
#include "rtc_base/thread_annotations.h"

namespace rtc {

// AsyncResolver will perform async DNS resolution, signaling the result on
// the SignalDone from AsyncResolverInterface when the operation completes.
//
// This class is thread-compatible, and all methods and destruction needs to
// happen from the same rtc::Thread.
class RTC_EXPORT AsyncResolver : public AsyncResolverInterface {
 public:
  AsyncResolver();
  ~AsyncResolver() override;

  void Start(const SocketAddress& addr) override;
  bool GetResolvedAddress(int family, SocketAddress* addr) const override;
  int GetError() const override;
  void Destroy(bool wait) override;

  const std::vector<IPAddress>& addresses() const;

 private:
  class Ticket : public rtc::RefCountInterface {
   public:
    explicit Ticket(Event* activity_done);

    bool StartActivity();
    void CompleteActivity();

   private:
    Event* activity_done_;
    webrtc::webrtc_impl::RefCounter ref_count_{1};
  };

  SocketAddress addr_ RTC_GUARDED_BY(sequence_checker_);
  CriticalSection mu_;
  std::vector<IPAddress> addresses_ RTC_GUARDED_BY(mu_);
  int error_ RTC_GUARDED_BY(mu_);

  Event async_activity_done_;
  scoped_refptr<Ticket> ticket_;

  webrtc::SequenceChecker sequence_checker_;
};

// rtc namespaced wrappers for inet_ntop and inet_pton so we can avoid
// the windows-native versions of these.
const char* inet_ntop(int af, const void* src, char* dst, socklen_t size);
int inet_pton(int af, const char* src, void* dst);

bool HasIPv4Enabled();
bool HasIPv6Enabled();
}  // namespace rtc

#endif  // RTC_BASE_NET_HELPERS_H_
