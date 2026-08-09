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
    common_native::Vec2f ai_aim_stick;
    common_native::Vec2f planned_assist_stick;
    common_native::Vec2f body_lock_short_plan_stick;
    common_native::Vec2f output_validation_stick;
    common_native::Vec2f requested_assist_stick;
    common_native::Vec2f shaped_assist_stick;
    common_native::Vec2f axis_intent_intervention;
    common_native::Vec2f axis_intent_wrong_way;
    common_native::Vec2f axis_intent_evidence_stable;
    common_native::Vec2f axis_intent_error_worsening;
    common_native::Vec2f axis_manual_retention{1.0f, 1.0f};
    std::string intent_fusion_mode = "legacy_axis";
    int intent_fusion_candidate = 0;
    float intent_fusion_manual_weight = 1.0f;
    float intent_fusion_ai_weight = 0.0f;
    float intent_fusion_winner_margin = 0.0f;
    bool intent_fusion_fallback = false;
    bool intent_fusion_manual_escape = false;
    bool intent_fusion_fresh_vision_policy_applied = false;
    float intent_fusion_fresh_manual_radial_scale = 1.0f;
    common_native::Vec2f intent_fusion_fresh_validated_manual_proposal;
    common_native::Vec2f intent_fusion_fresh_validated_ai_proposal;
    bool intent_fusion_fresh_ai_radial_bound = false;
    float intent_fusion_fresh_ai_radial_scale = 1.0f;
    bool intent_fusion_predictive_envelope_applied = false;
    bool intent_fusion_fresh_escape_latched = false;
    common_native::Vec2f intent_fusion_fresh_authoritative_error_px;
    common_native::Vec2f intent_fusion_fresh_predicted_error_px;
    float intent_fusion_fresh_raw_manual_radial = 0.0f;
    float intent_fusion_fresh_raw_ai_radial = 0.0f;
    float intent_fusion_fresh_strongest_valid_radial = 0.0f;
    float intent_fusion_fresh_stopping_radial = 0.0f;
    float intent_fusion_fresh_permitted_radial = 0.0f;
    float intent_fusion_fresh_pre_slew_radial = 0.0f;
    float intent_fusion_fresh_final_radial = 0.0f;
    float intent_fusion_fresh_horizon_seconds = 0.0f;
    float intent_fusion_fresh_horizon_y_seconds = 0.0f;
    common_native::Vec2f intent_fusion_fresh_max_force;
    common_native::Vec2f intent_fusion_fresh_envelope_target_stick;
    std::string intent_fusion_fresh_envelope_reason = "none";
    std::string intent_fusion_fresh_envelope_source = "unavailable";
    common_native::Vec2f bodylock_error_rate_px_per_sec;
    common_native::Vec2f bodylock_position_stick;
    common_native::Vec2f bodylock_motion_stick;
    common_native::Vec2f bodylock_effective_motion_stick;
    bool bodylock_radial_motion_bound = false;
    std::string bodylock_constraint_reason = "none";
    common_native::Vec2f dynamic_adjustment_stick;
    common_native::Vec2f post_ai_stick;
    common_native::Vec2f post_dynamic_stick;
    common_native::Vec2f ads_brake_stick;
    common_native::Vec2f post_ads_brake_stick;
    common_native::Vec2f ads_brake_error_px;
    // Causal memory diagnostics are deliberately named by their D/P/R
    // contract.  These are observations of the single final output path,
    // not a second output contribution or an allocation between manual/AI.
    common_native::Vec2f observed_error_px;
    common_native::Vec2f pending_motion_px;
    common_native::Vec2f control_error_px;
    float pending_motion_confidence = 0.0f;
    bool pending_motion_valid = false;
    bool memory_applied = false;
    const char* memory_status = "disabled";
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
