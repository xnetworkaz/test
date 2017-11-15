/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_OPENSSLSTREAMADAPTER_H_
#define RTC_BASE_OPENSSLSTREAMADAPTER_H_

#include <string>
#include <memory>
#include <vector>

#include "rtc_base/buffer.h"
#include "rtc_base/opensslidentity.h"
#include "rtc_base/sslstreamadapter.h"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_cipher_st SSL_CIPHER;
typedef struct x509_store_ctx_st X509_STORE_CTX;

namespace rtc {

// This class was written with OpenSSLAdapter (a socket adapter) as a
// starting point. It has similar structure and functionality, but uses a
// "peer-to-peer" mode, verifying the peer's certificate using a digest
// sent over a secure signaling channel.
//
// Static methods to initialize and deinit the SSL library are in
// OpenSSLAdapter. These should probably be moved out to a neutral class.
//
// In a few cases I have factored out some OpenSSLAdapter code into static
// methods so it can be reused from this class. Eventually that code should
// probably be moved to a common support class. Unfortunately there remain a
// few duplicated sections of code. I have not done more restructuring because
// I did not want to affect existing code that uses OpenSSLAdapter.
//
// This class does not support the SSL connection restart feature present in
// OpenSSLAdapter. I am not entirely sure how the feature is useful and I am
// not convinced that it works properly.
//
// This implementation is careful to disallow data exchange after an SSL error,
// and it has an explicit SSL_CLOSED state. It should not be possible to send
// any data in clear after one of the StartSSL methods has been called.

// Look in sslstreamadapter.h for documentation of the methods.

class OpenSSLIdentity;

///////////////////////////////////////////////////////////////////////////////

class OpenSSLStreamAdapter : public SSLStreamAdapter {
 public:
  explicit OpenSSLStreamAdapter(StreamInterface* stream);
  ~OpenSSLStreamAdapter() override;

  void SetIdentity(SSLIdentity* identity) override;

  // Default argument is for compatibility
  void SetServerRole(SSLRole role = SSL_SERVER) override;
  bool SetPeerCertificateDigest(
      const std::string& digest_alg,
      const unsigned char* digest_val,
      size_t digest_len,
      SSLPeerCertificateDigestError* error = nullptr) override;

  std::unique_ptr<SSLCertificate> GetPeerCertificate() const override;

  std::unique_ptr<SSLCertChain> GetPeerSSLCertChain() const override;

  // Goes from state SSL_NONE to either SSL_CONNECTING or SSL_WAIT, depending
  // on whether the underlying stream is already open or not.
  int StartSSL() override;
  void SetMode(SSLMode mode) override;
  void SetMaxProtocolVersion(SSLProtocolVersion version) override;
  void SetInitialRetransmissionTimeout(int timeout_ms) override;

  StreamResult Read(void* data,
                    size_t data_len,
                    size_t* read,
                    int* error) override;
  StreamResult Write(const void* data,
                     size_t data_len,
                     size_t* written,
                     int* error) override;
  void Close() override;
  StreamState GetState() const override;

  // TODO(guoweis): Move this away from a static class method.
  static std::string SslCipherSuiteToName(int crypto_suite);

  bool GetSslCipherSuite(int* cipher) override;

  int GetSslVersion() const override;

  // Key Extractor interface
  bool ExportKeyingMaterial(const std::string& label,
                            const uint8_t* context,
                            size_t context_len,
                            bool use_context,
                            uint8_t* result,
                            size_t result_len) override;

  // DTLS-SRTP interface
  bool SetDtlsSrtpCryptoSuites(const std::vector<int>& crypto_suites) override;
  bool GetDtlsSrtpCryptoSuite(int* crypto_suite) override;

  bool IsTlsConnected() override;

  // Capabilities interfaces.
  static bool IsBoringSsl();

  static bool IsAcceptableCipher(int cipher, KeyType key_type);
  static bool IsAcceptableCipher(const std::string& cipher, KeyType key_type);

