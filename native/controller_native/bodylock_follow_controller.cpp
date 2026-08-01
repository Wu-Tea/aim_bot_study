#include "bodylock_follow_controller.h"
#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

BodylockFollowController::BodylockFollowController(BodylockFollowControllerConfig config)
    : config_(config) {}

pipeline_contract::Vec2f BodylockFollowController::compute(
    const pipeline_contract::TargetPlan& plan,
    const pipeline_contract::IntentState& intent,
    float) const noexcept {
    if (plan.mode != pipeline_contract::ControlMode::BodyLockFollow ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        return {};
    }
    const float authority = std::clamp(
        std::min(plan.aim_authority, plan.reliability), 0.0f, 1.0f);
    const float response = plan.response_confidence > 0.0f && plan.response_scale >= 50.0f
        ? std::fabs(plan.response_scale) : config_.fallback_response_px_per_stick_second;
    ResponseModelAimRequest request{};
    request.error_px = plan.remaining_work_valid
        ? plan.remaining_work_px
        : plan.error_px;
    constexpr float kPlayerMotionForecastWeight = 0.65f;
    request.error_px.x +=
        plan.player_motion_forecast_px.x *
        plan.player_motion_confidence *
        kPlayerMotionForecastWeight;
    request.error_px.y +=
        plan.player_motion_forecast_px.y *
        plan.player_motion_confidence *
        kPlayerMotionForecastWeight;
    request.relative_velocity_px_per_sec = plan.error_rate_px_per_sec;
    request.response_px_per_stick_second = response;
    request.arrival_horizon_seconds = config_.feedback_range_x_px /
        std::max(1.0f, config_.max_force_x * config_.fallback_response_px_per_stick_second);
    request.arrival_horizon_y_seconds = config_.feedback_range_y_px /
        std::max(1.0f, config_.max_force_y * config_.fallback_response_px_per_stick_second);
    request.motion_weight = config_.feedforward_gain;
    request.max_force = {config_.max_force_x, config_.max_force_y};
    request.authority = authority;
    auto output = solve_response_model_aim(request).stick;
    if (plan.lifecycle != pipeline_contract::TargetLifecycle::Observed) {
        // A scope/FOV transition can hide the target for a few Vision frames.
        // The retained velocity is still useful for continuing toward the last
        // known target, but it is not fresh enough to command a reversal across
        // the residual position error.  Such reversals produced the visible
        // "almost arrived, then pulled back" oscillation during ADS occlusion.
        const pipeline_contract::Vec2f control_error{
            request.error_px.x, -request.error_px.y};
        if (output.x * control_error.x < 0.0f) {
            output.x = 0.0f;
        }
        if (output.y * control_error.y < 0.0f) {
            output.y = 0.0f;
        }
    }
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
