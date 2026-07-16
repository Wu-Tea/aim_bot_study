#include "aim_dynamics_shaper.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AimDynamicsShaper::AimDynamicsShaper(AimDynamicsShaperConfig config)
    : config_(config) {}

float AimDynamicsShaper::shape_axis(
    float requested,
    float manual,
    float manual_confidence,
    float dt) noexcept {
    if (requested * manual < 0.0f) {
        const float confidence = std::clamp(manual_confidence, 0.0f, 1.0f);
        requested *= 1.0f - (1.0f - config_.opposing_manual_scale) * confidence;
    }
    return requested;
}

pipeline_contract::Vec2f AimDynamicsShaper::shape(
    pipeline_contract::Vec2f requested_ai,
    const pipeline_contract::IntentState& intent,
    const pipeline_contract::TargetPlan& plan,
    float dt_seconds) noexcept {
    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        plan.mode == pipeline_contract::ControlMode::Manual) {
        requested_ai = {};
    }
    requested_ai.x = shape_axis(
        requested_ai.x, intent.filtered_right.x, intent.right_confidence, dt);
    requested_ai.y = shape_axis(
        requested_ai.y, intent.filtered_right.y, intent.right_confidence, dt);

    auto slew = [&](float current, float target) {
        const bool decaying = std::fabs(target) < std::fabs(current);
        const float rate = decaying ? config_.decay_slew_per_second : config_.rise_slew_per_second;
        const float step = rate * dt;
        return current + std::clamp(target - current, -step, step);
    };
    current_.x = slew(current_.x, requested_ai.x);
    current_.y = slew(current_.y, requested_ai.y);
    return current_;
}

pipeline_contract::Vec2f AimDynamicsShaper::current() const noexcept {
    return current_;
}

void AimDynamicsShaper::reset() noexcept {
    current_ = {};
}

}  // namespace controller_native
