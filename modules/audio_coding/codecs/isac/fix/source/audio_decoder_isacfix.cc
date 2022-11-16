/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/codecs/isac/fix/include/audio_decoder_isacfix.h"

#include "modules/audio_coding/codecs/isac/audio_decoder_isac_t_impl.h"

namespace webrtc {

// Explicit instantiation:
template class AudioDecoderIsacT<IsacFix>;

}  // namespace webrtc
