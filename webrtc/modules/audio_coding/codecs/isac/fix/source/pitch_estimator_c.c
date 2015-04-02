/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/audio_coding/codecs/isac/fix/source/pitch_estimator.h"

#ifdef WEBRTC_ARCH_ARM_NEON
#include <arm_neon.h>
#endif

#include "webrtc/common_audio/signal_processing/include/signal_processing_library.h"
#include "webrtc/system_wrappers/interface/compile_assert_c.h"

extern int32_t WebRtcIsacfix_Log2Q8(uint32_t x);

void WebRtcIsacfix_PCorr2Q32(const int16_t* in, int32_t* logcorQ8) {
  int16_t scaling,n,k;
  int32_t ysum32,csum32, lys, lcs;
  const int32_t oneQ8 = 1 << 8;  // 1.00 in Q8
  const int16_t* x;
  const int16_t* inptr;

  x = in + PITCH_MAX_LAG / 2 + 2;
  scaling = WebRtcSpl_GetScalingSquare((int16_t*)in,
                                       PITCH_CORR_LEN2,
                                       PITCH_CORR_LEN2);
  ysum32 = 1;
  csum32 = 0;
  x = in + PITCH_MAX_LAG / 2 + 2;
  for (n = 0; n < PITCH_CORR_LEN2; n++) {
    ysum32 += WEBRTC_SPL_MUL_16_16_RSFT((int16_t)in[n],
                                        (int16_t)in[n],
                                        scaling);  // Q0
    csum32 += WEBRTC_SPL_MUL_16_16_RSFT((int16_t)x[n],
                                        (int16_t)in[n],
                                        scaling);  // Q0
  }
  logcorQ8 += PITCH_LAG_SPAN2 - 1;
  lys = WebRtcIsacfix_Log2Q8((uint32_t)ysum32) >> 1; // Q8, sqrt(ysum)
  if (csum32 > 0) {
    lcs = WebRtcIsacfix_Log2Q8((uint32_t)csum32);  // 2log(csum) in Q8
    if (lcs > (lys + oneQ8)) {          // csum/sqrt(ysum) > 2 in Q8
      *logcorQ8 = lcs - lys;            // log2(csum/sqrt(ysum))
    } else {
      *logcorQ8 = oneQ8;                // 1.00
    }
  } else {
    *logcorQ8 = 0;
  }


  for (k = 1; k < PITCH_LAG_SPAN2; k++) {
    inptr = &in[k];
    ysum32 -= WEBRTC_SPL_MUL_16_16_RSFT((int16_t)in[k - 1],
                                        (int16_t)in[k - 1],
                                        scaling);
    ysum32 += WEBRTC_SPL_MUL_16_16_RSFT((int16_t)in[PITCH_CORR_LEN2 + k - 1],
                                        (int16_t)in[PITCH_CORR_LEN2 + k - 1],
                                        scaling);
#ifdef WEBRTC_ARCH_ARM_NEON
    {
      int32_t vbuff[4];
      int32x4_t int_32x4_sum = vmovq_n_s32(0);
      // Can't shift a Neon register to right with a non-constant shift value.
      int32x4_t int_32x4_scale = vdupq_n_s32(-scaling);
      // Assert a codition used in loop unrolling at compile-time.
      COMPILE_ASSERT(PITCH_CORR_LEN2 %4 == 0);

      for (n = 0; n < PITCH_CORR_LEN2; n += 4) {
        int16x4_t int_16x4_x = vld1_s16(&x[n]);
        int16x4_t int_16x4_in = vld1_s16(&inptr[n]);
        int32x4_t int_32x4 = vmull_s16(int_16x4_x, int_16x4_in);
        int_32x4 = vshlq_s32(int_32x4, int_32x4_scale);
        int_32x4_sum = vaddq_s32(int_32x4_sum, int_32x4);
      }

      // Use vector store to avoid long stall from data trasferring
      // from vector to general register.
      vst1q_s32(vbuff, int_32x4_sum);
      csum32 = vbuff[0] + vbuff[1];
      csum32 += vbuff[2];
      csum32 += vbuff[3];
    }
#else
    csum32 = 0;
    if(scaling == 0) {
      for (n = 0; n < PITCH_CORR_LEN2; n++) {
        csum32 += x[n] * inptr[n];
      }
    } else {
      for (n = 0; n < PITCH_CORR_LEN2; n++) {
        csum32 += (x[n] * inptr[n]) >> scaling;
      }
    }
#endif

    logcorQ8--;

    lys = WebRtcIsacfix_Log2Q8((uint32_t)ysum32) >> 1; // Q8, sqrt(ysum)

    if (csum32 > 0) {
      lcs = WebRtcIsacfix_Log2Q8((uint32_t)csum32);  // 2log(csum) in Q8
      if (lcs > (lys + oneQ8)) {          // csum/sqrt(ysum) > 2
        *logcorQ8 = lcs - lys;            // log2(csum/sqrt(ysum))
      } else {
        *logcorQ8 = oneQ8;                // 1.00
      }
    } else {
      *logcorQ8 = 0;
    }
  }
}
