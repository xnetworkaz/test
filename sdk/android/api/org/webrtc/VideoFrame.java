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

import android.graphics.Matrix;
import java.nio.ByteBuffer;

/**
 * Java version of webrtc::VideoFrame and webrtc::VideoFrameBuffer. A difference from the C++
 * version is that no explicit tag is used, and clients are expected to use 'instanceof' to find the
 * right subclass of the buffer. This allows clients to create custom VideoFrame.Buffer in
 * arbitrary format in their custom VideoSources, and then cast it back to the correct subclass in
 * their custom VideoSinks. All implementations must also implement the toI420() function,
 * converting from the underlying representation if necessary. I420 is the most widely accepted
 * format and serves as a fallback for video sinks that can only handle I420, e.g. the internal
 * WebRTC software encoders.
 */
public class VideoFrame {
  public interface Buffer {
    /**
     * Resolution of the buffer in pixels.
     */
    int getWidth();
    int getHeight();

    /**
     * Returns a memory-backed frame in I420 format. If the pixel data is in another format, a
     * conversion will take place. All implementations must provide a fallback to I420 for
     * compatibility with e.g. the internal WebRTC software encoders.
     */
    I420Buffer toI420();

    /**
     * Reference counting is needed since a video buffer can be shared between multiple VideoSinks,
     * and the buffer needs to be returned to the VideoSource as soon as all references are gone.
     */
    void retain();
    void release();

    /**
     * Crops a region defined by |cropx|, |cropY|, |cropWidth| and |cropHeight|. Scales it to size
     * |scaleWidth| x |scaleHeight|.
     */
    Buffer cropAndScale(
        int cropX, int cropY, int cropWidth, int cropHeight, int scaleWidth, int scaleHeight);
  }

  /**
   * Interface for I420 buffers.
   */
  public interface I420Buffer extends Buffer {
    /**
     * Returns a direct ByteBuffer containing Y-plane data. The buffer size is at least getStrideY()
     * * getHeight() bytes. Callers may mutate the ByteBuffer (eg. through relative-read
     * operations), so implementations must return a new ByteBuffer or slice for each call.
     */
    ByteBuffer getDataY();
    /**
     * Returns a direct ByteBuffer containing U-plane data. The buffer size is at least getStrideU()
     * * ((getHeight() + 1) / 2) bytes. Callers may mutate the ByteBuffer (eg. through relative-read
     * operations), so implementations must return a new ByteBuffer or slice for each call.
     */
    ByteBuffer getDataU();
    /**
     * Returns a direct ByteBuffer containing V-plane data. The buffer size is at least getStrideV()
     * * ((getHeight() + 1) / 2) bytes. Callers may mutate the ByteBuffer (eg. through relative-read
     * operations), so implementations must return a new ByteBuffer or slice for each call.
     */
    ByteBuffer getDataV();

    int getStrideY();
    int getStrideU();
    int getStrideV();
  }

  /**
   * Interface for buffers that are stored as a single texture, either in OES or RGB format.
   */
  public interface TextureBuffer extends Buffer {
    enum Type { OES, RGB }

    Type getType();
    int getTextureId();

    /**
     * Retrieve the transform matrix associated with the frame. This transform matrix maps 2D
     * homogeneous coordinates of the form (s, t, 1) with s and t in the inclusive range [0, 1] to
     * the coordinate that should be used to sample that location from the buffer.
     */
    public Matrix getTransformMatrix();
  }

  private final Buffer buffer;
  private final int rotation;
  private final long timestampNs;

  public VideoFrame(Buffer buffer, int rotation, long timestampNs) {
    if (buffer == null) {
      throw new IllegalArgumentException("buffer not allowed to be null");
    }
    if (rotation % 90 != 0) {
      throw new IllegalArgumentException("rotation must be a multiple of 90");
    }
    this.buffer = buffer;
    this.rotation = rotation;
    this.timestampNs = timestampNs;
  }

  public Buffer getBuffer() {
    return buffer;
  }

  /**
   * Rotation of the frame in degrees.
   */
  public int getRotation() {
    return rotation;
  }

  /**
   * Timestamp of the frame in nano seconds.
   */
  public long getTimestampNs() {
    return timestampNs;
  }

  public int getRotatedWidth() {
    if (rotation % 180 == 0) {
      return buffer.getWidth();
    }
    return buffer.getHeight();
  }

  public int getRotatedHeight() {
    if (rotation % 180 == 0) {
      return buffer.getHeight();
    }
    return buffer.getWidth();
  }

  /**
   * Reference counting of the underlying buffer.
   */
  public void retain() {
    buffer.retain();
  }

  public void release() {
    buffer.release();
  }

  public static VideoFrame.Buffer cropAndScaleI420(final I420Buffer buffer, int cropX, int cropY,
      int cropWidth, int cropHeight, int scaleWidth, int scaleHeight) {
    if (cropWidth == scaleWidth && cropHeight == scaleHeight) {
      // No scaling.
      ByteBuffer dataY = buffer.getDataY();
      ByteBuffer dataU = buffer.getDataU();
      ByteBuffer dataV = buffer.getDataV();

      dataY.position(cropX + cropY * buffer.getStrideY());
      dataU.position(cropX / 2 + cropY / 2 * buffer.getStrideU());
      dataV.position(cropX / 2 + cropY / 2 * buffer.getStrideV());

      buffer.retain();
      return new I420BufferImpl(buffer.getWidth(), buffer.getHeight(), dataY.slice(),
          buffer.getStrideY(), dataU.slice(), buffer.getStrideU(), dataV.slice(),
          buffer.getStrideV(), new Runnable() {
            @Override
            public void run() {
              buffer.release();
            }
          });
    }

    I420BufferImpl newBuffer = I420BufferImpl.allocate(scaleWidth, scaleHeight);
    nativeCropAndScaleI420(buffer.getDataY(), buffer.getStrideY(), buffer.getDataU(),
        buffer.getStrideU(), buffer.getDataV(), buffer.getStrideV(), cropX, cropY, cropWidth,
        cropHeight, newBuffer.getDataY(), newBuffer.getStrideY(), newBuffer.getDataU(),
        newBuffer.getStrideU(), newBuffer.getDataV(), newBuffer.getStrideV(), scaleWidth,
        scaleHeight);
    return newBuffer;
  }

  private static native void nativeCropAndScaleI420(ByteBuffer srcY, int srcStrideY,
      ByteBuffer srcU, int srcStrideU, ByteBuffer srcV, int srcStrideV, int cropX, int cropY,
      int cropWidth, int cropHeight, ByteBuffer dstY, int dstStrideY, ByteBuffer dstU,
      int dstStrideU, ByteBuffer dstV, int dstStrideV, int scaleWidth, int scaleHeight);
}
