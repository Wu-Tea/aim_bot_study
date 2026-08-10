#include "ads_acquisition_controller.h"
#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

constexpr float kVectorForceHeadroom = 1.41421356237f;

}  // namespace

float ads_start_authority(
    float ads_epoch_elapsed_ms,
    float start_delay_ms,
    float start_ramp_ms) noexcept {
    const float elapsed = std::max(0.0f, ads_epoch_elapsed_ms);
    const float delay = std::max(0.0f, start_delay_ms);
    if (elapsed < delay) return 0.0f;
    const float ramp = std::max(0.0f, start_ramp_ms);
    if (ramp <= 0.0f) return 1.0f;
    return std::clamp((elapsed - delay) / ramp, 0.0f, 1.0f);
}

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
    request.arrival_horizon_y_seconds = plan.error_px.y < 0.0f
        ? horizon * std::clamp(
            config_.target_above_horizon_scale, 0.75f, 1.0f)
        : horizon;
    request.motion_weight = config_.stopping_lookahead_seconds /
        horizon;
    request.max_force = {
        config_.max_force_x * kVectorForceHeadroom,
        config_.max_force_y * kVectorForceHeadroom};
    // Start shaping is acquisition-relative.  A target that appears late in
    // the physical ADS epoch still receives the same nominal window.
    const float acquisition_elapsed = plan.target_acquisition_id != 0
        ? plan.acquisition_elapsed_ms
        : plan.ads_epoch_elapsed_ms;
    request.authority = authority * ads_start_authority(
        acquisition_elapsed,
        config_.start_delay_ms,
        config_.start_ramp_ms);
    request.response_curve = config_.response_curve;
    return solve_response_model_aim(request).stick;
}

}  // namespace controller_native
