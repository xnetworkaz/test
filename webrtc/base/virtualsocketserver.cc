/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/base/virtualsocketserver.h"

#include <errno.h>
#include <math.h>

#include <algorithm>
#include <map>
#include <vector>

#include "webrtc/base/common.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/socketaddresspair.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/timeutils.h"

namespace rtc {
#if defined(WEBRTC_WIN)
const in_addr kInitialNextIPv4 = { {0x01, 0, 0, 0} };
#else
// This value is entirely arbitrary, hence the lack of concern about endianness.
const in_addr kInitialNextIPv4 = { 0x01000000 };
#endif
// Starts at ::2 so as to not cause confusion with ::1.
const in6_addr kInitialNextIPv6 = { { {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
    } } };

const uint16 kFirstEphemeralPort = 49152;
const uint16 kLastEphemeralPort = 65535;
const uint16 kEphemeralPortCount = kLastEphemeralPort - kFirstEphemeralPort + 1;
const uint32 kDefaultNetworkCapacity = 64 * 1024;
const uint32 kDefaultTcpBufferSize = 32 * 1024;

const uint32 UDP_HEADER_SIZE = 28;  // IP + UDP headers
const uint32 TCP_HEADER_SIZE = 40;  // IP + TCP headers
const uint32 TCP_MSS = 1400;  // Maximum segment size

// Note: The current algorithm doesn't work for sample sizes smaller than this.
const int NUM_SAMPLES = 1000;

enum {
  MSG_ID_PACKET,
  MSG_ID_ADDRESS_BOUND,
  MSG_ID_CONNECT,
  MSG_ID_DISCONNECT,
};

// Packets are passed between sockets as messages.  We copy the data just like
// the kernel does.
class Packet : public MessageData {
 public:
  Packet(const char* data, size_t size, const SocketAddress& from)
        : size_(size), consumed_(0), from_(from) {
    ASSERT(NULL != data);
    data_ = new char[size_];
    memcpy(data_, data, size_);
  }

  virtual ~Packet() {
    delete[] data_;
  }

  const char* data() const { return data_ + consumed_; }
  size_t size() const { return size_ - consumed_; }
  const SocketAddress& from() const { return from_; }

  // Remove the first size bytes from the data.
  void Consume(size_t size) {
    ASSERT(size + consumed_ < size_);
    consumed_ += size;
  }

