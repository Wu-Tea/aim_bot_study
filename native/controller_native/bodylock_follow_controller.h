#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct BodylockFollowControllerConfig {
    float max_force_x = 0.42f;
    float max_force_y = 0.42f;
    float feedback_range_x_px = 32.0f;
    float feedback_range_y_px = 32.0f;
    float strafing_feedback_range_x_px = 75.0f;
    float feedforward_gain = 0.65f;
    float stopping_lookahead_seconds = 0.020f;
    float fallback_response_px_per_stick_second = 500.0f;
};

class BodylockFollowController {
public:
    explicit BodylockFollowController(BodylockFollowControllerConfig config = {});

    pipeline_contract::Vec2f compute(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::IntentState& intent,
        float dt_seconds) const noexcept;

private:
    float axis(
        float error,
        float error_rate,
        float feedback_range,
        float max_force,
        float authority,
        float response_scale) const noexcept;

    BodylockFollowControllerConfig config_{};
};

}  // namespace controller_native
