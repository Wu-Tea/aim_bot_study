#pragma once

#include "../common_native/time_types.h"
#include "../pipeline_contract/assist_authority.h"
#include "../tracking_native/tracker_contract.h"

#include "output_mixer.h"
#include "xinput_reader.h"

#include <string>
#include <cstdint>

namespace controller_native {

struct NativeControllerVisionState {
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t selected_track_id = 0;
    std::uint64_t selected_backing_frame_id = 0;
    float track_observation_age_ms = 0.0f;
    float track_position_sigma = 0.0f;
    float track_ambiguity = 0.0f;
    pipeline_contract::AssistAuthorityState assist_authority_state =
        pipeline_contract::AssistAuthorityState::Reject;
    pipeline_contract::AssistAuthorityReason assist_authority_reason =
        pipeline_contract::AssistAuthorityReason::None;
    bool authority_decision_valid = false;
    bool fresh_observation = false;
    bool current_observed_target_present = false;
    bool has_target = false;
    bool auto_fire_requested = false;
    float dx = 0.0f;
    float dy = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float screen_center_x = 0.0f;
    float screen_center_y = 0.0f;
    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    bool aim_authority = false;
    bool fire_authority = false;
    std::string target_tier = "none";
    double observed_at_seconds = 0.0;
    bool has_tracker_projection = false;
    float tracker_dx = 0.0f;
    float tracker_dy = 0.0f;
    bool has_camera_attributed_velocity = false;
    float camera_attributed_velocity_x_px_per_sec = 0.0f;
};

struct NativeControllerTickContext {
    PhysicalGamepadState physical;
    NativeControllerVisionState vision;
    tracking_native::TrackerSnapshot tracker_snapshot;
    common_native::TimeSeconds now;
    common_native::DurationSeconds dt;
    bool aiming = false;
    bool manual_fire_pressed = false;
    bool auto_fire_requested = false;
    bool auto_fire_active = false;
    bool recoil_active = false;
    NativeControllerOutputComponents output_components;
};

}  // namespace controller_native
