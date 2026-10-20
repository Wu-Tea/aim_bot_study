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
    const pipeline_contract::TargetPlan& plan) const noexcept {
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
    request.error_px.x += plan.measurement_error_offset_px.x;
    request.error_px.y += plan.measurement_error_offset_px.y;
    request.position_stopping_reserve_px = plan.position_stopping_reserve_px;
    request.relative_velocity_px_per_sec = plan.error_rate_px_per_sec;
    request.relative_velocity_px_per_sec.x += plan.measurement_rate_offset_px_per_sec.x;
    request.relative_velocity_px_per_sec.y += plan.measurement_rate_offset_px_per_sec.y;
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
    // Delivered-work prediction already covers capture age + transport.
    // Only the uncovered part of the original lookahead may brake again.
    const float covered = config_.range_position_response && plan.camera_effect_delay_seconds >= 0.f
        ? plan.source_capture_age_ms*.001f+plan.camera_effect_delay_seconds : 0.f;
    request.motion_weight = std::max(0.f,config_.stopping_lookahead_seconds-covered) /
        request.arrival_horizon_seconds;
    request.motion_is_error_rate_lookahead = true;
    if (config_.range_position_response && plan.bodylock_target_motion_valid &&
        pipeline_contract::finite(plan.bodylock_target_motion_px_per_sec)) {
        request.sustaining_velocity_px_per_sec = plan.bodylock_target_motion_px_per_sec;
    }
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

pipeline_contract::Vec2f AdsAcquisitionController::cooperative_manual_work(
    const pipeline_contract::TargetPlan& plan,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai) const noexcept {
    if (!config_.range_position_response || plan.mode != pipeline_contract::ControlMode::AdsAcquire ||
        plan.aim_authority <= 0.f || plan.observation_gap || plan.source_capture_age_ms > 32.f ||
        !pipeline_contract::finite(manual) || !pipeline_contract::finite(ai)) return {};
    const auto remaining = [](float error, float reserve) {
        return error*reserve > 0.f
            ? std::copysign(std::max(0.f,std::fabs(error)-std::fabs(reserve)),error) : error;
    };
    const pipeline_contract::Vec2f error{
        remaining(plan.error_px.x+plan.measurement_error_offset_px.x,plan.position_stopping_reserve_px.x),
        -remaining(plan.error_px.y+plan.measurement_error_offset_px.y,plan.position_stopping_reserve_px.y)};
    const float response = std::isfinite(plan.response_scale) && plan.response_scale >= 50.f
        ? plan.response_scale : config_.fallback_response_px_per_stick_second;
    const float distance = std::hypot(error.x,error.y);
    const float taper = distance > 0.f ? distance/std::hypot(distance,plan.position_arrival_radius_px) : 0.f;
    const auto motion = plan.bodylock_target_motion_valid && pipeline_contract::finite(plan.bodylock_target_motion_px_per_sec)
        ? plan.bodylock_target_motion_px_per_sec : pipeline_contract::Vec2f{};
    // Use the solver's existing stopping horizon. Capture age and actuator
    // work are already in the reserve; no timer or authority extension here.
    const pipeline_contract::Vec2f limit{
        (motion.x+error.x*taper/kVelocityFeedbackMinimumSeconds)/response,
        (-motion.y+error.y*taper/kVelocityFeedbackMinimumSeconds)/response};
    const auto contribution = [](float m,float a,float e,bool correction) {
        return !correction && m*e>0.f && m*a>=0.f && std::fabs(m)>std::fabs(a) ? m-a : 0.f;
    };
    pipeline_contract::Vec2f delta{
        contribution(manual.x,ai.x,error.x,plan.manual_correction_x),
        contribution(manual.y,ai.y,error.y,plan.manual_correction_y)};
    if (delta.x==0.f && delta.y==0.f) return {};
    const auto base = forward_aim_response_curve(ai,config_.response_curve);
    const auto fits_axis = [](float value,float original,float bound,float direction) {
        // An existing proposal is owned by its solver. Only additional work
        // must fit; the coupled response curve must not accelerate the other
        // axis beyond its remaining work either.
        if (direction==0.f) return std::fabs(value-original)<1e-6f;
        return (value-original)*direction <= std::max(0.f,(bound-original)*direction)+1e-6f;
    };
    const auto fits = [&](float fraction) {
        const auto v=forward_aim_response_curve({ai.x+delta.x*fraction,ai.y+delta.y*fraction},config_.response_curve);
        return fits_axis(v.x,base.x,limit.x,error.x) && fits_axis(v.y,base.y,limit.y,error.y);
    };
    float fraction=1.f;
    if (!fits(1.f)) {
        float lo=0.f,hi=1.f;
        for(int i=0;i<16;++i) { const float mid=(lo+hi)*.5f; if(fits(mid)) lo=mid; else hi=mid; }
        fraction=lo;
    }
    return {delta.x*fraction,delta.y*fraction};
}

}  // namespace controller_native
