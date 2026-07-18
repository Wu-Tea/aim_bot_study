#include "ads_acquisition_controller.h"
#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

constexpr float kVectorForceHeadroom = 1.41421356237f;

}  // namespace

AdsAcquisitionController::AdsAcquisitionController(AdsAcquisitionControllerConfig config)
    : config_(config) {}

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
    const float response = plan.response_confidence > 0.0f && plan.response_scale >= 50.0f
        ? plan.response_scale : config_.fallback_response_px_per_stick_second;
    const float horizon = std::clamp(
        config_.arrival_horizon_seconds, 0.060f, 0.350f);
    ResponseModelAimRequest request{};
    request.error_px = plan.error_px;
    request.relative_velocity_px_per_sec = plan.error_rate_px_per_sec;
    request.response_px_per_stick_second = response;
    request.arrival_horizon_seconds = horizon;
    request.arrival_horizon_y_seconds = horizon;
    request.motion_weight = config_.stopping_lookahead_seconds /
        horizon;
    request.max_force = {
        config_.max_force_x * kVectorForceHeadroom,
        config_.max_force_y * kVectorForceHeadroom};
    request.authority = authority;
    auto output = solve_response_model_aim(request).stick;
    const pipeline_contract::Vec2f manual{
        intent.filtered_right.x, intent.filtered_right.y};
    if (output.x * manual.x + output.y * manual.y < 0.0f) {
        const float confidence = std::clamp(
            std::max(intent.right_x.confidence, intent.right_y.confidence), 0.0f, 1.0f);
        const float reduction = 1.0f - config_.opposing_manual_reduction * confidence;
        output.x *= reduction;
        output.y *= reduction;
    }
    return output;
}

}  // namespace controller_native
