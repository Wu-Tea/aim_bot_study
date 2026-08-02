#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "response_model_aim_solver.h"

namespace controller_native {

struct BodylockFollowControllerConfig {
    float max_force_x = 0.42f;
    float max_force_y = 0.42f;
    float feedback_range_x_px = 32.0f;
    float feedback_range_y_px = 32.0f;
    float strafing_feedback_range_x_px = 75.0f;
    float feedforward_gain = 0.64f;
    float stopping_lookahead_seconds = 0.020f;
    float fallback_response_px_per_stick_second = 500.0f;
    float opposing_manual_reduction = 0.6f;
};

struct BodylockFollowControllerOutput {
    pipeline_contract::Vec2f stick{};
    pipeline_contract::Vec2f position_stick{};
    pipeline_contract::Vec2f motion_stick{};
    pipeline_contract::Vec2f effective_motion_stick{};
    pipeline_contract::Vec2f error_rate_px_per_sec{};
    pipeline_contract::Vec2f response_max_force{};
    float response_horizon_seconds = 0.0f;
    float response_horizon_y_seconds = 0.0f;
    bool response_envelope_valid = false;
    const char* response_envelope_source = "unavailable";
    bool radial_motion_bound_applied = false;
    ResponseModelConstraintReason constraint_reason =
        ResponseModelConstraintReason::None;
};

class BodylockFollowController {
public:
    explicit BodylockFollowController(BodylockFollowControllerConfig config = {});

    pipeline_contract::Vec2f compute(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::IntentState& intent,
        float dt_seconds,
        bool fresh_position_authoritative = false) const noexcept;

    BodylockFollowControllerOutput compute_detailed(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::IntentState& intent,
        float dt_seconds,
        bool fresh_position_authoritative = false) const noexcept;

private:
    BodylockFollowControllerConfig config_{};
};

}  // namespace controller_native
