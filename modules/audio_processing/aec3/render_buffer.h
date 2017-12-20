/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AEC3_RENDER_BUFFER_H_
#define MODULES_AUDIO_PROCESSING_AEC3_RENDER_BUFFER_H_

#include <array>
#include <memory>

#include "api/array_view.h"
#include "modules/audio_processing/aec3/fft_buffer.h"
#include "modules/audio_processing/aec3/fft_data.h"
#include "modules/audio_processing/aec3/matrix_buffer.h"
#include "modules/audio_processing/aec3/vector_buffer.h"
#include "rtc_base/constructormagic.h"

namespace webrtc {

// Provides a buffer of the render data for the echo remover.
class RenderBuffer {
 public:
  RenderBuffer(MatrixBuffer* block_buffer,
               VectorBuffer* spectrum_buffer,
               FftBuffer* fft_buffer);
  ~RenderBuffer();

  // Get a block.
  const std::vector<std::vector<float>>& Block(int buffer_offset_blocks) const {
    int position =
        block_buffer_->OffsetIndex(block_buffer_->read, buffer_offset_blocks);
    return block_buffer_->buffer[position];
  }

  // Get the spectrum from one of the FFTs in the buffer.
  rtc::ArrayView<const float> Spectrum(int buffer_offset_ffts) const {
    int position = spectrum_buffer_->OffsetIndex(spectrum_buffer_->read,
                                                 buffer_offset_ffts);
    return spectrum_buffer_->buffer[position];
  }

  // Returns the circular fft buffer.
  rtc::ArrayView<const FftData> GetFftBuffer() const {
    return fft_buffer_->buffer;
  }

  // Returns the current position in the circular buffer.
  size_t Position() const {
    RTC_DCHECK_EQ(spectrum_buffer_->read, fft_buffer_->read);
    RTC_DCHECK_EQ(spectrum_buffer_->write, fft_buffer_->write);
    return fft_buffer_->read;
  }

  // Returns the sum of the spectrums for a certain number of FFTs.
  void SpectralSum(size_t num_spectra,
                   std::array<float, kFftLengthBy2Plus1>* X2) const;

 private:
  const MatrixBuffer* const block_buffer_;
  const VectorBuffer* const spectrum_buffer_;
  const FftBuffer* const fft_buffer_;
  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(RenderBuffer);
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AEC3_RENDER_BUFFER_H_
