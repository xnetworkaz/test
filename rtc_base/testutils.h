/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_TESTUTILS_H_
#define RTC_BASE_TESTUTILS_H_

// Utilities for testing rtc infrastructure in unittests

#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include "rtc_base/asyncsocket.h"
#include "rtc_base/checks.h"
#include "rtc_base/gunit.h"
#include "rtc_base/stream.h"

namespace webrtc {
namespace testing {

using namespace rtc;

///////////////////////////////////////////////////////////////////////////////
// StreamSink - Monitor asynchronously signalled events from StreamInterface
// or AsyncSocket (which should probably be a StreamInterface.
///////////////////////////////////////////////////////////////////////////////

// Note: Any event that is an error is treaded as SSE_ERROR instead of that
// event.

enum StreamSinkEvent {
  SSE_OPEN = SE_OPEN,
  SSE_READ = SE_READ,
  SSE_WRITE = SE_WRITE,
  SSE_CLOSE = SE_CLOSE,
  SSE_ERROR = 16
};

class StreamSink : public sigslot::has_slots<> {
 public:
  StreamSink();
  ~StreamSink() override;

  void Monitor(StreamInterface* stream) {
    stream->SignalEvent.connect(this, &StreamSink::OnEvent);
    events_.erase(stream);
  }
  void Unmonitor(StreamInterface* stream) {
    stream->SignalEvent.disconnect(this);
    // In case you forgot to unmonitor a previous object with this address
    events_.erase(stream);
  }
  bool Check(StreamInterface* stream,
             StreamSinkEvent event,
             bool reset = true) {
    return DoCheck(stream, event, reset);
  }
  int Events(StreamInterface* stream, bool reset = true) {
    return DoEvents(stream, reset);
  }

  void Monitor(AsyncSocket* socket) {
    socket->SignalConnectEvent.connect(this, &StreamSink::OnConnectEvent);
    socket->SignalReadEvent.connect(this, &StreamSink::OnReadEvent);
    socket->SignalWriteEvent.connect(this, &StreamSink::OnWriteEvent);
    socket->SignalCloseEvent.connect(this, &StreamSink::OnCloseEvent);
    // In case you forgot to unmonitor a previous object with this address
    events_.erase(socket);
  }
  void Unmonitor(AsyncSocket* socket) {
    socket->SignalConnectEvent.disconnect(this);
    socket->SignalReadEvent.disconnect(this);
    socket->SignalWriteEvent.disconnect(this);
    socket->SignalCloseEvent.disconnect(this);
    events_.erase(socket);
  }
  bool Check(AsyncSocket* socket, StreamSinkEvent event, bool reset = true) {
    return DoCheck(socket, event, reset);
  }
  int Events(AsyncSocket* socket, bool reset = true) {
    return DoEvents(socket, reset);
  }

 private:
  typedef std::map<void*, int> EventMap;

  void OnEvent(StreamInterface* stream, int events, int error) {
    if (error) {
      events = SSE_ERROR;
    }
    AddEvents(stream, events);
  }
  void OnConnectEvent(AsyncSocket* socket) { AddEvents(socket, SSE_OPEN); }
  void OnReadEvent(AsyncSocket* socket) { AddEvents(socket, SSE_READ); }
  void OnWriteEvent(AsyncSocket* socket) { AddEvents(socket, SSE_WRITE); }
  void OnCloseEvent(AsyncSocket* socket, int error) {
    AddEvents(socket, (0 == error) ? SSE_CLOSE : SSE_ERROR);
  }

  void AddEvents(void* obj, int events) {
    EventMap::iterator it = events_.find(obj);
    if (events_.end() == it) {
      events_.insert(EventMap::value_type(obj, events));
    } else {
      it->second |= events;
    }
  }
  bool DoCheck(void* obj, StreamSinkEvent event, bool reset) {
    EventMap::iterator it = events_.find(obj);
    if ((events_.end() == it) || (0 == (it->second & event))) {
      return false;
    }
    if (reset) {
      it->second &= ~event;
    }
    return true;
  }
  int DoEvents(void* obj, bool reset) {
    EventMap::iterator it = events_.find(obj);
    if (events_.end() == it)
      return 0;
    int events = it->second;
    if (reset) {
      it->second = 0;
    }
    return events;
  }

  EventMap events_;
};

}  // namespace testing
}  // namespace webrtc

#endif  // RTC_BASE_TESTUTILS_H_
