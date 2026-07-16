#include "axis_intent_arbiter.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

constexpr float kCrossDeadzonePx = 0.5f;
constexpr float kInnovationLimitPx = 32.0f;
constexpr float kSizeChangeLimit = 0.18f;
constexpr float kMinimumReliability = 0.65f;
constexpr float kRiskAttackSeconds = 0.004f;
constexpr float kRiskReleaseSeconds = 0.055f;
constexpr float kMinimumWrongWayBudget = 0.03f;

bool sign_crossed(float previous, float current) noexcept {
    return std::fabs(previous) > kCrossDeadzonePx &&
        std::fabs(current) > kCrossDeadzonePx &&
        previous * current < 0.0f;
}

}  // namespace

AxisDecision AxisIntentArbiter::update(
    Axis axis,
    const AxisIntentInput& input,
    float dt_seconds) noexcept {
    AxisState& state = axes_[static_cast<std::size_t>(axis)];
    AxisDecision decision;
    decision.assist_output = input.requested_assist;

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

    const float confidence = std::clamp(input.manual_confidence, 0.0f, 1.0f);
    const bool manual_active = confidence > 0.0f && std::fabs(input.manual) > 0.0f;
    const bool helpful = manual_active && input.manual * input.error > 0.0f;
    const bool wrong_way = manual_active && input.manual * input.error < 0.0f;
    if (helpful && input.requested_assist * input.manual > 0.0f) {
        const float request_magnitude = std::fabs(input.requested_assist);
        const float floor_scale = input.mode == pipeline_contract::ControlMode::AdsAcquire &&
            std::fabs(input.error) >= 48.0f ? 0.50f : 0.15f;
        const float residual_magnitude = std::max(
            request_magnitude * floor_scale,
            request_magnitude - std::fabs(input.manual));
        decision.assist_output = std::copysign(residual_magnitude, input.requested_assist);
        decision.assist_scale = request_magnitude > 0.0001f
            ? residual_magnitude / request_magnitude
            : 1.0f;
        if (input.error * input.error_rate < 0.0f &&
            std::fabs(input.error_rate) > 1.0f) {
            const float time_to_cross = std::fabs(input.error / input.error_rate);
            const float horizon = input.mode == pipeline_contract::ControlMode::AdsAcquire
                ? 0.075f
                : 0.055f;
            const float stopping_scale = std::clamp(time_to_cross / horizon, 0.15f, 1.0f);
            decision.assist_output *= stopping_scale;
            decision.assist_scale *= stopping_scale;
        }
        decision.reason = AxisDecisionReason::HelpfulResidual;
    } else if (wrong_way && input.requested_assist * input.manual < 0.0f) {
        decision.assist_scale = 1.0f - 0.35f * confidence;
        decision.assist_output *= decision.assist_scale;
        decision.reason = AxisDecisionReason::ProbableWrongWay;
    }

    const bool observed_stable =
        input.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        input.reliability >= kMinimumReliability &&
        input.target_innovation_px <= kInnovationLimitPx &&
        input.normalized_size_change <= kSizeChangeLimit;
    const bool escape = manual_active &&
        std::fabs(input.manual) >= std::max(0.0f, input.manual_escape_threshold);
    if (observed_stable && helpful && !escape &&
        input.error * input.error_rate < 0.0f &&
        std::fabs(input.error_rate) > 1.0f) {
        const float time_to_cross = std::fabs(input.error / input.error_rate);
        const float horizon = input.mode == pipeline_contract::ControlMode::AdsAcquire
            ? 0.075f
            : 0.055f;
        if (time_to_cross < horizon) {
            const float stopping_scale = std::clamp(time_to_cross / horizon, 0.15f, 1.0f);
            decision.stopping_output_budget = std::max(
                kMinimumWrongWayBudget,
                std::fabs(input.manual + decision.assist_output) * stopping_scale);
        }
    }
    const bool crossed = sign_crossed(state.previous_error, input.error);
    const bool worsening = input.error * input.error_rate > 0.0f;
    float instantaneous_risk = 0.0f;
    if (observed_stable && wrong_way && !escape) {
        if (crossed) instantaneous_risk = 1.0f;
        else if (worsening) {
            instantaneous_risk = std::fabs(input.error) <= 8.0f ? 1.0f : 0.80f;
        }
    }

    const float dt = std::clamp(dt_seconds, 0.0001f, 0.05f);
    const float time_constant = instantaneous_risk > state.risk
        ? kRiskAttackSeconds
        : kRiskReleaseSeconds;
    const float alpha = std::clamp(dt / time_constant, 0.0f, 1.0f);
    state.risk += alpha * (instantaneous_risk - state.risk);
    decision.divergence_risk = state.risk;

    if (escape) {
        if (input.requested_assist * input.manual < 0.0f) {
            decision.assist_output = 0.0f;
            decision.assist_scale = 0.0f;
        }
        decision.reason = AxisDecisionReason::ManualEscape;
    } else if (!observed_stable && manual_active) {
        decision.reason = AxisDecisionReason::EvidenceAmbiguous;
    } else if (state.risk > 0.0f) {
        decision.wrong_way_budget = std::max(
            kMinimumWrongWayBudget,
            1.0f - state.risk);
        decision.reason = AxisDecisionReason::CrossingLimit;
    }

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
        case AxisDecisionReason::HelpfulResidual: return "helpful_residual";
        case AxisDecisionReason::ProbableWrongWay: return "probable_wrong_way";
        case AxisDecisionReason::CrossingLimit: return "crossing_limit";
        case AxisDecisionReason::EvidenceAmbiguous: return "evidence_ambiguous";
        case AxisDecisionReason::ManualEscape: return "manual_escape";
    }
    return "unknown";
}

}  // namespace controller_native
