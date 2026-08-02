#pragma once

#include "pipeline_contract/target_plan.h"

namespace controller_native {

enum class ResponseModelConstraintReason : unsigned char {
    None,
    FreshPositionRadialMotionBound,
    LifecycleStaleMotionDiscarded,
};

struct ResponseModelAimRequest {
    pipeline_contract::Vec2f error_px{};
    pipeline_contract::Vec2f relative_velocity_px_per_sec{};
    // A displacement-derived proposal that must remain in the motion branch.
    // It is already expressed in controller-stick units so callers can keep
    // its source separate from authoritative position pixels.
    pipeline_contract::Vec2f motion_feedforward_stick{};
    float response_px_per_stick_second = 500.0f;
    float arrival_horizon_seconds = 0.050f;
    float arrival_horizon_y_seconds = 0.0f;
    float motion_weight = 1.0f;
    pipeline_contract::Vec2f max_force{1.0f, 1.0f};
    float authority = 1.0f;
    bool fresh_position_authoritative = false;
};

struct ResponseModelAimOutput {
    pipeline_contract::Vec2f position_stick{};
    pipeline_contract::Vec2f motion_stick{};
    pipeline_contract::Vec2f bounded_motion_stick{};
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
