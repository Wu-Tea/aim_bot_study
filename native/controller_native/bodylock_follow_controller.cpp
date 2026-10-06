#include "bodylock_follow_controller.h"
#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

BodylockFollowController::BodylockFollowController(BodylockFollowControllerConfig config)
    : config_(config) {
    if (config_.response_time_x_seconds < 0.0f)
        config_.response_time_x_seconds = config_.feedback_range_x_px /
            std::max(1.0f, config_.max_force_x * config_.fallback_response_px_per_stick_second);
    if (config_.response_time_y_seconds < 0.0f)
        config_.response_time_y_seconds = config_.feedback_range_y_px /
            std::max(1.0f, config_.max_force_y * config_.fallback_response_px_per_stick_second);
}

pipeline_contract::Vec2f BodylockFollowController::compute(
    const pipeline_contract::TargetPlan& plan,
    const pipeline_contract::IntentState& intent,
    float dt_seconds) const noexcept {
    return compute_detailed(plan, intent, dt_seconds).stick;
}

BodylockFollowControllerOutput BodylockFollowController::compute_detailed(
    const pipeline_contract::TargetPlan& plan,
    const pipeline_contract::IntentState&,
    float) const noexcept {
    BodylockFollowControllerOutput result{};
    const bool target_motion_total_valid =
        plan.bodylock_target_motion_valid &&
        pipeline_contract::finite(
            plan.bodylock_target_motion_px_per_sec);
    result.error_rate_px_per_sec = target_motion_total_valid
        ? plan.bodylock_target_motion_px_per_sec
        : plan.error_rate_px_per_sec;
    if (plan.mode != pipeline_contract::ControlMode::BodyLockFollow ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        return result;
    }
    const float authority = std::clamp(
        std::min(plan.aim_authority, plan.reliability), 0.0f, 1.0f);
    // Consume the estimator's prior even before it has learned samples.
    // Plant response converts demand; independent planning times stay fixed.
    const float response = std::isfinite(plan.response_scale) && plan.response_scale >= 50.0f
        ? std::fabs(plan.response_scale) : config_.fallback_response_px_per_stick_second;
    result.response_max_force = {config_.max_force_x, config_.max_force_y};
    result.response_horizon_seconds = config_.response_time_x_seconds;
    result.response_horizon_y_seconds = config_.response_time_y_seconds;
    result.response_envelope_valid = true;
    result.response_envelope_source = "bodylock_response_model";
    ResponseModelAimRequest request{};
    request.error_px = plan.error_px;
    request.point_tolerance_px = config_.bodylock_point_tolerance_px;
    request.relative_velocity_px_per_sec = result.error_rate_px_per_sec;
    request.response_px_per_stick_second = response;
    request.arrival_horizon_seconds = config_.response_time_x_seconds;
    request.arrival_horizon_y_seconds = config_.response_time_y_seconds;
    // A valid observer value is already the total sustaining motion of the
    // target, not a residual hint. The legacy screen-relative fallback keeps
    // its conservative gain until enough aligned evidence exists.
    request.motion_weight = target_motion_total_valid
        ? 1.0f
        : config_.feedforward_gain;
    request.motion_is_sustaining_target_motion = target_motion_total_valid;
    request.max_force = {config_.max_force_x, config_.max_force_y};
    request.authority = authority;
    request.authority_budget_scale = config_.authority_budget_scale;
    request.response_curve = config_.response_curve;
    request.range_position_response = config_.range_position_response;
    request.position_range_px = plan.position_response_radius_px;
    request.minimum_position_stick = config_.minimum_position_stick;
    request.arrival_radius_px = plan.position_arrival_radius_px;
    const auto solved = solve_response_model_aim(request);
    result.position_stick = solved.position_stick;
    result.motion_stick = solved.motion_stick;
    const bool observed_motion = target_motion_total_valid &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed;
    result.position_hold_x = observed_motion &&
        solved.position_stick.x == 0.0f && solved.motion_stick.x == 0.0f;
    result.position_hold_y = observed_motion &&
        solved.position_stick.y == 0.0f && solved.motion_stick.y == 0.0f;
    result.effective_motion_stick = solved.bounded_motion_stick;
    result.radial_motion_bound_applied = solved.radial_motion_bound_applied;
    result.constraint_reason = solved.radial_motion_bound_reason;
    result.stick = solved.stick;
    return result;
}

}  // namespace controller_native
