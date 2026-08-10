#pragma once

#include "../pipeline_contract/target_plan.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

// Keeps the target solver as the sole direction owner while allowing physical
// input that agrees with the solved target vector to add a small, bounded
// amount of force. This is intended to cross an in-game aim-assist slowdown
// bubble without restoring unbounded manual + AI addition.
inline pipeline_contract::Vec2f apply_helpful_manual_overdrive(
    pipeline_contract::Vec2f target_stick,
    pipeline_contract::Vec2f physical_manual_stick,
    float max_scale,
    pipeline_contract::Vec2f current_target_error_stick = {},
    float direction_weight = 0.45f) {
    const float target_magnitude = std::hypot(target_stick.x, target_stick.y);
    const float manual_magnitude = std::hypot(
        physical_manual_stick.x, physical_manual_stick.y);
    if (!std::isfinite(target_magnitude) ||
        !std::isfinite(manual_magnitude) ||
        target_magnitude <= 1.0e-5f || manual_magnitude <= 1.0e-5f) {
        return target_stick;
    }

    const float bounded_scale = std::clamp(max_scale, 1.0f, 1.25f);
    if (bounded_scale <= 1.0f) return target_stick;

    const float current_error_magnitude = std::hypot(
        current_target_error_stick.x,
        current_target_error_stick.y);
    const pipeline_contract::Vec2f current_error_direction =
        std::isfinite(current_error_magnitude) &&
            current_error_magnitude > 1.0e-5f
        ? pipeline_contract::Vec2f{
            current_target_error_stick.x / current_error_magnitude,
            current_target_error_stick.y / current_error_magnitude}
        : pipeline_contract::Vec2f{
            target_stick.x / target_magnitude,
            target_stick.y / target_magnitude};
    const float helpful_projection =
        physical_manual_stick.x * current_error_direction.x +
        physical_manual_stick.y * current_error_direction.y;
    if (!std::isfinite(helpful_projection) || helpful_projection <= 0.0f) {
        return target_stick;
    }

    // Direction evidence and total-force headroom are separate contracts.
    // Manual may steer T noticeably toward the current target, while the
    // final actuator magnitude remains capped by max_scale below.
    const float guidance_fraction = std::clamp(direction_weight, 0.0f, 0.75f);
    pipeline_contract::Vec2f candidate{
        target_stick.x + physical_manual_stick.x * guidance_fraction,
        target_stick.y + physical_manual_stick.y * guidance_fraction};

    const auto direction_alignment = [&](pipeline_contract::Vec2f value) {
        const float magnitude = std::hypot(value.x, value.y);
        if (!std::isfinite(magnitude) || magnitude <= 1.0e-5f) return -1.0f;
        return (value.x * current_error_direction.x +
                value.y * current_error_direction.y) / magnitude;
    };
    const float original_alignment = direction_alignment(target_stick);
    if (direction_alignment(candidate) + 1.0e-5f < original_alignment) {
        // The physical vector contains a useful radial component but its
        // tangent would move T farther from the current target.  Retain only
        // the evidence that improves target closure.
        candidate = {
            target_stick.x + current_error_direction.x *
                helpful_projection * guidance_fraction,
            target_stick.y + current_error_direction.y *
                helpful_projection * guidance_fraction};
    }

    const float maximum_magnitude = std::min(
        1.0f, target_magnitude * bounded_scale);
    const float candidate_magnitude = std::hypot(candidate.x, candidate.y);
    if (!std::isfinite(candidate_magnitude) || candidate_magnitude <= 1.0e-5f) {
        return target_stick;
    }
    const float scale = std::min(1.0f, maximum_magnitude / candidate_magnitude);
    return {candidate.x * scale, candidate.y * scale};
}

