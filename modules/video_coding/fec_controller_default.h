/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_FEC_CONTROLLER_DEFAULT_H_
#define MODULES_VIDEO_CODING_FEC_CONTROLLER_DEFAULT_H_

#include <memory>
#include <vector>
#include "api/fec_controller.h"
#include "modules/include/module_common_types.h"
#include "modules/video_coding/media_opt_util.h"
#include "rtc_base/criticalsection.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

class FecControllerDefault : public FecController {
 public:
  FecControllerDefault(Clock* clock,
                       VCMProtectionCallback* protection_callback);
  explicit FecControllerDefault(Clock* clock);
  ~FecControllerDefault();
  void SetProtectionCallback(
      VCMProtectionCallback* protection_callback) override;
  void SetProtectionMethod(bool enable_fec, bool enable_nack) override;
  void SetEncodingData(size_t width,
                       size_t height,
                       size_t num_temporal_layers,
                       size_t max_payload_size) override;
  uint32_t UpdateFecRates(uint32_t estimated_bitrate_bps,
                          int actual_framerate_fps,
                          uint8_t fraction_lost,
                          std::vector<bool> loss_mask_vector,
                          int64_t round_trip_time_ms) override;
  void UpdateWithEncodedData(const size_t encoded_image_length,
                             const FrameType encoded_image_frametype) override;
  bool UseLossVectorMask() override { return false; }

 private:
  enum { kBitrateAverageWinMs = 1000 };
  Clock* const clock_;
  VCMProtectionCallback* protection_callback_;
  rtc::CriticalSection crit_sect_;
  std::unique_ptr<media_optimization::VCMLossProtectionLogic> loss_prot_logic_
      RTC_GUARDED_BY(crit_sect_);
  size_t max_payload_size_ RTC_GUARDED_BY(crit_sect_);
  RTC_DISALLOW_COPY_AND_ASSIGN(FecControllerDefault);
};

// Will always use realtime clock.
class DefaultFecControllerFactory : public FecControllerFactoryInterface {
 public:
  DefaultFecControllerFactory() = default;
  std::unique_ptr<FecController> CreateFecController() override;
};

}  // namespace webrtc
#endif  // MODULES_VIDEO_CODING_FEC_CONTROLLER_DEFAULT_H_