  // Use our timeutils.h source of timing in BoringSSL, allowing us to test
  // using a fake clock.
  static void enable_time_callback_for_testing();

 protected:
  void OnEvent(StreamInterface* stream, int events, int err) override;

 private:
  enum SSLState {
    // Before calling one of the StartSSL methods, data flows
    // in clear text.
    SSL_NONE,
    SSL_WAIT,  // waiting for the stream to open to start SSL negotiation
    SSL_CONNECTING,  // SSL negotiation in progress
    SSL_CONNECTED,  // SSL stream successfully established
    SSL_ERROR,  // some SSL error occurred, stream is closed
    SSL_CLOSED  // Clean close
  };

  enum { MSG_TIMEOUT = MSG_MAX+1};

  // The following three methods return 0 on success and a negative
  // error code on failure. The error code may be from OpenSSL or -1
  // on some other error cases, so it can't really be interpreted
  // unfortunately.

  // Prepare SSL library, state is SSL_CONNECTING.
  int BeginSSL();
  // Perform SSL negotiation steps.
  int ContinueSSL();

  // Error handler helper. signal is given as true for errors in
  // asynchronous contexts (when an error method was not returned
  // through some other method), and in that case an SE_CLOSE event is
  // raised on the stream with the specified error.
  // A 0 error means a graceful close, otherwise there is not really enough
  // context to interpret the error code.
  // |alert| indicates an alert description (one of the SSL_AD constants) to
  // send to the remote endpoint when closing the association. If 0, a normal
  // shutdown will be performed.
  void Error(const char* context, int err, uint8_t alert, bool signal);
  void Cleanup(uint8_t alert);

  // Override MessageHandler
  void OnMessage(Message* msg) override;

  // Flush the input buffers by reading left bytes (for DTLS)
  void FlushInput(unsigned int left);

  // SSL library configuration
  SSL_CTX* SetupSSLContext();
  // Verify the peer certificate matches the signaled digest.
  bool VerifyPeerCertificate();
  // SSL certificate verification callback. See
  // SSL_CTX_set_cert_verify_callback.
  static int SSLVerifyCallback(X509_STORE_CTX* store, void* arg);

  bool waiting_to_verify_peer_certificate() const {
    return client_auth_enabled() && !peer_certificate_verified_;
  }

  bool has_peer_certificate_digest() const {
    return !peer_certificate_digest_algorithm_.empty() &&
           !peer_certificate_digest_value_.empty();
  }

  SSLState state_;
  SSLRole role_;
  int ssl_error_code_;  // valid when state_ == SSL_ERROR or SSL_CLOSED
  // Whether the SSL negotiation is blocked on needing to read or
  // write to the wrapped stream.
  bool ssl_read_needs_write_;
  bool ssl_write_needs_read_;

  SSL* ssl_;
  SSL_CTX* ssl_ctx_;

  // Our key and certificate.
  std::unique_ptr<OpenSSLIdentity> identity_;
  // The certificate that the peer presented. Initially null, until the
  // connection is established.
  std::unique_ptr<OpenSSLCertificate> peer_certificate_;
  std::unique_ptr<SSLCertChain> peer_cert_chain_;
  bool peer_certificate_verified_ = false;
  // The digest of the certificate that the peer must present.
  Buffer peer_certificate_digest_value_;
  std::string peer_certificate_digest_algorithm_;

  // The DtlsSrtp ciphers
  std::string srtp_ciphers_;

  // Do DTLS or not
  SSLMode ssl_mode_;

  // Max. allowed protocol version
  SSLProtocolVersion ssl_max_version_;

  // A 50-ms initial timeout ensures rapid setup on fast connections, but may
  // be too aggressive for low bandwidth links.
  int dtls_handshake_timeout_ms_ = 50;
};

/////////////////////////////////////////////////////////////////////////////

}  // namespace rtc

#endif  // RTC_BASE_OPENSSLSTREAMADAPTER_H_
