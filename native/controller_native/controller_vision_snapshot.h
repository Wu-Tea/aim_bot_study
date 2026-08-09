#pragma once

#include "controller_tick_context.h"

#include "../pipeline_contract/target_snapshot.h"
#include "../tracking_native/tracker_contract.h"

#include <cstdint>
#include <vector>

namespace controller_native {

struct ControllerVisionSnapshot {
    bool frame_updated = false;
    bool selector_identity_protocol = false;
    NativeControllerVisionState state;
    pipeline_contract::UserAimIntent user_intent;
    std::vector<pipeline_contract::VisionCandidateSnapshot> candidates;
    std::vector<tracking_native::TrackerDetection> tracker_detections;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    std::uint64_t frame_id = 0;
    double capture_time_seconds = 0.0;
    double ready_time_seconds = 0.0;
    // Independent W5 shadow endpoint. The legacy capture/ready clocks above
    // retain their tracker/controller meaning and must not be replaced.
    std::uint64_t actuator_effect_present_qpc = 0;
    std::uint64_t actuator_effect_present_qpc_frequency = 0;
    std::uint64_t actuator_effect_present_steady_ns = 0;
    std::uint64_t actuator_effect_present_calibration_id = 0;
    std::uint64_t actuator_effect_present_calibration_uncertainty_ns = 0;
    double actuator_effect_present_time_seconds = 0.0;
    bool actuator_effect_present_raw_available = false;
    bool actuator_effect_present_steady_available = false;
    bool actuator_effect_present_time_valid = false;
    std::uint32_t rejected_friendly_count = 0;
    std::uint32_t rejected_low_reliability_count = 0;
};

}  // namespace controller_native
