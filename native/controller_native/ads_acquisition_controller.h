#pragma once

#include "aim_response_curve_plugin.h"
#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct AdsAcquisitionControllerConfig {
    float max_force_x = 1.0f;
    float max_force_y = 1.0f;
    float arrival_horizon_seconds = 0.160f;
    float fallback_response_px_per_stick_second = 500.0f;
    float stopping_lookahead_seconds = 0.012f;
    float opposing_manual_reduction = 0.8f;
    float start_delay_ms = 0.0f;
    float start_ramp_ms = 0.0f;
    AimResponseCurveConfig response_curve{};
};

float ads_start_authority(
    float ads_epoch_elapsed_ms,
    float start_delay_ms,
    float start_ramp_ms) noexcept;

class AdsAcquisitionController {
public:
    explicit AdsAcquisitionController(AdsAcquisitionControllerConfig config = {});

    pipeline_contract::Vec2f compute(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::IntentState& intent,
        float dt_seconds) const noexcept;

private:
    AdsAcquisitionControllerConfig config_{};
};

}  // namespace controller_native
