/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc.audio;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;
import java.lang.Thread;
import java.nio.ByteBuffer;
import javax.annotation.Nullable;
import org.webrtc.ContextUtils;
import org.webrtc.Logging;
import org.webrtc.ThreadUtils;
import org.webrtc.audio.AudioDeviceModule.AudioTrackErrorCallback;
import org.webrtc.audio.AudioDeviceModule.AudioTrackStartErrorCode;
import org.webrtc.CalledByNative;
import org.webrtc.NativeClassQualifiedName;

class WebRtcAudioTrack {
  private static final boolean DEBUG = false;

  private static final String TAG = "WebRtcAudioTrack";

  // Default audio data format is PCM 16 bit per sample.
  // Guaranteed to be supported by all devices.
  private static final int BITS_PER_SAMPLE = 16;

  // Requested size of each recorded buffer provided to the client.
  private static final int CALLBACK_BUFFER_SIZE_MS = 10;

  // Average number of callbacks per second.
  private static final int BUFFERS_PER_SECOND = 1000 / CALLBACK_BUFFER_SIZE_MS;

  // The AudioTrackThread is allowed to wait for successful call to join()
  // but the wait times out afther this amount of time.
  private static final long AUDIO_TRACK_THREAD_JOIN_TIMEOUT_MS = 2000;

  // By default, WebRTC creates audio tracks with a usage attribute
  // corresponding to voice communications, such as telephony or VoIP.
  private static final int DEFAULT_USAGE = getDefaultUsageAttribute();
  private static int usageAttribute = DEFAULT_USAGE;

  // This method overrides the default usage attribute and allows the user
  // to set it to something else than AudioAttributes.USAGE_VOICE_COMMUNICATION.
  // NOTE: calling this method will most likely break existing VoIP tuning.
  // TODO(bugs.webrtc.org/8491): Remove NoSynchronizedMethodCheck suppression.
  @SuppressWarnings("NoSynchronizedMethodCheck")
  public static synchronized void setAudioTrackUsageAttribute(int usage) {
    Logging.w(TAG, "Default usage attribute is changed from: " + DEFAULT_USAGE + " to " + usage);
    usageAttribute = usage;
  }

  private static int getDefaultUsageAttribute() {
    if (WebRtcAudioUtils.runningOnLollipopOrHigher()) {
      return getDefaultUsageAttributeOnLollipopOrHigher();
    } else {
      // Not used on SDKs lower than L.
      return 0;
    }
  }

  @TargetApi(21)
  private static int getDefaultUsageAttributeOnLollipopOrHigher() {
    return AudioAttributes.USAGE_VOICE_COMMUNICATION;
  }

  private final long nativeAudioTrack;
  private final AudioManager audioManager;
  private final ThreadUtils.ThreadChecker threadChecker = new ThreadUtils.ThreadChecker();

  private ByteBuffer byteBuffer;

  private @Nullable AudioTrack audioTrack = null;
  private @Nullable AudioTrackThread audioThread = null;

  // Samples to be played are replaced by zeros if |speakerMute| is set to true.
  // Can be used to ensure that the speaker is fully muted.
  private static volatile boolean speakerMute = false;
  private byte[] emptyBytes;

  private static @Nullable AudioTrackErrorCallback errorCallback = null;

  public static void setErrorCallback(AudioTrackErrorCallback errorCallback) {
    Logging.d(TAG, "Set extended error callback");
    WebRtcAudioTrack.errorCallback = errorCallback;
  }

  /**
   * Audio thread which keeps calling AudioTrack.write() to stream audio.
   * Data is periodically acquired from the native WebRTC layer using the
   * nativeGetPlayoutData callback function.
   * This thread uses a Process.THREAD_PRIORITY_URGENT_AUDIO priority.
   */
  private class AudioTrackThread extends Thread {
    private volatile boolean keepAlive = true;

    public AudioTrackThread(String name) {
      super(name);
    }

