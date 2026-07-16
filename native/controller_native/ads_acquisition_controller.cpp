#include "ads_acquisition_controller.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AdsAcquisitionController::AdsAcquisitionController(AdsAcquisitionControllerConfig config)
    : config_(config) {}

float AdsAcquisitionController::axis(
    float error,
    float error_rate,
    float range,
    float max_force,
    float authority) const noexcept {
    float stopping_error = error;
    if (error * error_rate < 0.0f) {
        stopping_error += error_rate * config_.stopping_lookahead_seconds;
        if (stopping_error * error < 0.0f) stopping_error = 0.0f;
    }
    float output = std::clamp(stopping_error / std::max(1.0f, range), -1.0f, 1.0f);
    output *= max_force * authority;
    return output;
}

pipeline_contract::Vec2f AdsAcquisitionController::compute(
    const pipeline_contract::TargetPlan& plan,
    const pipeline_contract::IntentState& intent,
    float) const noexcept {
    if (plan.mode != pipeline_contract::ControlMode::AdsAcquire ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        return {};
    }
    const float authority = std::clamp(
        std::min(plan.aim_authority, plan.reliability), 0.0f, 1.0f);
    return {
        axis(plan.error_px.x, plan.error_rate_px_per_sec.x,
             config_.error_range_x_px, config_.max_force_x, authority),
        axis(-plan.error_px.y, -plan.error_rate_px_per_sec.y,
             config_.error_range_y_px, config_.max_force_y, authority),
    };
}

}  // namespace controller_native
