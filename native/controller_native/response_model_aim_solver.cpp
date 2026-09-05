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
        request.arrival_horizon_seconds, 0.005f, 1.0f);
    const float horizon_y = std::clamp(
        request.arrival_horizon_y_seconds > 0.0f
            ? request.arrival_horizon_y_seconds
            : request.arrival_horizon_seconds,
        0.005f, 1.0f);
    const float authority = std::clamp(request.authority, 0.0f, 1.0f);
    const float motion_weight = std::clamp(request.motion_weight, 0.0f, 2.0f);
    const pipeline_contract::Vec2f control_error{
        request.error_px.x, -request.error_px.y};
    const pipeline_contract::Vec2f control_velocity{
        request.relative_velocity_px_per_sec.x,
        -request.relative_velocity_px_per_sec.y};
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
        if (request.motion_is_error_rate_lookahead && position * motion >= 0.0f) {
            // Use the same position-owned neighborhood on both sides of zero.
            // The former opposing-only envelope tended to zero on approach,
            // then released the entire lookahead at zero/after crossing.
            const float bounded = motion * smoothstep(
                std::fabs(position) / kNearCenterPositionStick);
            bound_applied = bound_applied || std::fabs(bounded - motion) > 1e-6f;
            return bounded;
        }
        if ((!request.motion_is_error_rate_lookahead && std::fabs(position) <= 1.0e-5f) ||
            position * motion >= 0.0f) {
            return motion;
        }

        // ADS motion is a stopping lookahead of this source error, so retain
        // its full braking contribution up to cancellation of position. The
        // BodyLock feed-forward envelope erased this brake outside the small
        // center neighborhood, leaving an approaching target driven as hard
        // as a stationary one. Neither policy may predict through position
        // and reverse the requested axis; sustained BodyLock keeps its own
        // near-center feed-forward constraint.
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
    const float max_x = std::min(
        std::max(0.0f, request.max_force.x), authority);
    const float max_y = std::min(
        std::max(0.0f, request.max_force.y), authority);
    if (max_x <= 0.0f) output.unclamped_stick.x = 0.0f;
    if (max_y <= 0.0f) output.unclamped_stick.y = 0.0f;
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
    output.limited = ellipse_length > 1.0f;
    return output;
}

}  // namespace controller_native