    @Override
    public void run() {
      Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
      Logging.d(TAG, "AudioTrackThread" + WebRtcAudioUtils.getThreadInfo());
      assertTrue(audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING);

      // Fixed size in bytes of each 10ms block of audio data that we ask for
      // using callbacks to the native WebRTC client.
      final int sizeInBytes = byteBuffer.capacity();

      while (keepAlive) {
        // Get 10ms of PCM data from the native WebRTC client. Audio data is
        // written into the common ByteBuffer using the address that was
        // cached at construction.
        nativeGetPlayoutData(nativeAudioTrack, sizeInBytes);
        // Write data until all data has been written to the audio sink.
        // Upon return, the buffer position will have been advanced to reflect
        // the amount of data that was successfully written to the AudioTrack.
        assertTrue(sizeInBytes <= byteBuffer.remaining());
        if (speakerMute) {
          byteBuffer.clear();
          byteBuffer.put(emptyBytes);
          byteBuffer.position(0);
        }
        int bytesWritten = 0;
        if (WebRtcAudioUtils.runningOnLollipopOrHigher()) {
          bytesWritten = writeOnLollipop(audioTrack, byteBuffer, sizeInBytes);
        } else {
          bytesWritten = writePreLollipop(audioTrack, byteBuffer, sizeInBytes);
        }
        if (bytesWritten != sizeInBytes) {
          Logging.e(TAG, "AudioTrack.write played invalid number of bytes: " + bytesWritten);
          // If a write() returns a negative value, an error has occurred.
          // Stop playing and report an error in this case.
          if (bytesWritten < 0) {
            keepAlive = false;
            reportWebRtcAudioTrackError("AudioTrack.write failed: " + bytesWritten);
          }
        }
        // The byte buffer must be rewinded since byteBuffer.position() is
        // increased at each call to AudioTrack.write(). If we don't do this,
        // next call to AudioTrack.write() will fail.
        byteBuffer.rewind();

        // TODO(henrika): it is possible to create a delay estimate here by
        // counting number of written frames and subtracting the result from
        // audioTrack.getPlaybackHeadPosition().
      }

      // Stops playing the audio data. Since the instance was created in
      // MODE_STREAM mode, audio will stop playing after the last buffer that
      // was written has been played.
      if (audioTrack != null) {
        Logging.d(TAG, "Calling AudioTrack.stop...");
        try {
          audioTrack.stop();
          Logging.d(TAG, "AudioTrack.stop is done.");
        } catch (IllegalStateException e) {
          Logging.e(TAG, "AudioTrack.stop failed: " + e.getMessage());
        }
      }
    }

    @TargetApi(21)
    private int writeOnLollipop(AudioTrack audioTrack, ByteBuffer byteBuffer, int sizeInBytes) {
      return audioTrack.write(byteBuffer, sizeInBytes, AudioTrack.WRITE_BLOCKING);
    }

    private int writePreLollipop(AudioTrack audioTrack, ByteBuffer byteBuffer, int sizeInBytes) {
      return audioTrack.write(byteBuffer.array(), byteBuffer.arrayOffset(), sizeInBytes);
    }

    // Stops the inner thread loop which results in calling AudioTrack.stop().
    // Does not block the calling thread.
    public void stopThread() {
      Logging.d(TAG, "stopThread");
      keepAlive = false;
    }
  }

  @CalledByNative
  WebRtcAudioTrack(long nativeAudioTrack) {
    threadChecker.detachThread();
    Logging.d(TAG, "ctor" + WebRtcAudioUtils.getThreadInfo());
    this.nativeAudioTrack = nativeAudioTrack;
    audioManager =
        (AudioManager) ContextUtils.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
    if (DEBUG) {
      WebRtcAudioUtils.logDeviceInfo(TAG);
    }
  }

  @CalledByNative
  private boolean initPlayout(int sampleRate, int channels) {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "initPlayout(sampleRate=" + sampleRate + ", channels=" + channels + ")");
    final int bytesPerFrame = channels * (BITS_PER_SAMPLE / 8);
    byteBuffer = ByteBuffer.allocateDirect(bytesPerFrame * (sampleRate / BUFFERS_PER_SECOND));
    Logging.d(TAG, "byteBuffer.capacity: " + byteBuffer.capacity());
    emptyBytes = new byte[byteBuffer.capacity()];
    // Rather than passing the ByteBuffer with every callback (requiring
    // the potentially expensive GetDirectBufferAddress) we simply have the
    // the native class cache the address to the memory once.
    nativeCacheDirectBufferAddress(nativeAudioTrack, byteBuffer);

