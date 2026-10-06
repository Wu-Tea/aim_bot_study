#include "ads_acquisition_controller.h"
#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

float smoothstep(float value) noexcept {
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

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
    // TargetCoordinator has already made the binary admission decision. ADS
    // consumes that one authority value directly; geometry confidence remains
    // telemetry and must not silently multiply the configured force again.
    const float authority = std::clamp(plan.aim_authority, 0.0f, 1.0f);
    // The estimator already blends learned evidence with its startup prior.
    // Zero confidence means prior-only, not an absent response estimate.
    const float response = std::isfinite(plan.response_scale) && plan.response_scale >= 50.0f
        ? plan.response_scale : config_.fallback_response_px_per_stick_second;
    const float nominal_horizon = std::clamp(
        config_.arrival_horizon_seconds, 0.060f, 0.350f);
    const float close_horizon = std::clamp(
        config_.close_arrival_horizon_seconds,
        0.040f,
        nominal_horizon);
    const float close_begin = std::clamp(
        config_.close_target_size_begin, 0.0f, 1.0f);
    const float close_full = std::clamp(
        config_.close_target_size_full,
        close_begin + 0.01f,
        1.0f);
    const float close_weight = smoothstep(
        (std::clamp(plan.normalized_size, 0.0f, 1.0f) - close_begin) /
        (close_full - close_begin));
    const float horizon = (nominal_horizon +
        close_weight * (close_horizon - nominal_horizon)) / config_.arrival_speed;
    ResponseModelAimRequest request{};
    request.error_px = plan.error_px;
    request.relative_velocity_px_per_sec = plan.error_rate_px_per_sec;
    request.response_px_per_stick_second = response;
    request.arrival_horizon_seconds = horizon;
    request.arrival_horizon_y_seconds = plan.error_px.y < 0.0f
        ? horizon * std::clamp(
            config_.target_above_horizon_scale, 0.75f, 1.0f)
        : horizon;
    if (config_.range_position_response) {
        request.arrival_horizon_seconds = config_.arrival_horizon_seconds;
        request.arrival_horizon_y_seconds = config_.arrival_horizon_seconds;
    }
    request.motion_weight = config_.stopping_lookahead_seconds /
        request.arrival_horizon_seconds;
    request.motion_is_error_rate_lookahead = true;
    request.max_force = {
        config_.max_force_x * config_.force_headroom,
        config_.max_force_y * config_.force_headroom};
    request.authority = authority;
    request.authority_budget_scale = config_.authority_budget_scale;
    request.response_curve = config_.response_curve;
    request.range_position_response = config_.range_position_response;
    request.position_range_px = plan.position_response_radius_px;
    request.minimum_position_stick = config_.minimum_position_stick;
    request.arrival_radius_px = plan.position_arrival_radius_px;
    return solve_response_model_aim(request).stick;
}

}  // namespace controller_native
