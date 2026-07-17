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
    } else if (requested * manual > 0.0f) {
        const float confidence = std::clamp(manual_confidence, 0.0f, 1.0f);
        requested *= 1.0f - (1.0f - config_.cooperative_manual_scale) * confidence;
    }
    return requested;
}

pipeline_contract::Vec2f AimDynamicsShaper::shape(
    pipeline_contract::Vec2f requested_ai,
    const pipeline_contract::IntentState& intent,
    const pipeline_contract::TargetPlan& plan,
    float dt_seconds,
    pipeline_contract::Vec2f confirmed_wrong_axis) noexcept {
    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        plan.mode == pipeline_contract::ControlMode::Manual) {
        requested_ai = {};
    }
    bool trusted_coast_rise_x = false;
    bool trusted_coast_rise_y = false;
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting) {
        auto prevent_blind_rise = [](float requested, float current) {
            if (requested * current >= 0.0f &&
                std::fabs(requested) > std::fabs(current)) {
                return current;
            }
            return requested;
        };
        trusted_coast_rise_x =
            confirmed_wrong_axis.x > 0.5f &&
            std::fabs(requested_ai.x) > std::fabs(intent.filtered_right.x);
        trusted_coast_rise_y =
            confirmed_wrong_axis.y > 0.5f &&
            std::fabs(requested_ai.y) > std::fabs(intent.filtered_right.y);
        if (!trusted_coast_rise_x) {
            requested_ai.x = prevent_blind_rise(requested_ai.x, current_.x);
        }
        if (!trusted_coast_rise_y) {
            requested_ai.y = prevent_blind_rise(requested_ai.y, current_.y);
        }
    }
    requested_ai.x = shape_axis(
        requested_ai.x, intent.filtered_right.x, intent.right_x.confidence, dt);
    requested_ai.y = shape_axis(
        requested_ai.y, intent.filtered_right.y, intent.right_y.confidence, dt);

    auto slew = [&](float current, float target, bool trusted_coast_rise) {
        const bool decaying = std::fabs(target) < std::fabs(current);
        const float rise_rate = trusted_coast_rise
            ? std::min(config_.rise_slew_per_second, config_.decay_slew_per_second)
            : config_.rise_slew_per_second;
        const float rate = decaying ? config_.decay_slew_per_second : rise_rate;
        const float step = std::min(rate * dt, config_.max_step_per_tick);
        return current + std::clamp(target - current, -step, step);
    };
    current_.x = slew(current_.x, requested_ai.x, trusted_coast_rise_x);
    current_.y = slew(current_.y, requested_ai.y, trusted_coast_rise_y);
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
