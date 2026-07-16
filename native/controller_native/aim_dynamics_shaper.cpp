#include "aim_dynamics_shaper.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AimDynamicsShaper::AimDynamicsShaper(AimDynamicsShaperConfig config)
    : config_(config) {}

pipeline_contract::Vec2f AimDynamicsShaper::shape(
    pipeline_contract::Vec2f requested_ai,
    const pipeline_contract::TargetPlan& plan,
    float dt_seconds) noexcept {
    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        plan.mode == pipeline_contract::ControlMode::Manual) {
        requested_ai = {};
    }
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting) {
        auto prevent_blind_rise = [](float requested, float current) {
            if (requested * current > 0.0f && std::fabs(requested) > std::fabs(current)) {
                return current;
            }
            return requested;
        };
        requested_ai.x = prevent_blind_rise(requested_ai.x, current_.x);
        requested_ai.y = prevent_blind_rise(requested_ai.y, current_.y);
    }
    auto slew = [&](float current, float target) {
        const bool decaying = std::fabs(target) < std::fabs(current);
        const float rate = decaying ? config_.decay_slew_per_second : config_.rise_slew_per_second;
        const float step = std::min(rate * dt, config_.max_step_per_tick);
        return current + std::clamp(target - current, -step, step);
    };
    current_.x = slew(current_.x, requested_ai.x);
    current_.y = slew(current_.y, requested_ai.y);
    return current_;
}

pipeline_contract::Vec2f AimDynamicsShaper::current() const noexcept {
    return current_;
}

void AimDynamicsShaper::adopt(pipeline_contract::Vec2f output) noexcept {
    current_ = output;
}

void AimDynamicsShaper::reset() noexcept {
    current_ = {};
}

}  // namespace controller_native
