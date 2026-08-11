#pragma once

#include "../common_native/screen_geometry.h"
#include "../common_native/time_types.h"
#include "../common_native/authority_types.h"
#include "pipeline_contract/user_aim_intent.h"
#include "pipeline_contract/vision_observation.h"

#include <cstdint>

namespace pipeline_contract {

struct VisionCandidateSnapshot {
    std::uint64_t id = 0;
    bool valid = false;
    common_native::Box2f body_box_px;
    common_native::Vec2f aim_point_px;
    bool has_aim_point = false;
    common_native::Box2f aim_region_px;
    AimRegionSource aim_region_source = AimRegionSource::None;
    bool has_aim_region = false;
    float confidence = 0.0f;
    int class_id = 0;
    bool is_friendly = false;
    bool color_classified = false;
    float color_bonus = 0.0f;
    bool has_cue_point = false;
    common_native::Vec2f cue_point_px;
    float cue_score = 0.0f;
    bool has_motion_anchor = false;
    common_native::Vec2f motion_anchor_px;
    float motion_anchor_score = 0.0f;
    common_native::TargetAuthorityState suggested_authority_state =
        common_native::TargetAuthorityState::Reject;
};

struct ControllerTargetSnapshot {
    std::uint64_t frame_id = 0;
    bool frame_updated = false;
    bool has_target = false;
    bool aim_authority = false;
    bool fire_authority = false;
    common_native::Vec2f aim_error_px;
    common_native::Vec2f target_px;
    common_native::Vec2f screen_center_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    common_native::Box2f aim_region_px;
    bool has_aim_region = false;
    common_native::TimeSeconds captured_at;
    common_native::TimeSeconds ready_at;
};

}  // namespace pipeline_contract