 private:
  char* data_;
  size_t size_, consumed_;
  SocketAddress from_;
};

struct MessageAddress : public MessageData {
  explicit MessageAddress(const SocketAddress& a) : addr(a) { }
  SocketAddress addr;
};

VirtualSocket::VirtualSocket(VirtualSocketServer* server,
                             int family,
                             int type,
                             bool async)
    : server_(server),
      family_(family),
      type_(type),
      async_(async),
      state_(CS_CLOSED),
      error_(0),
      listen_queue_(NULL),
      write_enabled_(false),
      network_size_(0),
      recv_buffer_size_(0),
      bound_(false),
      was_any_(false) {
  ASSERT((type_ == SOCK_DGRAM) || (type_ == SOCK_STREAM));
  ASSERT(async_ || (type_ != SOCK_STREAM));  // We only support async streams
}

VirtualSocket::~VirtualSocket() {
  Close();

  for (RecvBuffer::iterator it = recv_buffer_.begin(); it != recv_buffer_.end();
       ++it) {
    delete *it;
  }
}

SocketAddress VirtualSocket::GetLocalAddress() const {
  if (!alternative_local_addr_.IsNil())
    return alternative_local_addr_;
  return local_addr_;
}

SocketAddress VirtualSocket::GetRemoteAddress() const {
  return remote_addr_;
}

void VirtualSocket::SetLocalAddress(const SocketAddress& addr) {
  local_addr_ = addr;
}

void VirtualSocket::SetAlternativeLocalAddress(const SocketAddress& addr) {
  alternative_local_addr_ = addr;
}

int VirtualSocket::Bind(const SocketAddress& addr) {
  if (!local_addr_.IsNil()) {
    error_ = EINVAL;
    return -1;
  }
  local_addr_ = addr;
  int result = server_->Bind(this, &local_addr_);
  if (result != 0) {
    local_addr_.Clear();
    error_ = EADDRINUSE;
  } else {
    bound_ = true;
    was_any_ = addr.IsAnyIP();
    // Post a message here such that test case could have chance to
    // process the local address. (i.e. SetAlternativeLocalAddress).
    server_->msg_queue_->Post(this, MSG_ID_ADDRESS_BOUND);
  }
  return result;
}

int VirtualSocket::Connect(const SocketAddress& addr) {
  return InitiateConnect(addr, true);
}

int VirtualSocket::Close() {
  if (!local_addr_.IsNil() && bound_) {
    // Remove from the binding table.
    server_->Unbind(local_addr_, this);
    bound_ = false;
  }

  if (SOCK_STREAM == type_) {
    // Cancel pending sockets
    if (listen_queue_) {
      while (!listen_queue_->empty()) {
        SocketAddress addr = listen_queue_->front();

        // Disconnect listening socket.
        server_->Disconnect(server_->LookupBinding(addr));
        listen_queue_->pop_front();
      }
      delete listen_queue_;
      listen_queue_ = NULL;
    }
    // Disconnect stream sockets
    if (CS_CONNECTED == state_) {
      // Disconnect remote socket, check if it is a child of a server socket.
      VirtualSocket* socket =
          server_->LookupConnection(local_addr_, remote_addr_);
      if (!socket) {
        // Not a server socket child, then see if it is bound.
        // TODO(tbd): If this is indeed a server socket that has no
        // children this will cause the server socket to be
        // closed. This might lead to unexpected results, how to fix this?
        socket = server_->LookupBinding(remote_addr_);
      }
      server_->Disconnect(socket);

      // Remove mapping for both directions.
      server_->RemoveConnection(remote_addr_, local_addr_);
      server_->RemoveConnection(local_addr_, remote_addr_);
    }
    // Cancel potential connects
    MessageList msgs;
    if (server_->msg_queue_) {
      server_->msg_queue_->Clear(this, MSG_ID_CONNECT, &msgs);
    }
    for (MessageList::iterator it = msgs.begin(); it != msgs.end(); ++it) {
      ASSERT(NULL != it->pdata);
      MessageAddress* data = static_cast<MessageAddress*>(it->pdata);

      // Lookup remote side.
      VirtualSocket* socket =
          server_->LookupConnection(local_addr_, data->addr);
      if (socket) {
        // Server socket, remote side is a socket retreived by
        // accept. Accepted sockets are not bound so we will not
        // find it by looking in the bindings table.
        server_->Disconnect(socket);
        server_->RemoveConnection(local_addr_, data->addr);
      } else {
        server_->Disconnect(server_->LookupBinding(data->addr));
      }
      delete data;
    }
    // Clear incoming packets and disconnect messages
    if (server_->msg_queue_) {
      server_->msg_queue_->Clear(this);
    }
  }

  state_ = CS_CLOSED;
  local_addr_.Clear();
  remote_addr_.Clear();
  return 0;
}

int VirtualSocket::Send(const void* pv, size_t cb) {
    if (CS_CONNECTED != state_) {
      error_ = ENOTCONN;
      return -1;
    }
    if (SOCK_DGRAM == type_) {
      return SendUdp(pv, cb, remote_addr_);
    } else {
      return SendTcp(pv, cb);
    }
}

int VirtualSocket::SendTo(const void* pv,
                          size_t cb,
                          const SocketAddress& addr) {
  if (SOCK_DGRAM == type_) {
    return SendUdp(pv, cb, addr);
  } else {
    if (CS_CONNECTED != state_) {
      error_ = ENOTCONN;
      return -1;
    }
    return SendTcp(pv, cb);
  }
}

int VirtualSocket::Recv(void* pv, size_t cb) {
  SocketAddress addr;
  return RecvFrom(pv, cb, &addr);
}

int VirtualSocket::RecvFrom(void* pv, size_t cb, SocketAddress* paddr) {
  // If we don't have a packet, then either error or wait for one to arrive.
  if (recv_buffer_.empty()) {
    if (async_) {
      error_ = EAGAIN;
      return -1;
    }
    while (recv_buffer_.empty()) {
      Message msg;
      server_->msg_queue_->Get(&msg);
      server_->msg_queue_->Dispatch(&msg);
    }
  }

  // Return the packet at the front of the queue.
  Packet* packet = recv_buffer_.front();
  size_t data_read = _min(cb, packet->size());
  memcpy(pv, packet->data(), data_read);
  *paddr = packet->from();

  if (data_read < packet->size()) {
    packet->Consume(data_read);
  } else {
    recv_buffer_.pop_front();
    delete packet;
  }

  if (SOCK_STREAM == type_) {
    bool was_full = (recv_buffer_size_ == server_->recv_buffer_capacity_);
    recv_buffer_size_ -= data_read;
    if (was_full) {
      VirtualSocket* sender = server_->LookupBinding(remote_addr_);
      ASSERT(NULL != sender);
      server_->SendTcp(sender);
    }
  }

  return static_cast<int>(data_read);
}

int VirtualSocket::Listen(int backlog) {
  ASSERT(SOCK_STREAM == type_);
  ASSERT(CS_CLOSED == state_);
  if (local_addr_.IsNil()) {
    error_ = EINVAL;
    return -1;
  }
  ASSERT(NULL == listen_queue_);
  listen_queue_ = new ListenQueue;
  state_ = CS_CONNECTING;
  return 0;
}

VirtualSocket* VirtualSocket::Accept(SocketAddress* paddr) {
  if (NULL == listen_queue_) {
    error_ = EINVAL;
    return NULL;
  }
  while (!listen_queue_->empty()) {
    VirtualSocket* socket = new VirtualSocket(server_, AF_INET, type_, async_);

    // Set the new local address to the same as this server socket.
    socket->SetLocalAddress(local_addr_);
    // Sockets made from a socket that 'was Any' need to inherit that.
    socket->set_was_any(was_any_);
    SocketAddress remote_addr(listen_queue_->front());
    int result = socket->InitiateConnect(remote_addr, false);
    listen_queue_->pop_front();
    if (result != 0) {
      delete socket;
      continue;
    }
    socket->CompleteConnect(remote_addr, false);
    if (paddr) {
      *paddr = remote_addr;
    }
    return socket;
  }
  error_ = EWOULDBLOCK;
  return NULL;
}

int VirtualSocket::GetError() const {
  return error_;
}

void VirtualSocket::SetError(int error) {
  error_ = error;
}

Socket::ConnState VirtualSocket::GetState() const {
  return state_;
}

int VirtualSocket::GetOption(Option opt, int* value) {
  OptionsMap::const_iterator it = options_map_.find(opt);
  if (it == options_map_.end()) {
    return -1;
  }
  *value = it->second;
  return 0;  // 0 is success to emulate getsockopt()
}

int VirtualSocket::SetOption(Option opt, int value) {
  options_map_[opt] = value;
  return 0;  // 0 is success to emulate setsockopt()
}

int VirtualSocket::EstimateMTU(uint16* mtu) {
  if (CS_CONNECTED != state_)
    return ENOTCONN;
  else
    return 65536;
}

void VirtualSocket::OnMessage(Message* pmsg) {
  if (pmsg->message_id == MSG_ID_PACKET) {
    // ASSERT(!local_addr_.IsAny());
    ASSERT(NULL != pmsg->pdata);
    Packet* packet = static_cast<Packet*>(pmsg->pdata);

    recv_buffer_.push_back(packet);

    if (async_) {
      SignalReadEvent(this);
    }
  } else if (pmsg->message_id == MSG_ID_CONNECT) {
    ASSERT(NULL != pmsg->pdata);
    MessageAddress* data = static_cast<MessageAddress*>(pmsg->pdata);
    if (listen_queue_ != NULL) {
      listen_queue_->push_back(data->addr);
      if (async_) {
        SignalReadEvent(this);
      }
    } else if ((SOCK_STREAM == type_) && (CS_CONNECTING == state_)) {
      CompleteConnect(data->addr, true);
    } else {
      LOG(LS_VERBOSE) << "Socket at " << local_addr_ << " is not listening";
      server_->Disconnect(server_->LookupBinding(data->addr));
    }
    delete data;
  } else if (pmsg->message_id == MSG_ID_DISCONNECT) {
    ASSERT(SOCK_STREAM == type_);
    if (CS_CLOSED != state_) {
      int error = (CS_CONNECTING == state_) ? ECONNREFUSED : 0;
      state_ = CS_CLOSED;
      remote_addr_.Clear();
      if (async_) {
        SignalCloseEvent(this, error);
      }
    }
  } else if (pmsg->message_id == MSG_ID_ADDRESS_BOUND) {
    SignalAddressReady(this, GetLocalAddress());
  } else {
    ASSERT(false);
  }
}

int VirtualSocket::InitiateConnect(const SocketAddress& addr, bool use_delay) {
  if (!remote_addr_.IsNil()) {
    error_ = (CS_CONNECTED == state_) ? EISCONN : EINPROGRESS;
    return -1;
  }
  if (local_addr_.IsNil()) {
    // If there's no local address set, grab a random one in the correct AF.
    int result = 0;
    if (addr.ipaddr().family() == AF_INET) {
      result = Bind(SocketAddress("0.0.0.0", 0));
    } else if (addr.ipaddr().family() == AF_INET6) {
      result = Bind(SocketAddress("::", 0));
    }
    if (result != 0) {
      return result;
    }
  }
  if (type_ == SOCK_DGRAM) {
    remote_addr_ = addr;
    state_ = CS_CONNECTED;
  } else {
    int result = server_->Connect(this, addr, use_delay);
    if (result != 0) {
      error_ = EHOSTUNREACH;
      return -1;
    }
    state_ = CS_CONNECTING;
  }
  return 0;
}

void VirtualSocket::CompleteConnect(const SocketAddress& addr, bool notify) {
  ASSERT(CS_CONNECTING == state_);
  remote_addr_ = addr;
  state_ = CS_CONNECTED;
  server_->AddConnection(remote_addr_, local_addr_, this);
  if (async_ && notify) {
    SignalConnectEvent(this);
  }
}

int VirtualSocket::SendUdp(const void* pv,
                           size_t cb,
                           const SocketAddress& addr) {
  // If we have not been assigned a local port, then get one.
  if (local_addr_.IsNil()) {
    local_addr_ = EmptySocketAddressWithFamily(addr.ipaddr().family());
    int result = server_->Bind(this, &local_addr_);
    if (result != 0) {
      local_addr_.Clear();
      error_ = EADDRINUSE;
      return result;
    }
  }

  // Send the data in a message to the appropriate socket.
  return server_->SendUdp(this, static_cast<const char*>(pv), cb, addr);
}

int VirtualSocket::SendTcp(const void* pv, size_t cb) {
  size_t capacity = server_->send_buffer_capacity_ - send_buffer_.size();
  if (0 == capacity) {
    write_enabled_ = true;
    error_ = EWOULDBLOCK;
    return -1;
  }
  size_t consumed = _min(cb, capacity);
  const char* cpv = static_cast<const char*>(pv);
  send_buffer_.insert(send_buffer_.end(), cpv, cpv + consumed);
  server_->SendTcp(this);
  return static_cast<int>(consumed);
}

VirtualSocketServer::VirtualSocketServer(SocketServer* ss)
    : server_(ss), server_owned_(false), msg_queue_(NULL), stop_on_idle_(false),
      network_delay_(Time()), next_ipv4_(kInitialNextIPv4),
      next_ipv6_(kInitialNextIPv6), next_port_(kFirstEphemeralPort),
      bindings_(new AddressMap()), connections_(new ConnectionMap()),
      bandwidth_(0), network_capacity_(kDefaultNetworkCapacity),
      send_buffer_capacity_(kDefaultTcpBufferSize),
      recv_buffer_capacity_(kDefaultTcpBufferSize),
      delay_mean_(0), delay_stddev_(0), delay_samples_(NUM_SAMPLES),
      delay_dist_(NULL), drop_prob_(0.0) {
  if (!server_) {
    server_ = new PhysicalSocketServer();
    server_owned_ = true;
  }
  UpdateDelayDistribution();
}

VirtualSocketServer::~VirtualSocketServer() {
  delete bindings_;
  delete connections_;
  delete delay_dist_;
  if (server_owned_) {
    delete server_;
  }
}

IPAddress VirtualSocketServer::GetNextIP(int family) {
  if (family == AF_INET) {
    IPAddress next_ip(next_ipv4_);
    next_ipv4_.s_addr =
        HostToNetwork32(NetworkToHost32(next_ipv4_.s_addr) + 1);
    return next_ip;
  } else if (family == AF_INET6) {
    IPAddress next_ip(next_ipv6_);
    uint32* as_ints = reinterpret_cast<uint32*>(&next_ipv6_.s6_addr);
    as_ints[3] += 1;
    return next_ip;
  }
  return IPAddress();
}

uint16 VirtualSocketServer::GetNextPort() {
  uint16 port = next_port_;
  if (next_port_ < kLastEphemeralPort) {
    ++next_port_;
  } else {
    next_port_ = kFirstEphemeralPort;
  }
  return port;
}

Socket* VirtualSocketServer::CreateSocket(int type) {
  return CreateSocket(AF_INET, type);
}

Socket* VirtualSocketServer::CreateSocket(int family, int type) {
  return CreateSocketInternal(family, type);
}

AsyncSocket* VirtualSocketServer::CreateAsyncSocket(int type) {
  return CreateAsyncSocket(AF_INET, type);
}

AsyncSocket* VirtualSocketServer::CreateAsyncSocket(int family, int type) {
  return CreateSocketInternal(family, type);
}

VirtualSocket* VirtualSocketServer::CreateSocketInternal(int family, int type) {
  return new VirtualSocket(this, family, type, true);
}

void VirtualSocketServer::SetMessageQueue(MessageQueue* msg_queue) {
  msg_queue_ = msg_queue;
  if (msg_queue_) {
    msg_queue_->SignalQueueDestroyed.connect(this,
        &VirtualSocketServer::OnMessageQueueDestroyed);
  }
}

bool VirtualSocketServer::Wait(int cmsWait, bool process_io) {
  ASSERT(msg_queue_ == Thread::Current());
  if (stop_on_idle_ && Thread::Current()->empty()) {
    return false;
  }
  return socketserver()->Wait(cmsWait, process_io);
}

void VirtualSocketServer::WakeUp() {
  socketserver()->WakeUp();
}

bool VirtualSocketServer::ProcessMessagesUntilIdle() {
  ASSERT(msg_queue_ == Thread::Current());
  stop_on_idle_ = true;
  while (!msg_queue_->empty()) {
    Message msg;
    if (msg_queue_->Get(&msg, kForever)) {
      msg_queue_->Dispatch(&msg);
    }
  }
  stop_on_idle_ = false;
  return !msg_queue_->IsQuitting();
}

void VirtualSocketServer::SetNextPortForTesting(uint16 port) {
  next_port_ = port;
}

int VirtualSocketServer::Bind(VirtualSocket* socket,
                              const SocketAddress& addr) {
  ASSERT(NULL != socket);
  // Address must be completely specified at this point
  ASSERT(!IPIsUnspec(addr.ipaddr()));
  ASSERT(addr.port() != 0);

  // Normalize the address (turns v6-mapped addresses into v4-addresses).
  SocketAddress normalized(addr.ipaddr().Normalized(), addr.port());

  AddressMap::value_type entry(normalized, socket);
  return bindings_->insert(entry).second ? 0 : -1;
}

int VirtualSocketServer::Bind(VirtualSocket* socket, SocketAddress* addr) {
  ASSERT(NULL != socket);

  if (IPIsAny(addr->ipaddr())) {
    addr->SetIP(GetNextIP(addr->ipaddr().family()));
  } else if (!IPIsUnspec(addr->ipaddr())) {
    addr->SetIP(addr->ipaddr().Normalized());
  } else {
    ASSERT(false);
  }

  if (addr->port() == 0) {
    for (int i = 0; i < kEphemeralPortCount; ++i) {
      addr->SetPort(GetNextPort());
      if (bindings_->find(*addr) == bindings_->end()) {
        break;
      }
    }
  }

  return Bind(socket, *addr);
}

VirtualSocket* VirtualSocketServer::LookupBinding(const SocketAddress& addr) {
  SocketAddress normalized(addr.ipaddr().Normalized(),
                           addr.port());
  AddressMap::iterator it = bindings_->find(normalized);
  return (bindings_->end() != it) ? it->second : NULL;
}

int VirtualSocketServer::Unbind(const SocketAddress& addr,
                                VirtualSocket* socket) {
  SocketAddress normalized(addr.ipaddr().Normalized(),
                           addr.port());
  ASSERT((*bindings_)[normalized] == socket);
  bindings_->erase(bindings_->find(normalized));
  return 0;
}

void VirtualSocketServer::AddConnection(const SocketAddress& local,
                                        const SocketAddress& remote,
                                        VirtualSocket* remote_socket) {
  // Add this socket pair to our routing table. This will allow
  // multiple clients to connect to the same server address.
  SocketAddress local_normalized(local.ipaddr().Normalized(),
                                 local.port());
  SocketAddress remote_normalized(remote.ipaddr().Normalized(),
                                  remote.port());
  SocketAddressPair address_pair(local_normalized, remote_normalized);
  connections_->insert(std::pair<SocketAddressPair,
                       VirtualSocket*>(address_pair, remote_socket));
}

VirtualSocket* VirtualSocketServer::LookupConnection(
    const SocketAddress& local,
    const SocketAddress& remote) {
  SocketAddress local_normalized(local.ipaddr().Normalized(),
                                 local.port());
  SocketAddress remote_normalized(remote.ipaddr().Normalized(),
                                  remote.port());
  SocketAddressPair address_pair(local_normalized, remote_normalized);
  ConnectionMap::iterator it = connections_->find(address_pair);
  return (connections_->end() != it) ? it->second : NULL;
}

void VirtualSocketServer::RemoveConnection(const SocketAddress& local,
                                           const SocketAddress& remote) {
  SocketAddress local_normalized(local.ipaddr().Normalized(),
                                local.port());
  SocketAddress remote_normalized(remote.ipaddr().Normalized(),
                                 remote.port());
  SocketAddressPair address_pair(local_normalized, remote_normalized);
  connections_->erase(address_pair);
}

static double Random() {
  return static_cast<double>(rand()) / RAND_MAX;
}

int VirtualSocketServer::Connect(VirtualSocket* socket,
                                 const SocketAddress& remote_addr,
                                 bool use_delay) {
  uint32 delay = use_delay ? GetRandomTransitDelay() : 0;
  VirtualSocket* remote = LookupBinding(remote_addr);
  if (!CanInteractWith(socket, remote)) {
    LOG(LS_INFO) << "Address family mismatch between "
                 << socket->GetLocalAddress() << " and " << remote_addr;
    return -1;
  }
  if (remote != NULL) {
    SocketAddress addr = socket->GetLocalAddress();
    msg_queue_->PostDelayed(delay, remote, MSG_ID_CONNECT,
                            new MessageAddress(addr));
  } else {
    LOG(LS_INFO) << "No one listening at " << remote_addr;
    msg_queue_->PostDelayed(delay, socket, MSG_ID_DISCONNECT);
  }
  return 0;
}

bool VirtualSocketServer::Disconnect(VirtualSocket* socket) {
  if (socket) {
    // Remove the mapping.
    // Posted at network_delay_ + 1 so it is scheduled after any
    // pending packets.
    msg_queue_->PostAt(network_delay_ + 1, socket, MSG_ID_DISCONNECT);
    return true;
  }
  return false;
}

int VirtualSocketServer::SendUdp(VirtualSocket* socket,
                                 const char* data, size_t data_size,
                                 const SocketAddress& remote_addr) {
  // See if we want to drop this packet.
  if (Random() < drop_prob_) {
    LOG(LS_VERBOSE) << "Dropping packet: bad luck";
    return static_cast<int>(data_size);
  }

  VirtualSocket* recipient = LookupBinding(remote_addr);
  if (!recipient) {
    // Make a fake recipient for address family checking.
    scoped_ptr<VirtualSocket> dummy_socket(
        CreateSocketInternal(AF_INET, SOCK_DGRAM));
    dummy_socket->SetLocalAddress(remote_addr);
    if (!CanInteractWith(socket, dummy_socket.get())) {
      LOG(LS_VERBOSE) << "Incompatible address families: "
                      << socket->GetLocalAddress() << " and " << remote_addr;
      return -1;
    }
    LOG(LS_VERBOSE) << "No one listening at " << remote_addr;
    return static_cast<int>(data_size);
  }

  if (!CanInteractWith(socket, recipient)) {
    LOG(LS_VERBOSE) << "Incompatible address families: "
                    << socket->GetLocalAddress() << " and " << remote_addr;
    return -1;
  }

  CritScope cs(&socket->crit_);

  uint32 cur_time = Time();
  PurgeNetworkPackets(socket, cur_time);

  // Determine whether we have enough bandwidth to accept this packet.  To do
  // this, we need to update the send queue.  Once we know it's current size,
  // we know whether we can fit this packet.
  //
  // NOTE: There are better algorithms for maintaining such a queue (such as
  // "Derivative Random Drop"); however, this algorithm is a more accurate
  // simulation of what a normal network would do.

  size_t packet_size = data_size + UDP_HEADER_SIZE;
  if (socket->network_size_ + packet_size > network_capacity_) {
    LOG(LS_VERBOSE) << "Dropping packet: network capacity exceeded";
    return static_cast<int>(data_size);
  }

  AddPacketToNetwork(socket, recipient, cur_time, data, data_size,
                     UDP_HEADER_SIZE, false);

  return static_cast<int>(data_size);
}

void VirtualSocketServer::SendTcp(VirtualSocket* socket) {
  // TCP can't send more data than will fill up the receiver's buffer.
  // We track the data that is in the buffer plus data in flight using the
  // recipient's recv_buffer_size_.  Anything beyond that must be stored in the
  // sender's buffer.  We will trigger the buffered data to be sent when data
  // is read from the recv_buffer.

  // Lookup the local/remote pair in the connections table.
  VirtualSocket* recipient = LookupConnection(socket->local_addr_,
                                              socket->remote_addr_);
  if (!recipient) {
    LOG(LS_VERBOSE) << "Sending data to no one.";
    return;
  }

  CritScope cs(&socket->crit_);

  uint32 cur_time = Time();
  PurgeNetworkPackets(socket, cur_time);

  while (true) {
    size_t available = recv_buffer_capacity_ - recipient->recv_buffer_size_;
    size_t max_data_size = _min<size_t>(available, TCP_MSS - TCP_HEADER_SIZE);
    size_t data_size = _min(socket->send_buffer_.size(), max_data_size);
    if (0 == data_size)
      break;

    AddPacketToNetwork(socket, recipient, cur_time, &socket->send_buffer_[0],
                       data_size, TCP_HEADER_SIZE, true);
    recipient->recv_buffer_size_ += data_size;

    size_t new_buffer_size = socket->send_buffer_.size() - data_size;
    // Avoid undefined access beyond the last element of the vector.
    // This only happens when new_buffer_size is 0.
    if (data_size < socket->send_buffer_.size()) {
      // memmove is required for potentially overlapping source/destination.
      memmove(&socket->send_buffer_[0], &socket->send_buffer_[data_size],
              new_buffer_size);
    }
    socket->send_buffer_.resize(new_buffer_size);
  }

  if (socket->write_enabled_
      && (socket->send_buffer_.size() < send_buffer_capacity_)) {
    socket->write_enabled_ = false;
    socket->SignalWriteEvent(socket);
  }
}

void VirtualSocketServer::AddPacketToNetwork(VirtualSocket* sender,
                                             VirtualSocket* recipient,
                                             uint32 cur_time,
                                             const char* data,
                                             size_t data_size,
                                             size_t header_size,
                                             bool ordered) {
  VirtualSocket::NetworkEntry entry;
  entry.size = data_size + header_size;

  sender->network_size_ += entry.size;
  uint32 send_delay = SendDelay(static_cast<uint32>(sender->network_size_));
  entry.done_time = cur_time + send_delay;
  sender->network_.push_back(entry);

  // Find the delay for crossing the many virtual hops of the network.
  uint32 transit_delay = GetRandomTransitDelay();

  // Post the packet as a message to be delivered (on our own thread)
  Packet* p = new Packet(data, data_size, sender->local_addr_);
  uint32 ts = TimeAfter(send_delay + transit_delay);
  if (ordered) {
    // Ensure that new packets arrive after previous ones
    // TODO: consider ordering on a per-socket basis, since this
    // introduces artifical delay.
    ts = TimeMax(ts, network_delay_);
  }
  msg_queue_->PostAt(ts, recipient, MSG_ID_PACKET, p);
  network_delay_ = TimeMax(ts, network_delay_);
}

void VirtualSocketServer::PurgeNetworkPackets(VirtualSocket* socket,
                                              uint32 cur_time) {
  while (!socket->network_.empty() &&
         (socket->network_.front().done_time <= cur_time)) {
    ASSERT(socket->network_size_ >= socket->network_.front().size);
    socket->network_size_ -= socket->network_.front().size;
    socket->network_.pop_front();
  }
}

uint32 VirtualSocketServer::SendDelay(uint32 size) {
  if (bandwidth_ == 0)
    return 0;
  else
    return 1000 * size / bandwidth_;
}

#if 0
void PrintFunction(std::vector<std::pair<double, double> >* f) {
  return;
  double sum = 0;
  for (uint32 i = 0; i < f->size(); ++i) {
    std::cout << (*f)[i].first << '\t' << (*f)[i].second << std::endl;
    sum += (*f)[i].second;
  }
  if (!f->empty()) {
    const double mean = sum / f->size();
    double sum_sq_dev = 0;
    for (uint32 i = 0; i < f->size(); ++i) {
      double dev = (*f)[i].second - mean;
      sum_sq_dev += dev * dev;
    }
    std::cout << "Mean = " << mean << " StdDev = "
              << sqrt(sum_sq_dev / f->size()) << std::endl;
  }
}
#endif  // <unused>

void VirtualSocketServer::UpdateDelayDistribution() {
  Function* dist = CreateDistribution(delay_mean_, delay_stddev_,
                                      delay_samples_);
  // We take a lock just to make sure we don't leak memory.
  {
    CritScope cs(&delay_crit_);
    delete delay_dist_;
    delay_dist_ = dist;
  }
}

static double PI = 4 * atan(1.0);

static double Normal(double x, double mean, double stddev) {
  double a = (x - mean) * (x - mean) / (2 * stddev * stddev);
  return exp(-a) / (stddev * sqrt(2 * PI));
}

#if 0  // static unused gives a warning
static double Pareto(double x, double min, double k) {
  if (x < min)
    return 0;
  else
    return k * std::pow(min, k) / std::pow(x, k+1);
}
#endif

VirtualSocketServer::Function* VirtualSocketServer::CreateDistribution(
    uint32 mean, uint32 stddev, uint32 samples) {
  Function* f = new Function();

  if (0 == stddev) {
    f->push_back(Point(mean, 1.0));
  } else {
    double start = 0;
    if (mean >= 4 * static_cast<double>(stddev))
      start = mean - 4 * static_cast<double>(stddev);
    double end = mean + 4 * static_cast<double>(stddev);

    for (uint32 i = 0; i < samples; i++) {
      double x = start + (end - start) * i / (samples - 1);
      double y = Normal(x, mean, stddev);
      f->push_back(Point(x, y));
    }
  }
  return Resample(Invert(Accumulate(f)), 0, 1, samples);
}

uint32 VirtualSocketServer::GetRandomTransitDelay() {
  size_t index = rand() % delay_dist_->size();
  double delay = (*delay_dist_)[index].second;
  //LOG_F(LS_INFO) << "random[" << index << "] = " << delay;
  return static_cast<uint32>(delay);
}

struct FunctionDomainCmp {
  bool operator()(const VirtualSocketServer::Point& p1,
                   const VirtualSocketServer::Point& p2) {
    return p1.first < p2.first;
  }
  bool operator()(double v1, const VirtualSocketServer::Point& p2) {
    return v1 < p2.first;
  }
  bool operator()(const VirtualSocketServer::Point& p1, double v2) {
    return p1.first < v2;
  }
};

VirtualSocketServer::Function* VirtualSocketServer::Accumulate(Function* f) {
  ASSERT(f->size() >= 1);
  double v = 0;
  for (Function::size_type i = 0; i < f->size() - 1; ++i) {
    double dx = (*f)[i + 1].first - (*f)[i].first;
    double avgy = ((*f)[i + 1].second + (*f)[i].second) / 2;
    (*f)[i].second = v;
    v = v + dx * avgy;
  }
  (*f)[f->size()-1].second = v;
  return f;
}

VirtualSocketServer::Function* VirtualSocketServer::Invert(Function* f) {
  for (Function::size_type i = 0; i < f->size(); ++i)
    std::swap((*f)[i].first, (*f)[i].second);

  std::sort(f->begin(), f->end(), FunctionDomainCmp());
  return f;
}

VirtualSocketServer::Function* VirtualSocketServer::Resample(
    Function* f, double x1, double x2, uint32 samples) {
  Function* g = new Function();

  for (size_t i = 0; i < samples; i++) {
    double x = x1 + (x2 - x1) * i / (samples - 1);
    double y = Evaluate(f, x);
    g->push_back(Point(x, y));
  }

  delete f;
  return g;
}

double VirtualSocketServer::Evaluate(Function* f, double x) {
  Function::iterator iter =
      std::lower_bound(f->begin(), f->end(), x, FunctionDomainCmp());
  if (iter == f->begin()) {
    return (*f)[0].second;
  } else if (iter == f->end()) {
    ASSERT(f->size() >= 1);
    return (*f)[f->size() - 1].second;
  } else if (iter->first == x) {
    return iter->second;
  } else {
    double x1 = (iter - 1)->first;
    double y1 = (iter - 1)->second;
    double x2 = iter->first;
    double y2 = iter->second;
    return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
  }
}

bool VirtualSocketServer::CanInteractWith(VirtualSocket* local,
                                          VirtualSocket* remote) {
  if (!local || !remote) {
    return false;
  }
  IPAddress local_ip = local->GetLocalAddress().ipaddr();
  IPAddress remote_ip = remote->GetLocalAddress().ipaddr();
  IPAddress local_normalized = local_ip.Normalized();
  IPAddress remote_normalized = remote_ip.Normalized();
  // Check if the addresses are the same family after Normalization (turns
  // mapped IPv6 address into IPv4 addresses).
  // This will stop unmapped V6 addresses from talking to mapped V6 addresses.
  if (local_normalized.family() == remote_normalized.family()) {
    return true;
  }

  // If ip1 is IPv4 and ip2 is :: and ip2 is not IPV6_V6ONLY.
  int remote_v6_only = 0;
  remote->GetOption(Socket::OPT_IPV6_V6ONLY, &remote_v6_only);
  if (local_ip.family() == AF_INET && !remote_v6_only && IPIsAny(remote_ip)) {
    return true;
  }
  // Same check, backwards.
  int local_v6_only = 0;
  local->GetOption(Socket::OPT_IPV6_V6ONLY, &local_v6_only);
  if (remote_ip.family() == AF_INET && !local_v6_only && IPIsAny(local_ip)) {
    return true;
  }

  // Check to see if either socket was explicitly bound to IPv6-any.
  // These sockets can talk with anyone.
  if (local_ip.family() == AF_INET6 && local->was_any()) {
    return true;
  }
  if (remote_ip.family() == AF_INET6 && remote->was_any()) {
    return true;
  }

  return false;
}

}  // namespace rtc
