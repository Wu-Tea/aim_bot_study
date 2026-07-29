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
    const pipeline_contract::Vec2f player_motion_forecast{
        plan.player_motion_forecast_px.x *
            plan.player_motion_confidence,
        plan.player_motion_forecast_px.y *
            plan.player_motion_confidence,
    };
    // ADS owns point acquisition, not trajectory lead. Let a causal player
    // motion forecast consume most of the current error, but never make ADS
    // command through the observed point before a new observation/BodyLock
    // confirms the crossing.
    request.error_px.x += std::clamp(
        player_motion_forecast.x,
        -std::fabs(plan.error_px.x) * 0.85f,
        std::fabs(plan.error_px.x) * 0.85f);
    request.error_px.y += std::clamp(
        player_motion_forecast.y,
        -std::fabs(plan.error_px.y) * 0.85f,
        std::fabs(plan.error_px.y) * 0.85f);
    request.relative_velocity_px_per_sec = plan.error_rate_px_per_sec;
    request.response_px_per_stick_second = response;
    request.arrival_horizon_seconds = horizon;
    request.arrival_horizon_y_seconds = horizon;
    request.motion_weight = config_.stopping_lookahead_seconds /
        horizon;
    request.max_force = {
        config_.max_force_x * kVectorForceHeadroom,
        config_.max_force_y * kVectorForceHeadroom};
    request.authority = authority * ads_start_authority(
        plan.ads_epoch_elapsed_ms,
        config_.start_delay_ms,
        config_.start_ramp_ms);
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
