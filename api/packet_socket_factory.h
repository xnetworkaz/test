/*
 *  Copyright 2019 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_PACKET_SOCKET_FACTORY_H_
#define API_PACKET_SOCKET_FACTORY_H_

#include <string>
#include <vector>

#include "rtc_base/async_packet_socket.h"
#include "rtc_base/proxy_info.h"
#include "rtc_base/system/rtc_export.h"

namespace rtc {

class SSLCertificateVerifier;
class AsyncResolverInterface;

struct PacketSocketTcpOptions {
  PacketSocketTcpOptions() = default;
  ~PacketSocketTcpOptions() = default;

  int opts = 0;
  std::vector<std::string> tls_alpn_protocols;
  std::vector<std::string> tls_elliptic_curves;
  // An optional custom SSL certificate verifier that an API user can provide to
  // inject their own certificate verification logic (not available to users
  // outside of the WebRTC repo).
  SSLCertificateVerifier* tls_cert_verifier = nullptr;
};

class RTC_EXPORT PacketSocketFactory {
 public:
  enum Options {
    OPT_STUN = 0x04,

    // The TLS options below are mutually exclusive.
    OPT_TLS = 0x02,           // Real and secure TLS.
    OPT_TLS_INSECURE = 0x08,  // Insecure TLS without certificate validation.

    // TODO(bugs.webrtc.org/10947): Deprecated. Update chromium's
    // IpcPacketSocketFactory, then delete.
    OPT_SSLTCP = 0x01,
  };

  PacketSocketFactory() = default;
  virtual ~PacketSocketFactory() = default;

  virtual AsyncPacketSocket* CreateUdpSocket(const SocketAddress& address,
                                             uint16_t min_port,
                                             uint16_t max_port) = 0;
  virtual AsyncPacketSocket* CreateServerTcpSocket(
      const SocketAddress& local_address,
      uint16_t min_port,
      uint16_t max_port,
      int opts) = 0;

  virtual AsyncPacketSocket* CreateClientTcpSocket(
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      const ProxyInfo& proxy_info,
      const std::string& user_agent,
      const PacketSocketTcpOptions& tcp_options) = 0;

  virtual AsyncResolverInterface* CreateAsyncResolver() = 0;

 private:
  PacketSocketFactory(const PacketSocketFactory&) = delete;
  PacketSocketFactory& operator=(const PacketSocketFactory&) = delete;
};

}  // namespace rtc

#endif  // API_PACKET_SOCKET_FACTORY_H_