// Manual is evidence for the one final target output T, not a second force.
// Keep ordinary position closure on the established 1.10x..1.20x envelope,
// but admit two meanings that current-position projection alone cannot see:
// pursuit in the measured target-motion direction and vertical correction
// while firing. Their configured direction weight is an absolute intent
// bound, so a target T that is already underestimated cannot reduce useful
// physical control to almost zero.
inline pipeline_contract::Vec2f apply_target_guided_manual_intent(
    pipeline_contract::Vec2f target_stick,
    pipeline_contract::Vec2f physical_manual_stick,
    float max_scale,
    pipeline_contract::Vec2f current_target_error_stick,
    pipeline_contract::Vec2f target_motion_stick,
    bool firing,
    float direction_weight) {
    const auto position_guided = apply_helpful_manual_overdrive(
        target_stick,
        physical_manual_stick,
        max_scale,
        current_target_error_stick,
        direction_weight);
    if (!pipeline_contract::finite(position_guided) ||
        !pipeline_contract::finite(physical_manual_stick) ||
        !pipeline_contract::finite(target_motion_stick)) {
        return target_stick;
    }

    const float manual_magnitude = std::hypot(
        physical_manual_stick.x, physical_manual_stick.y);
    const float guidance_fraction = std::clamp(
        direction_weight, 0.0f, 0.75f);
    if (!std::isfinite(manual_magnitude) || manual_magnitude <= 0.02f ||
        guidance_fraction <= 0.0f) {
        return position_guided;
    }

    pipeline_contract::Vec2f validated_manual{};
    bool has_validated_intent = false;
    const float motion_magnitude = std::hypot(
        target_motion_stick.x, target_motion_stick.y);
    constexpr float kMinimumPursuitMotionPxPerSecond = 40.0f;
    if (std::isfinite(motion_magnitude) &&
        motion_magnitude >= kMinimumPursuitMotionPxPerSecond) {
        const pipeline_contract::Vec2f motion_direction{
            target_motion_stick.x / motion_magnitude,
            target_motion_stick.y / motion_magnitude};
        const float pursuit_projection =
            physical_manual_stick.x * motion_direction.x +
            physical_manual_stick.y * motion_direction.y;
        if (std::isfinite(pursuit_projection) &&
            pursuit_projection > 0.02f) {
            validated_manual = {
                motion_direction.x * pursuit_projection,
                motion_direction.y * pursuit_projection};
            has_validated_intent = true;
        }
    }

    // Downward physical stick is a deliberate recoil/aim-point request only
    // while fire is physically active. It may strengthen or reverse the
    // target-only Y proposal, but remains bounded by direction_weight below.
    if (firing && physical_manual_stick.y < -0.02f) {
        if (std::fabs(physical_manual_stick.y) >
            std::fabs(validated_manual.y)) {
            validated_manual.y = physical_manual_stick.y;
        }
        has_validated_intent = true;
    }
    if (!has_validated_intent) return position_guided;

    const pipeline_contract::Vec2f candidate{
        position_guided.x + validated_manual.x * guidance_fraction,
        position_guided.y + validated_manual.y * guidance_fraction};
    const float candidate_magnitude = std::hypot(candidate.x, candidate.y);
    const float target_magnitude = std::hypot(target_stick.x, target_stick.y);
    const float position_guided_magnitude = std::hypot(
        position_guided.x, position_guided.y);
    const float validated_magnitude = std::hypot(
        validated_manual.x, validated_manual.y);
    if (!std::isfinite(candidate_magnitude) ||
        !std::isfinite(target_magnitude) ||
        !std::isfinite(position_guided_magnitude) ||
        !std::isfinite(validated_magnitude) ||
        candidate_magnitude <= 1.0e-5f) {
        return position_guided;
    }

    const float bounded_scale = std::clamp(max_scale, 1.0f, 1.25f);
    const float target_headroom = target_magnitude * bounded_scale;
    const float validated_intent_bound =
        validated_magnitude * guidance_fraction;
    const float maximum_magnitude = std::min(
        1.0f,
        std::max({
            position_guided_magnitude,
            target_headroom,
            validated_intent_bound}));
    const float scale = std::min(
        1.0f, maximum_magnitude / candidate_magnitude);
    return {candidate.x * scale, candidate.y * scale};
}

// Cue continuation is weak geometry evidence. It may guide the actuator, but
// it must not erase a useful physical correction just because the solved cue
// target T is small or points the wrong way. Keep that correction bounded by
// the configured 40-50% budget and by the larger of T or that budget, so this
// remains a bounded final-output correction rather than additive manual+AI.
inline pipeline_contract::Vec2f apply_bounded_cue_manual_correction(
    pipeline_contract::Vec2f target_stick,
    pipeline_contract::Vec2f physical_manual_stick,
    float direction_weight) {
    if (!pipeline_contract::finite(target_stick) ||
        !pipeline_contract::finite(physical_manual_stick)) {
        return target_stick;
    }
    const float manual_magnitude = std::hypot(
        physical_manual_stick.x, physical_manual_stick.y);
    if (!std::isfinite(manual_magnitude) || manual_magnitude <= 1.0e-5f) {
        return target_stick;
    }

    const float correction_budget = std::clamp(direction_weight, 0.0f, 0.75f);
    if (correction_budget <= 0.0f) return target_stick;
    const pipeline_contract::Vec2f candidate{
        target_stick.x + physical_manual_stick.x * correction_budget,
        target_stick.y + physical_manual_stick.y * correction_budget};
    const float target_magnitude = std::hypot(
        target_stick.x, target_stick.y);
    const float candidate_magnitude = std::hypot(
        candidate.x, candidate.y);
    if (!std::isfinite(target_magnitude) ||
        !std::isfinite(candidate_magnitude) ||
        candidate_magnitude <= 1.0e-5f) {
        return target_stick;
    }

    const float maximum_magnitude = std::min(
        1.0f, std::max(target_magnitude, correction_budget));
    const float scale = std::min(1.0f, maximum_magnitude / candidate_magnitude);
    return {candidate.x * scale, candidate.y * scale};
}

}  // namespace controller_native