    // Get the minimum buffer size required for the successful creation of an
    // AudioTrack object to be created in the MODE_STREAM mode.
    // Note that this size doesn't guarantee a smooth playback under load.
    // TODO(henrika): should we extend the buffer size to avoid glitches?
    final int channelConfig = channelCountToConfiguration(channels);
    final int minBufferSizeInBytes =
        AudioTrack.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
    Logging.d(TAG, "AudioTrack.getMinBufferSize: " + minBufferSizeInBytes);
    // For the streaming mode, data must be written to the audio sink in
    // chunks of size (given by byteBuffer.capacity()) less than or equal
    // to the total buffer size |minBufferSizeInBytes|. But, we have seen
    // reports of "getMinBufferSize(): error querying hardware". Hence, it
    // can happen that |minBufferSizeInBytes| contains an invalid value.
    if (minBufferSizeInBytes < byteBuffer.capacity()) {
      reportWebRtcAudioTrackInitError("AudioTrack.getMinBufferSize returns an invalid value.");
      return false;
    }

    // Ensure that prevision audio session was stopped correctly before trying
    // to create a new AudioTrack.
    if (audioTrack != null) {
      reportWebRtcAudioTrackInitError("Conflict with existing AudioTrack.");
      return false;
    }
    try {
      // Create an AudioTrack object and initialize its associated audio buffer.
      // The size of this buffer determines how long an AudioTrack can play
      // before running out of data.
      if (WebRtcAudioUtils.runningOnLollipopOrHigher()) {
        // If we are on API level 21 or higher, it is possible to use a special AudioTrack
        // constructor that uses AudioAttributes and AudioFormat as input. It allows us to
        // supersede the notion of stream types for defining the behavior of audio playback,
        // and to allow certain platforms or routing policies to use this information for more
        // refined volume or routing decisions.
        audioTrack =
            createAudioTrackOnLollipopOrHigher(sampleRate, channelConfig, minBufferSizeInBytes);
      } else {
        // Use default constructor for API levels below 21.
        audioTrack =
            createAudioTrackOnLowerThanLollipop(sampleRate, channelConfig, minBufferSizeInBytes);
      }
    } catch (IllegalArgumentException e) {
      reportWebRtcAudioTrackInitError(e.getMessage());
      releaseAudioResources();
      return false;
    }

