#pragma once

#include "../common_native/screen_geometry.h"

#include "virtual_gamepad.h"

#include <string>

namespace controller_native {

struct NativeControllerOutputComponents {
    common_native::Vec2f physical_stick;
    common_native::Vec2f manual_stick;
    common_native::Vec2f ai_aim_stick;
    common_native::Vec2f planned_assist_stick;
    common_native::Vec2f body_lock_short_plan_stick;
    common_native::Vec2f output_validation_stick;
    common_native::Vec2f requested_assist_stick;
    common_native::Vec2f shaped_assist_stick;
    common_native::Vec2f dynamic_adjustment_stick;
    common_native::Vec2f post_ai_stick;
    common_native::Vec2f post_dynamic_stick;
    common_native::Vec2f ads_brake_stick;
    common_native::Vec2f post_ads_brake_stick;
    common_native::Vec2f ads_brake_error_px;
    common_native::Vec2f ads_carry_brake_stick;
    common_native::Vec2f post_ads_carry_brake_stick;
    bool ads_carry_brake_active = false;
    bool ads_completion_active = false;
    int ads_completion_stable_frames = 0;
    float ads_completion_radius_px = 0.0f;
    int ads_completion_required_frames = 0;
    float ads_completion_max_ms = 0.0f;
    std::string ads_completion_reason = "none";
    common_native::Vec2f before_recoil_stick;
    common_native::Vec2f recoil_stick;
    common_native::Vec2f final_stick;
    std::string aim_mode = "none";
    std::string assist_authority = "reject";
    std::string assist_authority_reason = "none";
    std::string bodylock_lifecycle = "inactive";
    std::string bodylock_transition_reason = "none";
    std::string assist_limit_reason = "none";
    bool ads_brake_active = false;
    bool fire_button = false;
};

NativeControllerOutputComponents output_components_from_manual_output(
    const GamepadOutputState& output);

void capture_output_component_delta(
    const GamepadOutputState& before,
    const GamepadOutputState& after,
    common_native::Vec2f* component);

void capture_final_output_component(
    const GamepadOutputState& output,
    NativeControllerOutputComponents* components);

}  // namespace controller_native
