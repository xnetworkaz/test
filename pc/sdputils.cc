/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/sdputils.h"

#include "api/jsepsessiondescription.h"
#include "p2p/base/sessiondescription.h"

namespace webrtc {

std::unique_ptr<SessionDescriptionInterface> CloneSessionDescription(
    const SessionDescriptionInterface* sdesc) {
  std::unique_ptr<JsepSessionDescription> clone(
      new JsepSessionDescription(sdesc->type()));
  RTC_CHECK(clone->Initialize(sdesc->description()->Copy(), sdesc->session_id(),
                              sdesc->session_version()));
  return clone;
}

bool SdpContentsAll(SdpContentPredicate pred,
                    const cricket::SessionDescription* desc) {
  for (const auto& content : desc->contents()) {
    const auto* transport_info = desc->GetTransportInfoByName(content.name);
    if (!pred(&content, transport_info)) {
      return false;
    }
  }
  return true;
}

bool SdpContentsNone(SdpContentPredicate pred,
                     const cricket::SessionDescription* desc) {
  return SdpContentsAll(std::not2(pred), desc);
}

void SdpContentsForEach(SdpContentMutator fn,
                        cricket::SessionDescription* desc) {
  for (auto& content : desc->contents()) {
    auto* transport_info = desc->GetTransportInfoByName(content.name);
    fn(&content, transport_info);
  }
}

}  // namespace webrtc