    // It can happen that an AudioTrack is created but it was not successfully
    // initialized upon creation. Seems to be the case e.g. when the maximum
    // number of globally available audio tracks is exceeded.
    if (audioTrack == null || audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
      reportWebRtcAudioTrackInitError("Initialization of audio track failed.");
      releaseAudioResources();
      return false;
    }
    logMainParameters();
    logMainParametersExtended();
    return true;
  }

  @CalledByNative
  private boolean startPlayout() {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "startPlayout");
    assertTrue(audioTrack != null);
    assertTrue(audioThread == null);

    // Starts playing an audio track.
    try {
      audioTrack.play();
    } catch (IllegalStateException e) {
      reportWebRtcAudioTrackStartError(AudioTrackStartErrorCode.AUDIO_TRACK_START_EXCEPTION,
          "AudioTrack.play failed: " + e.getMessage());
      releaseAudioResources();
      return false;
    }
    if (audioTrack.getPlayState() != AudioTrack.PLAYSTATE_PLAYING) {
      reportWebRtcAudioTrackStartError(AudioTrackStartErrorCode.AUDIO_TRACK_START_STATE_MISMATCH,
          "AudioTrack.play failed - incorrect state :" + audioTrack.getPlayState());
      releaseAudioResources();
      return false;
    }

    // Create and start new high-priority thread which calls AudioTrack.write()
    // and where we also call the native nativeGetPlayoutData() callback to
    // request decoded audio from WebRTC.
    audioThread = new AudioTrackThread("AudioTrackJavaThread");
    audioThread.start();
    return true;
  }

  @CalledByNative
  private boolean stopPlayout() {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "stopPlayout");
    assertTrue(audioThread != null);
    logUnderrunCount();
    audioThread.stopThread();

    Logging.d(TAG, "Stopping the AudioTrackThread...");
    audioThread.interrupt();
    if (!ThreadUtils.joinUninterruptibly(audioThread, AUDIO_TRACK_THREAD_JOIN_TIMEOUT_MS)) {
      Logging.e(TAG, "Join of AudioTrackThread timed out.");
      WebRtcAudioUtils.logAudioState(TAG);
    }
    Logging.d(TAG, "AudioTrackThread has now been stopped.");
    audioThread = null;
    releaseAudioResources();
    return true;
  }

  // Get max possible volume index for a phone call audio stream.
  @CalledByNative
  private int getStreamMaxVolume() {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "getStreamMaxVolume");
    assertTrue(audioManager != null);
    return audioManager.getStreamMaxVolume(AudioManager.STREAM_VOICE_CALL);
  }

  // Set current volume level for a phone call audio stream.
  @CalledByNative
  private boolean setStreamVolume(int volume) {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "setStreamVolume(" + volume + ")");
    assertTrue(audioManager != null);
    if (isVolumeFixed()) {
      Logging.e(TAG, "The device implements a fixed volume policy.");
      return false;
    }
    audioManager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, volume, 0);
    return true;
  }

  // TODO(bugs.webrtc.org/8580): Call requires API level 21 (current min is 16):
  // `android.media.AudioManager#isVolumeFixed`: NewApi [warning]
  @SuppressLint("NewApi")
  private boolean isVolumeFixed() {
    if (!WebRtcAudioUtils.runningOnLollipopOrHigher())
      return false;
    return audioManager.isVolumeFixed();
  }

  /** Get current volume level for a phone call audio stream. */
  @CalledByNative
  private int getStreamVolume() {
    threadChecker.checkIsOnValidThread();
    Logging.d(TAG, "getStreamVolume");
    assertTrue(audioManager != null);
    return audioManager.getStreamVolume(AudioManager.STREAM_VOICE_CALL);
  }

  private void logMainParameters() {
    Logging.d(TAG,
        "AudioTrack: "
            + "session ID: " + audioTrack.getAudioSessionId() + ", "
            + "channels: " + audioTrack.getChannelCount() + ", "
            + "sample rate: " + audioTrack.getSampleRate()
            + ", "
            // Gain (>=1.0) expressed as linear multiplier on sample values.
            + "max gain: " + AudioTrack.getMaxVolume());
  }

  // Creates and AudioTrack instance using AudioAttributes and AudioFormat as input.
  // It allows certain platforms or routing policies to use this information for more
  // refined volume or routing decisions.
  @TargetApi(21)
  private static AudioTrack createAudioTrackOnLollipopOrHigher(
      int sampleRateInHz, int channelConfig, int bufferSizeInBytes) {
    Logging.d(TAG, "createAudioTrackOnLollipopOrHigher");
    // TODO(henrika): use setPerformanceMode(int) with PERFORMANCE_MODE_LOW_LATENCY to control
    // performance when Android O is supported. Add some logging in the mean time.
    final int nativeOutputSampleRate =
        AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_VOICE_CALL);
    Logging.d(TAG, "nativeOutputSampleRate: " + nativeOutputSampleRate);
    if (sampleRateInHz != nativeOutputSampleRate) {
      Logging.w(TAG, "Unable to use fast mode since requested sample rate is not native");
    }
    if (usageAttribute != DEFAULT_USAGE) {
      Logging.w(TAG, "A non default usage attribute is used: " + usageAttribute);
    }
    // Create an audio track where the audio usage is for VoIP and the content type is speech.
    return new AudioTrack(new AudioAttributes.Builder()
                              .setUsage(usageAttribute)
                              .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                              .build(),
        new AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(sampleRateInHz)
            .setChannelMask(channelConfig)
            .build(),
        bufferSizeInBytes, AudioTrack.MODE_STREAM, AudioManager.AUDIO_SESSION_ID_GENERATE);
  }

  @SuppressWarnings("deprecation") // Deprecated in API level 25.
  private static AudioTrack createAudioTrackOnLowerThanLollipop(
      int sampleRateInHz, int channelConfig, int bufferSizeInBytes) {
    return new AudioTrack(AudioManager.STREAM_VOICE_CALL, sampleRateInHz, channelConfig,
        AudioFormat.ENCODING_PCM_16BIT, bufferSizeInBytes, AudioTrack.MODE_STREAM);
  }

  @TargetApi(24)
  private void logMainParametersExtended() {
    if (WebRtcAudioUtils.runningOnMarshmallowOrHigher()) {
      Logging.d(TAG,
          "AudioTrack: "
              // The effective size of the AudioTrack buffer that the app writes to.
              + "buffer size in frames: " + audioTrack.getBufferSizeInFrames());
    }
    if (WebRtcAudioUtils.runningOnNougatOrHigher()) {
      Logging.d(TAG,
          "AudioTrack: "
              // Maximum size of the AudioTrack buffer in frames.
              + "buffer capacity in frames: " + audioTrack.getBufferCapacityInFrames());
    }
  }

  // Prints the number of underrun occurrences in the application-level write
  // buffer since the AudioTrack was created. An underrun occurs if the app does
  // not write audio data quickly enough, causing the buffer to underflow and a
  // potential audio glitch.
  // TODO(henrika): keep track of this value in the field and possibly add new
  // UMA stat if needed.
  @TargetApi(24)
  private void logUnderrunCount() {
    if (WebRtcAudioUtils.runningOnNougatOrHigher()) {
      Logging.d(TAG, "underrun count: " + audioTrack.getUnderrunCount());
    }
  }

  // Helper method which throws an exception  when an assertion has failed.
  private static void assertTrue(boolean condition) {
    if (!condition) {
      throw new AssertionError("Expected condition to be true");
    }
  }

  private int channelCountToConfiguration(int channels) {
    return (channels == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO);
  }

  @NativeClassQualifiedName("webrtc::android_adm::AudioTrackJni")
  private static native void nativeCacheDirectBufferAddress(
      long nativeAudioRecord, ByteBuffer byteBuffer);

  @NativeClassQualifiedName("webrtc::android_adm::AudioTrackJni")
  private static native void nativeGetPlayoutData(long nativeAudioRecord, int bytes);

  // Sets all samples to be played out to zero if |mute| is true, i.e.,
  // ensures that the speaker is muted.
  public static void setSpeakerMute(boolean mute) {
    Logging.w(TAG, "setSpeakerMute(" + mute + ")");
    speakerMute = mute;
  }

  // Releases the native AudioTrack resources.
  private void releaseAudioResources() {
    Logging.d(TAG, "releaseAudioResources");
    if (audioTrack != null) {
      audioTrack.release();
      audioTrack = null;
    }
  }

  private void reportWebRtcAudioTrackInitError(String errorMessage) {
    Logging.e(TAG, "Init playout error: " + errorMessage);
    WebRtcAudioUtils.logAudioState(TAG);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioTrackInitError(errorMessage);
    }
  }

  private void reportWebRtcAudioTrackStartError(
      AudioTrackStartErrorCode errorCode, String errorMessage) {
    Logging.e(TAG, "Start playout error: " + errorCode + ". " + errorMessage);
    WebRtcAudioUtils.logAudioState(TAG);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioTrackStartError(errorCode, errorMessage);
    }
  }

  private void reportWebRtcAudioTrackError(String errorMessage) {
    Logging.e(TAG, "Run-time playback error: " + errorMessage);
    WebRtcAudioUtils.logAudioState(TAG);
    if (errorCallback != null) {
      errorCallback.onWebRtcAudioTrackError(errorMessage);
    }
  }
}
