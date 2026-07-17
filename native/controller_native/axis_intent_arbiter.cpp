#include "axis_intent_arbiter.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

constexpr float kInnovationLimitPx = 64.0f;
constexpr float kGeometryChangeThreshold = 0.01f;
constexpr float kMinimumReliability = 0.65f;
constexpr float kMinimumErrorGrowthPx = 1.0f;
// The recorded P25 wrong-axis input is 0.28 before the observed +/-0.0118
// drift. Keep deliberate sub-0.25 predictive corrections fully user-owned.
constexpr float kMinimumWrongManualMagnitude = 0.25f;
constexpr float kGeometryCooldownSeconds = 0.040f;
constexpr float kInterframeHoldSeconds = 0.012f;
constexpr float kInitialWrongWayRetention = 0.85f;
constexpr float kRetentionAttackSeconds = 0.025f;
constexpr float kRetentionReleaseSeconds = 0.050f;

}  // namespace

AxisDecision AxisIntentArbiter::update(
    Axis axis,
    const AxisIntentInput& input,
    float dt_seconds) noexcept {
    AxisState& state = axes_[static_cast<std::size_t>(axis)];
    AxisDecision decision;
    decision.manual_yield_confidence =
        std::clamp(input.manual_confidence, 0.0f, 1.0f);

    if (input.mode == pipeline_contract::ControlMode::Manual || input.target_id == 0) {
        state = {};
        return decision;
    }
    const bool target_changed = state.initialized && state.target_id != input.target_id;
    if (target_changed) {
        state = {};
        state.previous_error = input.error;
        state.target_id = input.target_id;
        state.initialized = true;
        decision.reason = AxisDecisionReason::EvidenceAmbiguous;
        return decision;
    }
    if (!state.initialized) {
        state.previous_error = input.error;
        state.target_id = input.target_id;
        state.initialized = true;
    }

    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    state.geometry_cooldown_seconds = std::max(
        0.0f, state.geometry_cooldown_seconds - dt);
    state.intervention_hold_seconds = std::max(
        0.0f, state.intervention_hold_seconds - dt);
    const bool geometry_changed =
        input.normalized_size_change > kGeometryChangeThreshold;
    const bool hard_reset = geometry_changed ||
        input.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring ||
        input.lifecycle == pipeline_contract::TargetLifecycle::None ||
        input.reliability < kMinimumReliability;
    if (hard_reset) {
        state.geometry_cooldown_seconds = kGeometryCooldownSeconds;
        state.intervention_hold_seconds = 0.0f;
        state.manual_retention = 1.0f;
    }

    const bool manual_active = decision.manual_yield_confidence > 0.0f &&
        std::fabs(input.manual) > 0.0f;
    const bool wrong_way = manual_active && input.manual * input.error < 0.0f;
    const bool attenuation_eligible =
        std::fabs(input.manual) >= kMinimumWrongManualMagnitude;
    const bool observed_stable =
        input.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        input.reliability >= kMinimumReliability &&
        input.target_innovation_px <= kInnovationLimitPx &&
        input.normalized_size_change <= kGeometryChangeThreshold &&
        state.geometry_cooldown_seconds <= 0.0f;
    const bool escape = manual_active &&
        std::fabs(input.manual) >= std::max(0.0f, input.manual_escape_threshold);
    const bool worsening_by_rate = input.error * input.error_rate > 0.0f;
    const bool worsening_by_history =
        std::fabs(input.error) >= std::fabs(state.previous_error) + kMinimumErrorGrowthPx;
    decision.wrong_way = wrong_way;
    decision.evidence_stable = observed_stable;
    decision.error_worsening = worsening_by_rate || worsening_by_history;

    if (escape) {
        state.intervention_hold_seconds = 0.0f;
        state.manual_retention = 1.0f;
        decision.reason = AxisDecisionReason::ManualEscape;
    } else if (observed_stable && wrong_way && attenuation_eligible &&
               (worsening_by_rate || worsening_by_history)) {
        decision.manual_yield_confidence = 0.0f;
        decision.intervention = true;
        state.intervention_hold_seconds = kInterframeHoldSeconds;
        decision.reason = AxisDecisionReason::ConfirmedWrongWay;
    } else if (observed_stable && state.intervention_hold_seconds > 0.0f &&
               wrong_way && (worsening_by_rate || worsening_by_history)) {
        decision.manual_yield_confidence = 0.0f;
        decision.intervention = true;
        state.intervention_hold_seconds = kInterframeHoldSeconds;
        decision.reason = AxisDecisionReason::ConfirmedWrongWay;
    } else if (input.lifecycle == pipeline_contract::TargetLifecycle::Coasting &&
               state.intervention_hold_seconds > 0.0f && wrong_way) {
        decision.manual_yield_confidence = 0.0f;
        decision.intervention = true;
        decision.reason = AxisDecisionReason::ConfirmedWrongWay;
    } else if (!observed_stable && manual_active) {
        decision.reason = AxisDecisionReason::EvidenceAmbiguous;
    } else if (input.lifecycle == pipeline_contract::TargetLifecycle::Observed) {
        state.intervention_hold_seconds = 0.0f;
    }

    if (decision.intervention) {
        const float floor = std::clamp(
            input.manual_preservation_floor, 0.50f, 1.0f);
        if (state.manual_retention >= 0.999f) {
            state.manual_retention = std::max(floor, kInitialWrongWayRetention);
        } else {
            const float alpha = std::clamp(dt / kRetentionAttackSeconds, 0.0f, 1.0f);
            state.manual_retention += alpha * (floor - state.manual_retention);
            state.manual_retention = std::max(floor, state.manual_retention);
        }
    } else if (!escape && !hard_reset) {
        const float alpha = std::clamp(dt / kRetentionReleaseSeconds, 0.0f, 1.0f);
        state.manual_retention += alpha * (1.0f - state.manual_retention);
    }
    decision.manual_retention = state.manual_retention;

    state.previous_error = input.error;
    state.target_id = input.target_id;
    return decision;
}

void AxisIntentArbiter::reset() noexcept {
    axes_ = {};
}

const char* to_string(AxisDecisionReason reason) noexcept {
    switch (reason) {
        case AxisDecisionReason::Neutral: return "neutral";
        case AxisDecisionReason::ConfirmedWrongWay: return "confirmed_wrong_way";
        case AxisDecisionReason::EvidenceAmbiguous: return "evidence_ambiguous";
        case AxisDecisionReason::ManualEscape: return "manual_escape";
    }
    return "unknown";
}

}  // namespace controller_native
