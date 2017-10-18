/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>
#import "WebRTC/RTCPeerConnection.h"

@interface ARDBitrateAllocationStrategy : NSObject

+ (ARDBitrateAllocationStrategy*)
    createAudioPriorityBitrateAllocationStrategyForPeerConnection:(RTCPeerConnection*)peerConnection
                                                   withAudioTrack:(NSString*)audioTrackID
                                           sufficientAudioBitrate:(uint32_t)sufficientAudioBitrate;

- (instancetype)init NS_UNAVAILABLE;

@end
