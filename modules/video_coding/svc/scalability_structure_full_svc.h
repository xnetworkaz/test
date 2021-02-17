/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_VIDEO_CODING_SVC_SCALABILITY_STRUCTURE_FULL_SVC_H_
#define MODULES_VIDEO_CODING_SVC_SCALABILITY_STRUCTURE_FULL_SVC_H_

#include <bitset>
#include <vector>

#include "api/transport/rtp/dependency_descriptor.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/video_coding/svc/scalability_structure_helper_t3.h"
#include "modules/video_coding/svc/scalable_video_controller.h"

namespace webrtc {

class ScalabilityStructureFullSvc : public ScalableVideoController {
 public:
  using ScalingFactor = ScalabilityStructureHelperT3::ScalingFactor;
  ScalabilityStructureFullSvc(int num_spatial_layers,
                              int num_temporal_layers,
                              ScalingFactor resolution_factor);
  ~ScalabilityStructureFullSvc() override;

  StreamLayersConfig StreamConfig() const override;

  std::vector<LayerFrameConfig> NextFrameConfig(bool restart) override;
  GenericFrameInfo OnEncodeDone(const LayerFrameConfig& config) override;
  void OnRatesUpdated(const VideoBitrateAllocation& bitrates) override;

 private:
  ScalabilityStructureHelperT3 helper_;
  std::bitset<ScalabilityStructureHelperT3::kMaxNumSpatialLayers>
      can_reference_t0_frame_for_spatial_id_ = 0;
  std::bitset<ScalabilityStructureHelperT3::kMaxNumSpatialLayers>
      can_reference_t1_frame_for_spatial_id_ = 0;
  ScalabilityStructureHelperT3::FramePattern last_pattern_ =
      ScalabilityStructureHelperT3::kNone;
};

// T1       0   0
//         /   /   / ...
// T0     0---0---0--
// Time-> 0 1 2 3 4
class ScalabilityStructureL1T2 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL1T2(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(1, 2, resolution_factor) {}
  ~ScalabilityStructureL1T2() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

// T2       0   0   0   0
//          |  /    |  /
// T1       / 0     / 0  ...
//         |_/     |_/
// T0     0-------0------
// Time-> 0 1 2 3 4 5 6 7
class ScalabilityStructureL1T3 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL1T3(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(1, 3, resolution_factor) {}
  ~ScalabilityStructureL1T3() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

// S1  0--0--0-
//     |  |  | ...
// S0  0--0--0-
class ScalabilityStructureL2T1 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL2T1(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(2, 1, resolution_factor) {}
  ~ScalabilityStructureL2T1() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

// S1T1     0   0
//         /|  /|  /
// S1T0   0-+-0-+-0
//        | | | | | ...
// S0T1   | 0 | 0 |
//        |/  |/  |/
// S0T0   0---0---0--
// Time-> 0 1 2 3 4
class ScalabilityStructureL2T2 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL2T2(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(2, 2, resolution_factor) {}
  ~ScalabilityStructureL2T2() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

// S2     0-0-0-
//        | | |
// S1     0-0-0-...
//        | | |
// S0     0-0-0-
// Time-> 0 1 2
class ScalabilityStructureL3T1 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL3T1(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(3, 1, resolution_factor) {}
  ~ScalabilityStructureL3T1() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

// https://www.w3.org/TR/webrtc-svc/#L3T3*
class ScalabilityStructureL3T3 : public ScalabilityStructureFullSvc {
 public:
  explicit ScalabilityStructureL3T3(ScalingFactor resolution_factor = {})
      : ScalabilityStructureFullSvc(3, 3, resolution_factor) {}
  ~ScalabilityStructureL3T3() override = default;

  FrameDependencyStructure DependencyStructure() const override;
};

}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_SVC_SCALABILITY_STRUCTURE_FULL_SVC_H_
