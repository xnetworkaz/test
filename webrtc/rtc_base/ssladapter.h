/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_RTC_BASE_SSLADAPTER_H_
#define WEBRTC_RTC_BASE_SSLADAPTER_H_

#include "webrtc/rtc_base/asyncsocket.h"
#include "webrtc/rtc_base/sslstreamadapter.h"

namespace rtc {

class SSLAdapter;

// Class for creating SSL adapters with shared state, e.g., a session cache,
// which allows clients to resume SSL sessions to previously-contacted hosts.
// Clients should create the factory using Create(), set up the factory as
// needed using SetMode, and then call CreateAdapter to create adapters when
// needed.
class SSLAdapterFactory {
 public:
  virtual ~SSLAdapterFactory() {}
  // Specifies whether TLS or DTLS is to be used for the SSL adapters.
  virtual void SetMode(SSLMode mode) = 0;
  // Creates a new SSL adapter, but from a shared context.
  virtual SSLAdapter* CreateAdapter(AsyncSocket* socket) = 0;

  static SSLAdapterFactory* Create();
};

// Class that abstracts a client-to-server SSL session. It can be created
// standalone, via SSLAdapter::Create, or through a factory as described above,
// in which case it will share state with other SSLAdapters created from the
// same factory.
// After creation, call StartSSL to initiate the SSL handshake to the server.
class SSLAdapter : public AsyncSocketAdapter {
 public:
  explicit SSLAdapter(AsyncSocket* socket) : AsyncSocketAdapter(socket) {}

  // Methods that control server certificate verification, used in unit tests.
  // Do not call these methods in production code.
  bool ignore_bad_cert() const { return ignore_bad_cert_; }
  void set_ignore_bad_cert(bool ignore) { ignore_bad_cert_ = ignore; }

  // Do DTLS or TLS (default is TLS, if unspecified)
  virtual void SetMode(SSLMode mode) = 0;

  // StartSSL returns 0 if successful.
  // If StartSSL is called while the socket is closed or connecting, the SSL
  // negotiation will begin as soon as the socket connects.
  // TODO(juberti): Remove |restartable|.
  virtual int StartSSL(const char* hostname, bool restartable = false) = 0;

  // When an SSLAdapterFactory is used, an SSLAdapter may be used to resume
  // a previous SSL session, which results in an abbreviated handshake.
  // This method, if called after SSL has been established for this adapter,
  // indicates whether the current session is a resumption of a previous
  // session.
  virtual bool IsResumedSession() = 0;

  // When called after SSL has been established,
  // returns if the session was resumed or not.
  virtual bool IsResumedSession() = 0;

  // Create the default SSL adapter for this platform. On failure, returns null
  // and deletes |socket|. Otherwise, the returned SSLAdapter takes ownership
  // of |socket|.
  static SSLAdapter* Create(AsyncSocket* socket);

 private:
  // If true, the server certificate need not match the configured hostname.
  bool ignore_bad_cert_ = false;
};

///////////////////////////////////////////////////////////////////////////////

typedef bool (*VerificationCallback)(void* cert);

// Call this on the main thread, before using SSL.
// Call CleanupSSLThread when finished with SSL.
bool InitializeSSL(VerificationCallback callback = nullptr);

// Call to initialize additional threads.
bool InitializeSSLThread();

// Call to cleanup additional threads, and also the main thread.
bool CleanupSSL();

}  // namespace rtc

#endif  // WEBRTC_RTC_BASE_SSLADAPTER_H_
