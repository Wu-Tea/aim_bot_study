#include "auto_fire_gate.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

bool has_manual_right_stick_input(float manual_right_x, float manual_right_y) {
    constexpr float kManualFireReadyStickThreshold = 0.08f;
    return std::hypot(manual_right_x, manual_right_y) >= kManualFireReadyStickThreshold;
}

}  // namespace

AutoFireGate::AutoFireGate(
    GamepadAutoFireConfig auto_fire_config,
    GamepadAiAimConfig ai_config)
    : auto_fire_config_(std::move(auto_fire_config)),
      ai_config_(std::move(ai_config)) {}

void AutoFireGate::reset() {
    counters_ = NativeAutoFireCounters{};
    manual_fire_was_pressed_ = false;
    auto_fire_was_active_ = false;
    manual_takeover_started_at_seconds_ = -1.0;
    reset_pulse_schedule();
    reset_readiness();
}

void AutoFireGate::reset_readiness() {
    ready_frames_ = 0;
    has_ready_vision_sequence_ = false;
    ready_vision_sequence_ = 0;
}

AutoFireGateDecision AutoFireGate::evaluate(const AutoFireGateInput& input) {
    AutoFireGateDecision decision;
    decision.before_auto_fire_active = auto_fire_was_active_;
    decision.aim_ready = aim_ready_for_input(input);
    decision.block_reason = block_reason_for_input(input, decision.aim_ready);
    bool should_fire = decision.block_reason == AutoFireBlockReason::None;
    decision.pre_takeover_should_fire = should_fire;

    const bool manual_fire_started =
        input.manual_fire_pressed && !manual_fire_was_pressed_;
    if (manual_fire_started && (should_fire || auto_fire_was_active_)) {
        manual_takeover_started_at_seconds_ = input.now_seconds;
    }

    double takeover_elapsed = manual_takeover_elapsed(input.now_seconds);
    bool in_takeover_guard = takeover_elapsed >= 0.0 &&
        takeover_elapsed < manual_takeover_total_seconds();
    if (takeover_elapsed >= manual_takeover_total_seconds()) {
        manual_takeover_started_at_seconds_ = -1.0;
        in_takeover_guard = false;
    }

    manual_fire_was_pressed_ = input.manual_fire_pressed;
    if (input.manual_fire_pressed) {
        should_fire = false;
        decision.block_reason = AutoFireBlockReason::ManualFire;
    } else if (in_takeover_guard) {
        should_fire = false;
        decision.block_reason = AutoFireBlockReason::ManualTakeoverGuard;
    }

    const bool authorized = should_fire;
    if (!authorized) {
        reset_pulse_schedule();
        should_fire = false;
    } else {
        constexpr double kClockEpsilonSeconds = 1e-9;
        const double width_seconds =
            static_cast<double>(auto_fire_config_.pulse_width_ms) / 1000.0;
        const double period_seconds =
            static_cast<double>(auto_fire_config_.pulse_period_ms) / 1000.0;
        if (pulse_cycle_active_ && input.now_seconds < pulse_started_at_seconds_) {
            reset_pulse_schedule();
        }
        if (!pulse_cycle_active_ ||
            input.now_seconds + kClockEpsilonSeconds >= next_pulse_at_seconds_) {
            pulse_cycle_active_ = true;
            pulse_started_at_seconds_ = input.now_seconds;
            next_pulse_at_seconds_ = input.now_seconds + period_seconds;
            ++counters_.pulse_starts;
            should_fire = true;
        } else {
            should_fire =
                input.now_seconds - pulse_started_at_seconds_ < width_seconds;
        }
    }
    decision.pulse_waiting = authorized && !should_fire;

    if (input.vision_state.auto_fire_requested) {
        ++counters_.requested;
        if (should_fire) {
            ++counters_.allowed;
        } else {
            ++counters_.blocked;
        }
    }

    decision.should_fire = should_fire;
    decision.after_auto_fire_active = should_fire;
    decision.counters = counters_;
    auto_fire_was_active_ = should_fire;
    return decision;
}

AutoFireReduction AutoFireGate::reduce(
    const AutoFireGateInput& input,
    pipeline_contract::ControllerTickId controller_tick,
    pipeline_contract::EventSequence command_sequence,
    pipeline_contract::EventSequence cause_event) {
    AutoFireReduction result{};
    result.decision = evaluate(input);
    if (!result.decision.should_fire) return result;
    result.command.header.controller_tick = controller_tick;
    result.command.header.sequence = command_sequence;
    result.command.header.cause_event = cause_event;
    result.command.synthetic_active = true;
    result.command.synthetic_rb = auto_fire_config_.fire_output != "RT";
    result.command.synthetic_right_trigger =
        result.command.synthetic_rb ? 0.0f : 1.0f;
    return result;
}

NativeAutoFireCounters AutoFireGate::counters() const {
    return counters_;
}

bool AutoFireGate::active() const {
    return auto_fire_was_active_;
}

