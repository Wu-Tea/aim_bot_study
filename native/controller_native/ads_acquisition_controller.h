#pragma once

#include "aim_response_curve_plugin.h"
#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct AdsAcquisitionControllerConfig {
    float max_force_x = 1.0f;
    float max_force_y = 1.0f;
    // Production supplies its nominal horizon from ads_snap_window_ms (135 ms
    // by default). Apparent body height then shortens it continuously for CQB,
    // but keeps enough travel time to avoid a near-instant close transfer;
    // authority remains unchanged after target admission.
    float arrival_horizon_seconds = 0.160f;
    float arrival_speed = 1.0f;
    float authority_budget_scale = 1.0f;
    float close_arrival_horizon_seconds = 0.090f;
    float close_target_size_begin = 0.18f;
    float close_target_size_full = 0.40f;
    // Live evidence shows materially slower convergence when the target is
    // above the reticle.  Shorten only that Y horizon; the opposite direction
    // remains the matched counterfactual and keeps its existing response.
    float target_above_horizon_scale = 0.84f;
    float fallback_response_px_per_stick_second = 500.0f;
    float stopping_lookahead_seconds = 0.012f;
    AimResponseCurveConfig response_curve{};
};

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
