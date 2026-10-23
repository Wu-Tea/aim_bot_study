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
    if (plan.observation_gap) {
        // Open-loop prediction is aim-only, not new evidence for more force.
        // It may keep or reduce already-supported work, never start an axis,
        // amplify it, or reverse it. The coordinator owns the capture deadline.
        // With identity alone, revoke AI immediately rather than slewing stale work.
        const float remaining = std::max(0.f, plan.observation_gap_remaining_ms);
        // Integrating the remaining-time ratio produces a linear discharge
        // toward the original capture deadline, independent of tick cadence.
        const float remaining_fraction = remaining / (remaining + dt * 1000.f);
        auto bounded_axis = [remaining_fraction](float requested, float previous) {
            if (!std::isfinite(requested) || requested * previous <= 0.f) return 0.f;
            return std::copysign(std::min(std::fabs(requested), std::fabs(previous) * remaining_fraction), previous);
        };
        current_ = !plan.gap_prediction_valid || plan.aim_authority <= 0 || target_changed
            ? pipeline_contract::Vec2f{}
            : pipeline_contract::Vec2f{bounded_axis(requested_ai.x, current_.x),
                                      bounded_axis(requested_ai.y, current_.y)};
        return current_;
    }
    const bool timed_bodylock = plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        (config_.bodylock_accel_ms > 0 || config_.bodylock_decel_ms > 0);
    const auto step_for = [&](bool decaying, float force, float legacy_limit) {
        const float duration = decaying ? config_.bodylock_decel_ms : config_.bodylock_accel_ms;
        if (timed_bodylock && duration > 0 && force > 0)
            return force * dt * 1000.0f / duration;
        return std::min((decaying ? config_.decay_slew_per_second : config_.rise_slew_per_second) * dt,
            legacy_limit);
    };
    const auto authorized = [&](pipeline_contract::Vec2f value) {
        if (!timed_bodylock) return value;
        // A slower ramp may never prolong revoked authority. Match the
        // solver's current force ellipse; shrinking evidence is immediate.
        const float budget = std::clamp(std::min(plan.aim_authority, plan.reliability),0.0f,1.0f) *
            config_.bodylock_authority_budget_scale;
        const float x = std::min(config_.bodylock_max_force.x,budget);
        const float y = std::min(config_.bodylock_max_force.y,budget);
        if(x<=0) value.x=0;
        if(y<=0) value.y=0;
        const float length=std::hypot(x>0 ? value.x/x:0, y>0 ? value.y/y:0);
        if(length>1) { value.x/=length;value.y/=length; }
        return value;
    };
    current_=authorized(current_);
    requested_ai=authorized(requested_ai);
    if (same_target_ads_to_bodylock) {
        // ADS and BodyLock requests have different meanings. A saturated ADS
        // value must not leak into the new mode, but an ordinary handoff still
        // needs to respect the lifecycle delta envelope. Clamp only the stale
        // high-force case into the new request envelope; otherwise discharge
        // toward BodyLock at a bounded step.
        const auto handoff_axis = [&](float current, float requested, float force) {
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
            const float step = step_for(decaying,force,0.07f);
            return current + std::clamp(
                effective_target - current, -step, step);
        };
        current_.x = handoff_axis(current_.x, requested_ai.x,config_.bodylock_max_force.x);
        current_.y = handoff_axis(current_.y, requested_ai.y,config_.bodylock_max_force.y);
        current_=authorized(current_);
        return current_;
    }
    if (target_changed) {
        // A new target must not inherit the old target's direction. Start the
        // new request from a neutral AI baseline and let the ordinary slew
        // envelope acquire it on subsequent ticks.
        current_ = {};
    }
    const bool measured_cue_motion = plan.bodylock_target_motion_valid &&
        plan.bodylock_target_motion_from_cue && !plan.observation_gap;
    if (plan.lifecycle == pipeline_contract::TargetLifecycle::CueContinuation &&
        !measured_cue_motion) {
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
    auto slew = [&](float current, float target, float force) {
        const bool reversing = current * target < 0.0f;
        const bool decaying = reversing ||
            std::fabs(target) < std::fabs(current);
        // A reversal is two phases: discharge old work, then rise in the new
        // direction. This preserves a short continuous decay without ever
        // carrying stale force across zero or amplifying it.
        const float effective_target = reversing ? 0.0f : target;
        const float step = step_for(decaying,force,config_.max_step_per_tick);
        return current + std::clamp(
            effective_target - current, -step, step);
    };
    current_.x = slew(current_.x, requested_ai.x,config_.bodylock_max_force.x);
    current_.y = slew(current_.y, requested_ai.y,config_.bodylock_max_force.y);
    current_=authorized(current_);
    return current_;
}

