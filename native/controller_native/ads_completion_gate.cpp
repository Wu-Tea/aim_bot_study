#include "ads_completion_gate.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AdsCompletionGate::AdsCompletionGate(float radius_px, int fresh_frames, float max_acquisition_ms)
    : radius_px_(std::max(1.0f, radius_px)),
      fresh_frames_(std::max(1, fresh_frames)),
      max_acquisition_seconds_(static_cast<double>(std::max(1.0f, max_acquisition_ms)) / 1000.0) {}

AdsCompletionGateState AdsCompletionGate::update(const AdsCompletionGateInput& input) {
    if (!input.aiming) {
        completed_for_ads_hold_ = false;
        reset(AdsCompletionReason::Released);
        return state_;
    }
    if (!input.has_target_authority) {
        completed_for_ads_hold_ = false;
        reset(AdsCompletionReason::TargetLost);
        return state_;
    }
    if (completed_for_ads_hold_) return state_;
    if (!state_.active) {
        if (!input.has_strong_target || !input.fresh_observation) return state_;
        state_.active = true;
        state_.centered_fresh_frames = 0;
        state_.reason = AdsCompletionReason::None;
        started_at_seconds_ = input.now_seconds;
        last_vision_sequence_ = 0;
    }
    if (input.now_seconds - started_at_seconds_ >= max_acquisition_seconds_) {
        state_.active = false;
        state_.reason = AdsCompletionReason::Timeout;
        completed_for_ads_hold_ = true;
        return state_;
    }
    const bool distinct_fresh = input.has_strong_target && input.fresh_observation && input.vision_sequence != 0 &&
        input.vision_sequence != last_vision_sequence_;
    if (!distinct_fresh) return state_;
    last_vision_sequence_ = input.vision_sequence;
    constexpr float kTerminalHorizonSeconds = 0.050f;
    constexpr float kOvershootBudgetPx = 2.0f;
    constexpr float kSettledPositionAssist = 0.08f;
    const float projected_closing_px =
        std::max(0.0f, input.closing_speed_px_per_sec) * kTerminalHorizonSeconds;
    const bool terminal_approach_safe =
        input.terminal_approach_valid &&
        std::isfinite(input.closing_speed_px_per_sec) &&
        std::isfinite(input.position_closing_assist) &&
        projected_closing_px <= kOvershootBudgetPx &&
        std::fabs(input.position_closing_assist) <= kSettledPositionAssist;
    const bool settled_input =
        !input.crossing_brake_active && terminal_approach_safe;
    if (std::hypot(input.dx, input.dy) <= radius_px_ && settled_input) {
        ++state_.centered_fresh_frames;
    } else {
        state_.centered_fresh_frames = 0;
    }
    if (state_.centered_fresh_frames >= fresh_frames_) {
        state_.active = false;
        state_.reason = AdsCompletionReason::Centered;
        completed_for_ads_hold_ = true;
    }
    return state_;
}

void AdsCompletionGate::reset(AdsCompletionReason reason) {
    state_.active = false;
    state_.centered_fresh_frames = 0;
    state_.reason = reason;
    started_at_seconds_ = 0.0;
    last_vision_sequence_ = 0;
}

const AdsCompletionGateState& AdsCompletionGate::state() const { return state_; }

const char* ads_completion_reason_name(AdsCompletionReason reason) {
    switch (reason) {
    case AdsCompletionReason::Centered: return "centered";
    case AdsCompletionReason::Timeout: return "timeout";
    case AdsCompletionReason::Released: return "released";
    case AdsCompletionReason::TargetLost: return "target_lost";
    default: return "none";
    }
}

}  // namespace controller_native
