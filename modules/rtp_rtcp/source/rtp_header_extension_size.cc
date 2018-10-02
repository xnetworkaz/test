/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_header_extension_size.h"

namespace webrtc {

size_t RtpHeaderExtensionSize(
    rtc::ArrayView<const RtpExtensionSize> extensions,
    const RtpHeaderExtensionMap& registered_extensions) {
  // Header size of the extension block, see RFC3550 Section 5.3.1
  static constexpr size_t kRtpOneByteHeaderLength = 4;
  // Header size of each individual extension, see RFC8285 Section 4.2-4.3.
  static constexpr size_t kOneByteExtensionHeaderLength = 1;
  static constexpr size_t kTwoByteExtensionHeaderLength = 2;
  static constexpr size_t kOnyByteExtensionMaxValueLength = 16;
  size_t values_size = 0;
  size_t num_extensions = 0;
  size_t each_extension_header_size = kOneByteExtensionHeaderLength;
  for (const RtpExtensionSize& extension : extensions) {
    int id = registered_extensions.GetId(extension.type);
    if (id == RtpHeaderExtensionMap::kInvalidId)
      continue;
    if (id > RtpExtension::kOneByteHeaderExtensionMaxId ||
        extension.value_size > kOnyByteExtensionMaxValueLength) {
      each_extension_header_size = kTwoByteExtensionHeaderLength;
    }
    values_size += extension.value_size;
    num_extensions++;
  }
  if (values_size == 0)
    return 0;
  size_t size = kRtpOneByteHeaderLength +
                each_extension_header_size * num_extensions + values_size;
  // Extension size specified in 32bit words,
  // so result must be mutple of 4 bytes. Round up.
  return size + 3 - (size + 3) % 4;
}

}  // namespace webrtc
