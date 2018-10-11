/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "rtc_base/cocoa_threading.h"

#import <Foundation/Foundation.h>

#include "rtc_base/checks.h"

void InitCocoaMultiThreading() {
  static BOOL multithreaded = [NSThread isMultiThreaded];
  if (!multithreaded) {
    // +[NSObject class] is idempotent.
    [NSThread detachNewThreadSelector:@selector(class) toTarget:[NSObject class] withObject:nil];
    multithreaded = YES;
    RTC_DCHECK([NSThread isMultiThreaded]);
  }
}
