#pragma once

#include "../common_native/screen_geometry.h"

#include "virtual_gamepad.h"

#include <cstdint>
#include <string>

namespace controller_native {

struct NativeControllerOutputComponents {
    common_native::Vec2f physical_stick;
    common_native::Vec2f manual_stick;
    // Target-first diagnostics: M is the physical proposal, while T is the
    // single pre-recoil aim output.  `ai_correction_stick` is only T-M; it is
    // not a second actuator contribution.
    common_native::Vec2f target_final_stick;
    common_native::Vec2f ai_correction_stick;
    const char* manual_authority_mode = "no_target_passthrough";
    common_native::Vec2f filtered_manual_stick;
    float manual_confidence = 0.0f;
    // Operation-pattern model (§4.5): the recognized user operation this tick
    // and its trust signal, consumed by diagnostics and telemetry.
    std::string operation_class = "no_gesture";
    float operation_confidence = 0.0f;
    float direction_trust = 0.5f;
    float recoil_pull_strength = 0.0f;
    common_native::Vec2f ai_aim_stick;
    common_native::Vec2f requested_assist_stick;
    common_native::Vec2f shaped_assist_stick;
    std::string assist_control_phase = "manual";
    bool manual_passthrough_x = true;
    bool manual_passthrough_y = true;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_boundary_x = false;
    bool manual_boundary_y = false;
    bool manual_exit_requested = false;
    bool handover_requested = false;
    bool handover_braking = false;
    common_native::Vec2f bodylock_error_rate_px_per_sec;
    common_native::Vec2f bodylock_position_stick;
    common_native::Vec2f bodylock_motion_stick;
    common_native::Vec2f bodylock_effective_motion_stick;
    bool bodylock_radial_motion_bound = false;
    std::string bodylock_constraint_reason = "none";
    common_native::Vec2f observed_error_px;
    common_native::Vec2f control_error_px;
    common_native::Vec2f source_aim_px;
    common_native::Vec2f desired_aim_px;
    common_native::Vec2f desired_point_normalized;
    common_native::Box2f aim_region_px;
    bool has_aim_region = false;
    float visual_authority = 0.0f;
    bool enemy_cue_current = false;
    bool enemy_identity_confirmed = false;
    bool enemy_cue_checked = false;
    std::string aim_region_source = "none";
    std::string desired_point_source = "none";
    common_native::Vec2f before_recoil_stick;
    common_native::Vec2f recoil_stick;
    common_native::Vec2f final_stick;
    std::string aim_mode = "none";
    std::string assist_authority = "reject";
    std::string assist_authority_reason = "none";
    std::string bodylock_lifecycle = "inactive";
    std::string assist_limit_reason = "none";
    bool auto_fire_requested = false;
    bool auto_fire_aim_ready = false;
    bool auto_fire_allowed = false;
    bool auto_fire_active = false;
    std::uint64_t auto_fire_pulse_starts = 0;
    bool auto_fire_pulse_pressed = false;
    bool auto_fire_cadence_wait = false;
    std::string auto_fire_block_reason = "none";
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
