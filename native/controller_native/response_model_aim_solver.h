#pragma once

#include "aim_response_curve_plugin.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

enum class ResponseModelConstraintReason : unsigned char {
    None,
    PositionRadialMotionBound,
};

struct ResponseModelAimRequest {
    pipeline_contract::Vec2f error_px{};
    pipeline_contract::Vec2f relative_velocity_px_per_sec{};
    float response_px_per_stick_second = 500.0f;
    float arrival_horizon_seconds = 0.050f;
    float arrival_horizon_y_seconds = 0.0f;
    float motion_weight = 1.0f;
    // Screen-error lookahead contains camera/FOV motion; it may shape a
    // position correction but cannot independently own the centered axis.
    // Sustaining target-motion demand (BodyLock) retains its centered work.
    bool motion_is_error_rate_lookahead = false;
    // Only a capture-aligned target-motion estimate may own centered work.
    // Raw screen-rate hints retain position ownership, including at zero.
    bool motion_is_sustaining_target_motion = false;
    // Mouse BodyLock point policy. Zero retains the shared controller policy.
    // A positive per-axis tolerance owns both position and motion demand.
    float point_tolerance_px = 0.0f;
    pipeline_contract::Vec2f max_force{1.0f, 1.0f};
    float authority = 1.0f;
    float authority_budget_scale = 1.0f;
    AimResponseCurveConfig response_curve{};
};

struct ResponseModelAimOutput {
    pipeline_contract::Vec2f position_stick{};
    pipeline_contract::Vec2f motion_stick{};
    pipeline_contract::Vec2f bounded_motion_stick{};
    // Desired camera response before the inverse curve. unclamped_stick is the
    // resulting final target T before the force envelope.
    pipeline_contract::Vec2f pre_curve_stick{};
    pipeline_contract::Vec2f unclamped_stick{};
    pipeline_contract::Vec2f stick{};
    bool limited = false;
    bool radial_motion_bound_applied = false;
    ResponseModelConstraintReason radial_motion_bound_reason =
        ResponseModelConstraintReason::None;
};

ResponseModelAimOutput solve_response_model_aim(
    const ResponseModelAimRequest& request) noexcept;

}  // namespace controller_native
