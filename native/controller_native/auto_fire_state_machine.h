#pragma once

namespace controller_native {
enum class FirePulsePhase : unsigned char { Idle, Pressed, Gap };
struct FirePulseResult { bool pressed = false; bool started = false; };

class FirePulseStateMachine {
public:
    void reset() noexcept { phase_ = FirePulsePhase::Idle; started_at_ = next_at_ = -1.0; }
    FirePulseResult update(bool authorized, double now, double width, double period) noexcept {
        if (!authorized) { reset(); return {}; }
        if (phase_ != FirePulsePhase::Idle && now < started_at_) reset();
        if (phase_ == FirePulsePhase::Idle || now + 1e-9 >= next_at_) {
            phase_ = FirePulsePhase::Pressed;
            started_at_ = now; next_at_ = now + period;
            return {true, true};
        }
        phase_ = now - started_at_ < width ? FirePulsePhase::Pressed : FirePulsePhase::Gap;
        return {phase_ == FirePulsePhase::Pressed, false};
    }
    FirePulsePhase phase() const noexcept { return phase_; }
private:
    FirePulsePhase phase_ = FirePulsePhase::Idle;
    double started_at_ = -1.0, next_at_ = -1.0;
};

enum class ManualFirePhase : unsigned char { Free, Held, ResumeGuard };
class ManualFireStateMachine {
public:
    void reset() noexcept { phase_ = ManualFirePhase::Free; guard_started_ = -1.0; }
    ManualFirePhase update(bool pressed, bool takeover_eligible, double now, double guard_seconds) noexcept {
        if (pressed && phase_ != ManualFirePhase::Held && takeover_eligible) guard_started_ = now;
        const double elapsed = guard_started_ < 0 ? -1.0 : (now < guard_started_ ? 0.0 : now - guard_started_);
        const bool guarded = elapsed >= 0 && elapsed < guard_seconds;
        if (elapsed >= guard_seconds) guard_started_ = -1.0;
        phase_ = pressed ? ManualFirePhase::Held : guarded ? ManualFirePhase::ResumeGuard : ManualFirePhase::Free;
        return phase_;
    }
    ManualFirePhase phase() const noexcept { return phase_; }
private:
    ManualFirePhase phase_ = ManualFirePhase::Free;
    double guard_started_ = -1.0;
};
}  // namespace controller_native
