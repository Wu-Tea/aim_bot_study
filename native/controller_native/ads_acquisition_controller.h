#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct AdsAcquisitionControllerConfig {
    float max_force_x = 1.0f;
    float max_force_y = 1.0f;
    float error_range_x_px = 130.0f;
    float error_range_y_px = 90.0f;
    float stopping_lookahead_seconds = 0.012f;
    float opposing_manual_reduction = 0.8f;
};

class AdsAcquisitionController {
public:
    explicit AdsAcquisitionController(AdsAcquisitionControllerConfig config = {});

    pipeline_contract::Vec2f compute(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::IntentState& intent,
        float dt_seconds) const noexcept;

private:
    float axis(
        float error,
        float error_rate,
        float manual,
        float manual_confidence,
        float range,
        float max_force,
        float authority) const noexcept;

    AdsAcquisitionControllerConfig config_{};
};

}  // namespace controller_native