bool AutoFireGate::aim_ready_for_input(const AutoFireGateInput& input) {
    if (!auto_fire_config_.require_aim_ready) {
        return true;
    }
    if (!input.vision_state.auto_fire_requested) {
        reset_readiness();
        return true;
    }
    if (input.vision_state.has_target &&
        !input.vision_state.current_observed_target_present) {
        return false;
    }
    if (!input.aiming || !has_fresh_aim_target(input.vision_state, input.now_seconds)) {
        reset_readiness();
        return false;
    }
    if (!is_strong_fire_target(input.vision_state)) {
        reset_readiness();
        return false;
    }
    if (!input.ads_min_elapsed) {
        reset_readiness();
        return false;
    }

    const float error_px = static_cast<float>(std::hypot(
        input.settle_dx * ai_config_.ai_delta_gain,
        input.settle_dy * ai_config_.ai_delta_gain));
    if (error_px > std::max(0.0f, ai_config_.auto_fire_ready_error_px)) {
        reset_readiness();
        return false;
    }

    const float ai_stick_mag =
        std::max(
            std::fabs(input.output_right_x - input.manual_right_x),
            std::fabs(input.output_right_y - input.manual_right_y)) *
        32767.0f;
    const float max_ai_stick = std::max(0.0f, ai_config_.auto_fire_ready_max_ai_stick);
    const bool manual_tracking =
        has_manual_right_stick_input(input.manual_right_x, input.manual_right_y) &&
        input.vision_state.auto_fire_requested;
    if (max_ai_stick > 0.0f && ai_stick_mag > max_ai_stick && !manual_tracking) {
        reset_readiness();
        return false;
    }

    const std::uint64_t vision_sequence = input.vision_state.vision_sequence;
    if (vision_sequence != 0 && !has_ready_vision_sequence_) {
        has_ready_vision_sequence_ = true;
        ready_vision_sequence_ = vision_sequence;
        ready_frames_ = 1;
        return ready_frames_ >= std::max(1, ai_config_.auto_fire_ready_frames);
    }
    if (vision_sequence == 0 || vision_sequence != ready_vision_sequence_) {
        ++ready_frames_;
        has_ready_vision_sequence_ = vision_sequence != 0;
        ready_vision_sequence_ = vision_sequence;
    }
    return ready_frames_ >= std::max(1, ai_config_.auto_fire_ready_frames);
}

AutoFireBlockReason AutoFireGate::block_reason_for_input(
    const AutoFireGateInput& input,
    bool aim_ready) const {
    if (!auto_fire_config_.enabled) return AutoFireBlockReason::Disabled;
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            input.vision_state.has_target,
            input.vision_state.aim_authority,
            input.vision_state.fire_authority,
            input.vision_state.target_tier);
    if (auto_fire_config_.aim_only && !input.aiming) {
        return AutoFireBlockReason::NotAiming;
    }
    if (auto_fire_config_.require_aim_ready && !aim_ready) {
        return AutoFireBlockReason::AimNotReady;
    }
    if (!input.vision_state.auto_fire_requested) {
        return AutoFireBlockReason::NotRequested;
    }
    if (!input.vision_state.has_target) {
        return AutoFireBlockReason::NoTarget;
    }
    if (authority.fire_authority != common_native::FireAuthority::ObservedOnly) {
        return AutoFireBlockReason::NoFireAuthority;
    }
    if (!has_fresh_auto_fire_source(input.vision_state, input.now_seconds)) {
        return AutoFireBlockReason::StaleSource;
    }
    return AutoFireBlockReason::None;
}

bool AutoFireGate::has_fresh_auto_fire_source(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = auto_fire_config_.max_source_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return true;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

bool AutoFireGate::has_fresh_aim_target(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = ai_config_.target_max_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return vision_state.has_target && vision_state.aim_authority;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return vision_state.has_target &&
        vision_state.aim_authority &&
        age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

bool AutoFireGate::is_strong_fire_target(
    const NativeControllerVisionState& vision_state) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    return authority.fire_authority == common_native::FireAuthority::ObservedOnly;
}

double AutoFireGate::manual_takeover_elapsed(double now_seconds) const {
    if (manual_takeover_started_at_seconds_ < 0.0) {
        return -1.0;
    }
    return std::max(0.0, now_seconds - manual_takeover_started_at_seconds_);
}

double AutoFireGate::manual_takeover_total_seconds() const {
    return std::max(0.0f, auto_fire_config_.manual_takeover_release_seconds) +
        std::max(0.0f, auto_fire_config_.manual_takeover_resume_delay_seconds);
}

void AutoFireGate::reset_pulse_schedule() {
    pulse_cycle_active_ = false;
    pulse_started_at_seconds_ = -1.0;
    next_pulse_at_seconds_ = -1.0;
}

const char* auto_fire_block_reason_name(AutoFireBlockReason reason) {
    switch (reason) {
    case AutoFireBlockReason::Disabled: return "disabled";
    case AutoFireBlockReason::NotAiming: return "not_aiming";
    case AutoFireBlockReason::AimNotReady: return "aim_not_ready";
    case AutoFireBlockReason::NotRequested: return "not_requested";
    case AutoFireBlockReason::NoTarget: return "no_target";
    case AutoFireBlockReason::NoFireAuthority: return "no_fire_authority";
    case AutoFireBlockReason::StaleSource: return "stale_source";
    case AutoFireBlockReason::ManualFire: return "manual_fire";
    case AutoFireBlockReason::ManualTakeoverGuard: return "manual_takeover_guard";
    default: return "none";
    }
}

}  // namespace controller_native
