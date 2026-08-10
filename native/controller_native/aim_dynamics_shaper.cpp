#include "aim_dynamics_shaper.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AimDynamicsShaper::AimDynamicsShaper(AimDynamicsShaperConfig config)
    : config_(config) {}

pipeline_contract::Vec2f AimDynamicsShaper::shape(
    pipeline_contract::Vec2f requested_ai,
    const pipeline_contract::IntentState& intent,
    const pipeline_contract::TargetPlan& plan,
    float dt_seconds,
    pipeline_contract::Vec2f confirmed_wrong_axis) noexcept {
    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    (void)intent;
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        plan.mode == pipeline_contract::ControlMode::Manual) {
        requested_ai = {};
    }
    (void)confirmed_wrong_axis;
    const std::uint64_t prior_target_id = previous_target_id_;
    const pipeline_contract::ControlMode prior_mode = previous_mode_;
    const bool target_changed = context_initialized_ &&
        prior_target_id != plan.target_id;
    const bool same_target_ads_to_bodylock = context_initialized_ &&
        prior_target_id != 0 && prior_target_id == plan.target_id &&
        prior_mode == pipeline_contract::ControlMode::AdsAcquire &&
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow;
    previous_target_id_ = plan.target_id;
    previous_mode_ = plan.mode;
    context_initialized_ = true;
    if (same_target_ads_to_bodylock) {
        // ADS and BodyLock requests have different meanings. A saturated ADS
        // value must not leak into the new mode, but an ordinary handoff still
        // needs to respect the lifecycle delta envelope. Clamp only the stale
        // high-force case into the new request envelope; otherwise discharge
        // toward BodyLock at a bounded step.
        const auto handoff_axis = [&](float current, float requested) {
            constexpr float kHandoffEnvelope = 0.08f;
            if (std::fabs(current) > std::fabs(requested) + kHandoffEnvelope) {
                return std::clamp(
                    current,
                    requested - kHandoffEnvelope,
                    requested + kHandoffEnvelope);
            }
            const bool reversing = current * requested < 0.0f;
            const float effective_target = reversing ? 0.0f : requested;
            const bool decaying = reversing ||
                std::fabs(effective_target) < std::fabs(current);
            const float rate = decaying
                ? config_.decay_slew_per_second
                : config_.rise_slew_per_second;
            const float step = std::min(rate * dt, 0.07f);
            return current + std::clamp(
                effective_target - current, -step, step);
        };
        current_.x = handoff_axis(current_.x, requested_ai.x);
        current_.y = handoff_axis(current_.y, requested_ai.y);
        return current_;
    }
    if (target_changed) {
        // A new target must not inherit the old target's direction. Start the
        // new request from a neutral AI baseline and let the ordinary slew
        // envelope acquire it on subsequent ticks.
        current_ = {};
    }
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::CueContinuation) {
        auto prevent_blind_rise = [](float requested, float current) {
            if (requested * current >= 0.0f &&
                std::fabs(requested) > std::fabs(current)) {
                return current;
            }
            return requested;
        };
        requested_ai.x = prevent_blind_rise(requested_ai.x, current_.x);
        requested_ai.y = prevent_blind_rise(requested_ai.y, current_.y);
    }
    auto slew = [&](float current, float target) {
        const bool reversing = current * target < 0.0f;
        const bool decaying = reversing ||
            std::fabs(target) < std::fabs(current);
        // A reversal is two phases: discharge old work, then rise in the new
        // direction. This preserves a short continuous decay without ever
        // carrying stale force across zero or amplifying it.
        const float effective_target = reversing ? 0.0f : target;
        const float rate = decaying
            ? config_.decay_slew_per_second
            : config_.rise_slew_per_second;
        const float step = std::min(rate * dt, config_.max_step_per_tick);
        return current + std::clamp(
            effective_target - current, -step, step);
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
    previous_target_id_ = 0;
    previous_mode_ = pipeline_contract::ControlMode::Manual;
    context_initialized_ = false;
}

}  // namespace controller_native