pipeline_contract::Vec2f AimDynamicsShaper::current() const noexcept {
    return current_;
}

pipeline_contract::Vec2f AimDynamicsShaper::shape_ownership(
    pipeline_contract::Vec2f authorized_output,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f shaped_ai,
    float dt_seconds,
    bool active,
    bool target_changed) noexcept {
    if (!active) {
        ownership_initialized_ = false;
        ai_share_ = {};
        return authorized_output;
    }
    const pipeline_contract::Vec2f span{shaped_ai.x-manual.x, shaped_ai.y-manual.y};
    const auto share = [](float output, float native, float distance, float previous) {
        return std::fabs(distance) > 1.e-6f
            ? std::clamp((output-native)/distance, 0.f, 1.f) : previous;
    };
    // Equal commands carry no ownership evidence. Preserve the share through
    // a zero crossing instead of inventing a manual takeover and re-ramping
    // ordinary diagonal tracking on the next tick.
    const pipeline_contract::Vec2f desired{
        share(authorized_output.x,manual.x,span.x,ownership_initialized_ && !target_changed ? ai_share_.x : 1.f),
        share(authorized_output.y,manual.y,span.y,ownership_initialized_ && !target_changed ? ai_share_.y : 1.f)};
    if (!ownership_initialized_ || target_changed) {
        // Acquisition already has its ordinary AI ramp. Do not add a second
        // acquisition delay or inherit the previous target's authority share.
        ai_share_ = desired;
        ownership_initialized_ = true;
        return authorized_output;
    }
    // Manual reclaim / revoked AI authority is immediate on each axis.
    ai_share_.x = std::min(ai_share_.x,desired.x);
    ai_share_.y = std::min(ai_share_.y,desired.y);
    if (ai_share_.x == desired.x && ai_share_.y == desired.y)
        return authorized_output;
    const pipeline_contract::Vec2f work{
        (desired.x-ai_share_.x)*span.x,
        (desired.y-ai_share_.y)*span.y};
    const float budget = std::min(config_.max_step_per_tick,
        config_.rise_slew_per_second*std::clamp(dt_seconds,.0001f,.05f));
    const float length = std::hypot(work.x,work.y);
    const float fraction = length > budget ? budget/length : 1.f;
    ai_share_.x += (desired.x-ai_share_.x)*fraction;
    ai_share_.y += (desired.y-ai_share_.y)*fraction;
    // Return exact arbitration endpoints; interpolation never grants work
    // beyond the current authorized segment or filters physical passthrough.
    return {
        ai_share_.x >= desired.x ? authorized_output.x : manual.x+span.x*ai_share_.x,
        ai_share_.y >= desired.y ? authorized_output.y : manual.y+span.y*ai_share_.y};
}

pipeline_contract::Vec2f AimDynamicsShaper::stopping_work(
    pipeline_contract::Vec2f sustaining,const AimResponseCurveConfig& curve) const noexcept {
    // Integrate the existing decay envelope in camera-response coordinates.
    // Prediction only: no altered slew, wait or queued output.
    const auto follow=inverse_aim_response_curve(sustaining,curve);
    const float rate=std::max(.001f,config_.decay_slew_per_second);
    const float horizon=std::max(std::fabs(current_.x-follow.x),std::fabs(current_.y-follow.y))/rate;
    pipeline_contract::Vec2f integral{};
    constexpr int steps=16;
    for(int i=0;i<steps;++i) {
        const float t=horizon*(i+.5f)/steps;
        const auto at=[&](float u,float v) {return u+std::clamp(v-u,-rate*t,rate*t);};
        const auto response=forward_aim_response_curve({at(current_.x,follow.x),at(current_.y,follow.y)},curve);
        integral.x+=(response.x-sustaining.x)*horizon/steps;
        integral.y+=(response.y-sustaining.y)*horizon/steps;
    }
    return integral;
}

void AimDynamicsShaper::adopt(pipeline_contract::Vec2f output) noexcept {
    current_ = output;
}

void AimDynamicsShaper::reset() noexcept {
    current_ = {};
    previous_target_id_ = 0;
    previous_mode_ = pipeline_contract::ControlMode::Manual;
    context_initialized_ = false;
    ownership_initialized_ = false;
    ai_share_ = {};
}

}  // namespace controller_native
