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
    std::uint64_t frame_id = 0;
    double capture_time_seconds = 0.0;
    double ready_time_seconds = 0.0;
};

}  // namespace controller_native
