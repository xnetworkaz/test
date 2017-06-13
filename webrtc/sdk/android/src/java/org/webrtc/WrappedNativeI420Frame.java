/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import java.nio.ByteBuffer;

/**
 * This class wraps a webrtc::I420BufferInterface into a VideoFrame.I420.
 */
class WrappedNativeI420Frame implements VideoFrame.I420 {
  private final int width;
  private final int height;
  private final int rotation;
  private final ByteBuffer dataY;
  private final int strideY;
  private final ByteBuffer dataU;
  private final int strideU;
  private final ByteBuffer dataV;
  private final int strideV;
  private final long nativeFrame;

  WrappedNativeI420Frame(int width, int height, int rotation, ByteBuffer dataY, int strideY,
      ByteBuffer dataU, int strideU, ByteBuffer dataV, int strideV, long nativeFrame) {
    this.width = width;
    this.height = height;
    this.rotation = rotation;
    this.dataY = dataY;
    this.strideY = strideY;
    this.dataU = dataU;
    this.strideU = strideU;
    this.dataV = dataV;
    this.strideV = strideV;
    this.nativeFrame = nativeFrame;
  }

  @Override
  public int width() {
    return width;
  }

  @Override
  public int height() {
    return height;
  }

  @Override
  public int rotation() {
    return rotation;
  }

  @Override
  public I420 ToI420() {
    return this;
  }

  @Override
  public void addRef() {
    nativeAddRef(nativeFrame);
  }

  @Override
  public void release() {
    nativeRelease(nativeFrame);
  }

  private static native long nativeAddRef(long nativeFrame);
  private static native long nativeRelease(long nativeFrame);
}
