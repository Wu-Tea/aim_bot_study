#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

float smoothstep(float value) noexcept {
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

}  // namespace

ResponseModelAimOutput solve_response_model_aim(
    const ResponseModelAimRequest& request) noexcept {
    ResponseModelAimOutput output;
    const float response = std::max(
        1.0f, std::fabs(request.response_px_per_stick_second));
    const float horizon = std::clamp(
        request.arrival_horizon_seconds, kArrivalHorizonMinimumSeconds, kArrivalHorizonMaximumSeconds);
    const float horizon_y = std::clamp(
        request.arrival_horizon_y_seconds > 0.0f
            ? request.arrival_horizon_y_seconds
            : request.arrival_horizon_seconds,
        kArrivalHorizonMinimumSeconds, kArrivalHorizonMaximumSeconds);
    const float authority = std::clamp(request.authority, 0.0f, 1.0f);
    const float motion_weight = std::clamp(request.motion_weight, 0.0f, 2.0f);
    const bool point_policy = request.point_tolerance_px > 0.0f;
    const auto point_error = [&](float error) noexcept {
        return point_policy && std::fabs(error) <= request.point_tolerance_px
            ? 0.0f : error;
    };
    pipeline_contract::Vec2f control_error{
        point_error(request.error_px.x), -point_error(request.error_px.y)};
    if (request.range_position_response) {
        if (request.max_force.x <= 0.f) control_error.x = 0.f;
        if (request.max_force.y <= 0.f) control_error.y = 0.f;
    }
    const pipeline_contract::Vec2f control_velocity{
        request.range_position_response && request.max_force.x <= 0.f ? 0.f : request.relative_velocity_px_per_sec.x,
        request.range_position_response && request.max_force.y <= 0.f ? 0.f : -request.relative_velocity_px_per_sec.y};
    output.position_stick = {
        control_error.x / (horizon * response),
        control_error.y / (horizon_y * response),
    };
    output.motion_stick = {
        control_velocity.x / response * motion_weight,
        control_velocity.y / response * motion_weight,
    };
    output.bounded_motion_stick = output.motion_stick;
    const auto bound_opposing_axis = [&request](float position, float motion,
                                         bool& bound_applied) noexcept {
        constexpr float kNearCenterPositionStick = 0.12f;
        if (request.point_tolerance_px > 0.0f) {
            // A point tolerance is not a person-box acceptance test. Inside
            // it, neither noisy velocity nor exact-zero feed-forward may
            // restart the axis. Outside it, keep at least half the position
            // correction; preserve the stronger legacy position guarantee
            // farther from center. Motion may help, but cannot dominate D.
            const float magnitude = std::fabs(position);
            const float retention = std::max(0.5f,
                smoothstep(magnitude / kNearCenterPositionStick));
            const float limit = position * motion < 0.0f
                ? magnitude * (1.0f - retention) : magnitude;
            const float bounded = std::clamp(motion, -limit, limit);
            bound_applied = bound_applied || std::fabs(bounded - motion) > 1e-6f;
            return bounded;
        }
        if (request.motion_is_sustaining_target_motion &&
            !request.motion_is_error_rate_lookahead) {
            // Sustaining motion remains necessary on BOTH sides of zero.
            // Blend an opposing estimate out over the existing position
            // neighborhood; a sign change alone must not release a full
            // feed-forward command. Beyond that neighborhood position owns
            // the axis again. This is spatial arbitration, not a time filter.
            const float bounded = position * motion < 0.0f
                ? motion * (1.0f - smoothstep(
                    std::fabs(position) / kNearCenterPositionStick))
                : motion;
            bound_applied = bound_applied || std::fabs(bounded - motion) > 1e-6f;
            return bounded;
        }
        if (position * motion >= 0.0f) {
            // Use the same position-owned neighborhood on both sides of zero.
            // The former opposing-only envelope tended to zero on approach,
            // then released the entire lookahead at zero/after crossing.
            // The same boundary applies to unconfirmed BodyLock screen rate:
            // camera/geometry noise cannot establish sustaining target motion.
            const float bounded = motion * smoothstep(
                std::fabs(position) / kNearCenterPositionStick);
            bound_applied = bound_applied || std::fabs(bounded - motion) > 1e-6f;
            return bounded;
        }
        // ADS motion is a stopping lookahead of this source error, so retain
        // its full braking contribution up to cancellation of position. The
        // BodyLock feed-forward envelope erased this brake outside the small
        // center neighborhood, leaving an approaching target driven as hard
        // as a stationary one. Neither policy may predict through position
        // and reverse the requested axis. Capture-aligned sustaining motion
        // has already taken its separately qualified branch above.
        const float center_envelope = smoothstep(
            std::fabs(position) / kNearCenterPositionStick);
        const float maximum_opposing_motion = std::fabs(position) *
            (request.motion_is_error_rate_lookahead ? 1.0f : 1.0f - center_envelope);
        const float bounded_magnitude = std::min(
            std::fabs(motion), maximum_opposing_motion);
        if (bounded_magnitude + 1.0e-6f < std::fabs(motion)) {
            bound_applied = true;
        }
        return std::copysign(bounded_magnitude, motion);
    };

    bool position_motion_bound_applied = false;
    output.bounded_motion_stick = {
        bound_opposing_axis(output.position_stick.x, output.motion_stick.x,
                            position_motion_bound_applied),
        bound_opposing_axis(output.position_stick.y, output.motion_stick.y,
                            position_motion_bound_applied),
    };
    if (position_motion_bound_applied) {
        // Keep the existing public reason for telemetry/schema compatibility;
        // the constraint is now evaluated independently on each axis.
        output.radial_motion_bound_applied = true;
        output.radial_motion_bound_reason =
            ResponseModelConstraintReason::PositionRadialMotionBound;
    }
    output.pre_curve_stick = {
        (output.position_stick.x + output.bounded_motion_stick.x) * authority,
        (output.position_stick.y + output.bounded_motion_stick.y) * authority,
    };
    output.unclamped_stick = inverse_aim_response_curve(
        output.pre_curve_stick, request.response_curve);

    // Authority is also a delivered per-axis budget, not only a gain before
    // the nonlinear response curve. Intersect it with the mode envelope: clear
    // evidence keeps the existing BodyLock cap, while a large no-cue error
    // cannot inflate weak evidence back into a near-full virtual stick.
    const float authority_budget = authority * request.authority_budget_scale;
    const float max_x = std::min(
        std::max(0.0f, request.max_force.x), authority_budget);
    const float max_y = std::min(
        std::max(0.0f, request.max_force.y), authority_budget);
    if (max_x <= 0.0f) output.unclamped_stick.x = 0.0f;
    if (max_y <= 0.0f) output.unclamped_stick.y = 0.0f;
    bool range_limited = false;
    if (request.range_position_response) {
        // A canonical active plan must carry its coordinator-owned range.
        // Missing geometry cannot manufacture an unrestricted correction.
        if (!std::isfinite(request.position_range_px) || request.position_range_px <= 0.0f)
            return {};
        const float distance = std::hypot(
            max_x > 0.0f ? control_error.x : 0.0f,
            max_y > 0.0f ? control_error.y : 0.0f);
        const float fraction = std::sqrt(std::clamp(distance / request.position_range_px, 0.0f, 1.0f));
        pipeline_contract::Vec2f sustaining{};
        if (request.motion_is_sustaining_target_motion && !request.motion_is_error_rate_lookahead) {
            // Only capture-aligned real target motion can own centered work.
            sustaining = inverse_aim_response_curve({
                output.bounded_motion_stick.x * authority,
                output.bounded_motion_stick.y * authority}, request.response_curve);
        }
        if (max_x <= 0.0f) sustaining.x = 0.0f;
        if (max_y <= 0.0f) sustaining.y = 0.0f;
        pipeline_contract::Vec2f correction{
            output.unclamped_stick.x - sustaining.x,
            output.unclamped_stick.y - sustaining.y};
        float magnitude = std::hypot(correction.x, correction.y);
        const float correction_length = std::hypot(
            max_x > 0.0f ? correction.x / max_x : 0.0f,
            max_y > 0.0f ? correction.y / max_y : 0.0f);
        const float directional_cap = correction_length > 0.0f ? magnitude / correction_length : 0.0f;
        const float minimum = std::min(directional_cap, request.minimum_position_stick);
        const float budget = std::max(directional_cap * fraction, minimum);
        // A pursuit floor is not a license to overrun the point. Bound the
        // remaining position work by a 25 ms stopping horizon (vision age,
        // actuator delay and ordinary output slew). Convert through the same
        // user curve; a nonlinear inverse must not bypass the camera budget.
        // The configured point radius now shapes a continuous final approach,
        // rather than switching position between zero and the pursuit floor.
        constexpr float stopping_seconds = .025f;
        const float taper = distance > 0.f
            ? distance / std::hypot(distance, request.arrival_radius_px) : 0.f;
        const auto braking = inverse_aim_response_curve({
            control_error.x / (response * stopping_seconds),
            control_error.y / (response * stopping_seconds)}, request.response_curve);
        const float braking_budget = std::hypot(braking.x, braking.y) * taper;
        const float delivered = magnitude > 0.f
            ? std::min(std::clamp(magnitude, minimum, budget), braking_budget) : 0.f;
        const float scale = magnitude > 0.f ? delivered / magnitude : 1.f;
        range_limited = magnitude > std::min(budget, braking_budget);
        // Apply in final stick space so a nonlinear inverse response curve
        // cannot expand a tiny position correction back to full force.
        output.unclamped_stick = {
            sustaining.x + correction.x * scale,
            sustaining.y + correction.y * scale};
    }
    const float normalized_x = max_x > 0.0f
        ? output.unclamped_stick.x / max_x : 0.0f;
    const float normalized_y = max_y > 0.0f
        ? output.unclamped_stick.y / max_y : 0.0f;
    const float ellipse_length = std::hypot(normalized_x, normalized_y);
    const float vector_scale = ellipse_length > 1.0f
        ? 1.0f / ellipse_length : 1.0f;
    output.stick = {
        output.unclamped_stick.x * vector_scale,
        output.unclamped_stick.y * vector_scale,
    };
    output.limited = range_limited || ellipse_length > 1.0f;
    return output;
}

}  // namespace controller_native
