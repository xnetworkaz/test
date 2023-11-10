/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/dtls_transport.h"

#include <utility>

#include "absl/types/optional.h"
#include "api/dtls_transport_interface.h"
#include "api/make_ref_counted.h"
#include "api/sequence_checker.h"
#include "pc/ice_transport.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_stream_adapter.h"

namespace webrtc {
namespace {
using cricket::DtlsTransportInternal;

absl::optional<DtlsTransportTlsRole> GetDtlsRole(DtlsTransportInternal* t) {
  rtc::SSLRole ssl_role;
  if (!t->GetDtlsRole(&ssl_role))
    return absl::nullopt;
  return ssl_role == rtc::SSL_CLIENT ? DtlsTransportTlsRole::kClient
                                     : DtlsTransportTlsRole::kServer;
}

absl::optional<int> GetTlsVersion(DtlsTransportInternal* t) {
  int version = 0;
  return t->GetSslVersionBytes(&version) ? version : absl::optional<int>();
}

absl::optional<int> GetSslCipherSuite(DtlsTransportInternal* t) {
  int suite = 0;
  return t->GetSslCipherSuite(&suite) ? suite : absl::optional<int>();
}

absl::optional<int> GetSrtpCryptoSuite(DtlsTransportInternal* t) {
  int suite = 0;
  return t->GetSrtpCryptoSuite(&suite) ? suite : absl::optional<int>();
}
}  // namespace

// Implementation of DtlsTransportInterface
DtlsTransport::DtlsTransport(
    std::unique_ptr<cricket::DtlsTransportInternal> internal)
    : owner_thread_(rtc::Thread::Current()),
      internal_dtls_transport_(std::move(internal)),
      ice_transport_(rtc::make_ref_counted<IceTransportWithPointer>(
          internal_dtls_transport_->ice_transport())),
      info_(internal_dtls_transport_->dtls_state(),
            GetDtlsRole(internal_dtls_transport_.get()),
            GetTlsVersion(internal_dtls_transport_.get()),
            GetSslCipherSuite(internal_dtls_transport_.get()),
            GetSrtpCryptoSuite(internal_dtls_transport_.get()),
            internal_dtls_transport_->GetRemoteSSLCertChain()) {
  internal_dtls_transport_->SubscribeDtlsTransportState(
      [this](cricket::DtlsTransportInternal* transport,
             DtlsTransportState state) {
        OnInternalDtlsState(transport, state);
      });
  internal_dtls_transport_->SubscribeDtlsRole([this](rtc::SSLRole role) {
    RTC_DCHECK_RUN_ON(owner_thread_);
    {
      MutexLock lock(&lock_);
      info_.set_role(role == rtc::SSL_CLIENT ? DtlsTransportTlsRole::kClient
                                             : DtlsTransportTlsRole::kServer);
    }
    if (observer_) {
      observer_->OnStateChange(Information());
    }
  });
}

DtlsTransport::~DtlsTransport() {
  // We depend on the signaling thread to call Clear() before dropping
  // its last reference to this object.
  RTC_DCHECK(owner_thread_->IsCurrent() || !internal_dtls_transport_);
}

DtlsTransportInformation DtlsTransport::Information() {
  MutexLock lock(&lock_);
  return info_;
}

void DtlsTransport::RegisterObserver(DtlsTransportObserverInterface* observer) {
  RTC_DCHECK_RUN_ON(owner_thread_);
  RTC_DCHECK(observer);
  observer_ = observer;
}

void DtlsTransport::UnregisterObserver() {
  RTC_DCHECK_RUN_ON(owner_thread_);
  observer_ = nullptr;
}

rtc::scoped_refptr<IceTransportInterface> DtlsTransport::ice_transport() {
  return ice_transport_;
}

// Internal functions
void DtlsTransport::Clear() {
  RTC_DCHECK_RUN_ON(owner_thread_);
  RTC_DCHECK(internal());
  const DtlsTransportState state = internal()->dtls_state();
  bool must_send_event = (state != DtlsTransportState::kClosed);
  // The destructor of cricket::DtlsTransportInternal calls back
  // into DtlsTransport, so we can't hold the lock while releasing.
  std::unique_ptr<cricket::DtlsTransportInternal> transport_to_release;
  {
    MutexLock lock(&lock_);
    info_.set_state(DtlsTransportState::kClosed);
    transport_to_release = std::move(internal_dtls_transport_);
    ice_transport_->Clear();
  }
  if (observer_ && must_send_event) {
    observer_->OnStateChange(Information());
  }
}

void DtlsTransport::OnInternalDtlsState(
    cricket::DtlsTransportInternal* transport,
    DtlsTransportState state) {
  RTC_DCHECK_RUN_ON(owner_thread_);
  RTC_DCHECK_EQ(transport, internal());
  RTC_DCHECK_EQ(state, transport->dtls_state());

  {
    MutexLock lock(&lock_);
    info_.set_state(state);
    if (state != DtlsTransportState::kClosed) {
      if (!info_.tls_version()) {
        info_.set_tls_version(GetTlsVersion(transport));
      }
      if (!info_.ssl_cipher_suite()) {
        info_.set_ssl_cipher_suite(GetSslCipherSuite(transport));
      }
      if (!info_.srtp_cipher_suite()) {
        info_.set_srtp_cipher_suite(GetSrtpCryptoSuite(transport));
      }
      if (!info_.remote_ssl_certificates()) {
        info_.set_remote_ssl_certificates(transport->GetRemoteSSLCertChain());
      }
    }
  }

  if (observer_) {
    observer_->OnStateChange(Information());
  }
}

}  // namespace webrtc
