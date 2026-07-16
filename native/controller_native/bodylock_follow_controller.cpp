#include "bodylock_follow_controller.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

BodylockFollowController::BodylockFollowController(BodylockFollowControllerConfig config)
    : config_(config) {}

float BodylockFollowController::axis(
    float error,
    float error_rate,
    float manual,
    float manual_confidence,
    float feedback_range,
    float max_force,
    float authority,
    float response_scale) const noexcept {
    const float scale = std::fabs(response_scale) >= 50.0f
        ? std::fabs(response_scale)
        : config_.fallback_response_px_per_stick_second;
    float stopping_error = error;
    if (error * error_rate < 0.0f) {
        stopping_error += error_rate * config_.stopping_lookahead_seconds;
        if (stopping_error * error < 0.0f) stopping_error = 0.0f;
    }
    const float feedback = stopping_error / std::max(1.0f, feedback_range);
    const float feedforward = error_rate / scale * config_.feedforward_gain;
    float combined = std::clamp(feedback + feedforward, -1.0f, 1.0f);
    if (error > 0.0f) combined = std::max(0.0f, combined);
    if (error < 0.0f) combined = std::min(0.0f, combined);
    float output = combined * max_force * authority;
    if (output * manual < 0.0f) {
        output *= 1.0f - config_.opposing_manual_reduction *
            std::clamp(manual_confidence, 0.0f, 1.0f);
    }
    return output;
}

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
    const float strafe_blend = std::clamp(
        std::fabs(intent.filtered_left.x) * 2.0f, 0.0f, 1.0f);
    const float feedback_range_x = config_.feedback_range_x_px +
        (std::max(config_.feedback_range_x_px,
                  config_.strafing_feedback_range_x_px) -
         config_.feedback_range_x_px) * strafe_blend;
    return {
        axis(plan.error_px.x, plan.error_rate_px_per_sec.x,
             intent.filtered_right.x, intent.right_confidence,
             feedback_range_x, config_.max_force_x,
             authority, plan.response_scale),
        axis(-plan.error_px.y, -plan.error_rate_px_per_sec.y,
             intent.filtered_right.y, intent.right_confidence,
             config_.feedback_range_y_px, config_.max_force_y,
             authority, plan.response_scale),
    };
}

}  // namespace controller_native
