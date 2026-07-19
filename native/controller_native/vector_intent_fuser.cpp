#include "vector_intent_fuser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace controller_native {
namespace {

float dot(pipeline_contract::Vec2f left,
          pipeline_contract::Vec2f right) noexcept {
    return left.x * right.x + left.y * right.y;
}

float length(pipeline_contract::Vec2f value) noexcept {
    return std::hypot(value.x, value.y);
}

pipeline_contract::Vec2f mix(
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai,
    FusionWeights weights) noexcept;

bool finite_vec(pipeline_contract::Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float approach(float current, float target, float max_delta) noexcept {
    if (current < target) return std::min(target, current + max_delta);
    return std::max(target, current - max_delta);
}

pipeline_contract::Vec2f subtract(
    pipeline_contract::Vec2f left,
    pipeline_contract::Vec2f right) noexcept {
    return {left.x - right.x, left.y - right.y};
}

pipeline_contract::Vec2f control_error(
    pipeline_contract::Vec2f screen_error) noexcept {
    return {screen_error.x, -screen_error.y};
}

pipeline_contract::Vec2f base_error_at(
    const pipeline_contract::TargetPlan& plan, float time_seconds) noexcept {
    pipeline_contract::Vec2f previous = plan.error_px;
    float previous_time = 0.0f;
    const std::size_t count = std::min<std::size_t>(
        plan.horizon_count, plan.horizon.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& sample = plan.horizon[index];
        if (sample.time_seconds <= previous_time) continue;
        if (sample.time_seconds >= time_seconds) {
            const float span = sample.time_seconds - previous_time;
            const float alpha = std::clamp(
                (time_seconds - previous_time) / span, 0.0f, 1.0f);
            return {
                previous.x + (sample.error_px.x - previous.x) * alpha,
                previous.y + (sample.error_px.y - previous.y) * alpha,
            };
        }
        previous = sample.error_px;
        previous_time = sample.time_seconds;
    }
    const float remaining = std::max(0.0f, time_seconds - previous_time);
    return {
        previous.x + plan.error_rate_px_per_sec.x * remaining,
        previous.y + plan.error_rate_px_per_sec.y * remaining,
    };
}

struct CandidateScore {
    FusionCandidate candidate = FusionCandidate::ManualOnly;
    FusionWeights weights{};
    pipeline_contract::Vec2f output{};
    float cost = std::numeric_limits<float>::infinity();
};

CandidateScore score_candidate(
    FusionCandidate candidate,
    const VectorIntentFusionInput& input,
    pipeline_contract::Vec2f previous_output) noexcept {
    constexpr std::array<float, 3> kHorizons{0.040f, 0.080f, 0.160f};
    constexpr std::array<float, 3> kHorizonWeights{0.25f, 0.35f, 0.40f};
    constexpr float kTerminalWeight = 0.50f;
    constexpr float kCrossingWeight = 1.50f;
    constexpr float kOwnershipResponseSeconds = 0.18f;
    constexpr float kOutputChangeWeight = 0.75f;

    CandidateScore result;
    result.candidate = candidate;
    result.weights = candidate_weights(candidate);
    result.output = mix(
        input.manual_stick, input.shaped_ai_stick, result.weights);
    const auto output_delta = subtract(result.output, previous_output);
    const float response = std::max(1.0f, input.plan.response_scale) *
        std::clamp(input.plan.response_confidence, 0.0f, 1.0f);
    const auto initial = control_error(input.plan.error_px);
    const float initial_length = std::max(0.001f, length(initial));
    const pipeline_contract::Vec2f radial{
        initial.x / initial_length, initial.y / initial_length};
    float terminal_error = 0.0f;
    float cost = 0.0f;
    for (std::size_t index = 0; index < kHorizons.size(); ++index) {
        const float horizon = kHorizons[index];
        const auto base = control_error(base_error_at(input.plan, horizon));
        const pipeline_contract::Vec2f camera{
            output_delta.x * response * horizon,
            output_delta.y * response * horizon};
        const auto predicted = subtract(base, camera);
        const float error = length(predicted);
        cost += error * kHorizonWeights[index];
        terminal_error = error;
        const float radial_error = dot(predicted, radial);
        const float radial_push = dot(result.output, radial);
        if (radial_error < 0.0f && radial_push > 0.0f) {
            cost += -radial_error * kCrossingWeight;
        }
    }
    cost += terminal_error * kTerminalWeight;
    const float manual_loss = std::clamp(input.manual_confidence, 0.0f, 1.0f) *
        length(input.manual_stick) * (1.0f - result.weights.manual);
    cost += manual_loss * response * kOwnershipResponseSeconds;
    cost += length(subtract(result.output, previous_output)) *
        response * 0.001f * kOutputChangeWeight;
    result.cost = cost;
    return result;
}

pipeline_contract::Vec2f mix(
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai,
    FusionWeights weights) noexcept {
    return {
        std::clamp(manual.x * weights.manual + ai.x * weights.ai, -1.0f, 1.0f),
        std::clamp(manual.y * weights.manual + ai.y * weights.ai, -1.0f, 1.0f),
    };
}

}  // namespace

FusionWeights candidate_weights(FusionCandidate candidate) noexcept {
    switch (candidate) {
        case FusionCandidate::ExistingMix: return {1.0f, 1.0f};
        case FusionCandidate::ManualSupported: return {1.0f, 0.5f};
        case FusionCandidate::AiSupported: return {0.5f, 1.0f};
        case FusionCandidate::ManualOnly: return {1.0f, 0.0f};
        case FusionCandidate::AiOnly: return {0.0f, 1.0f};
        case FusionCandidate::ReducedMix: return {0.5f, 0.5f};
    }
    return {1.0f, 0.0f};
}

VectorIntentFuser::VectorIntentFuser(VectorIntentFusionConfig config)
    : config_(config) {
    config_.manual_escape_threshold = std::clamp(
        config_.manual_escape_threshold, 0.0f, 1.0f);
    if (!std::isfinite(config_.weight_transition_ms) ||
        config_.weight_transition_ms <= 0.0f) {
        config_.weight_transition_ms = 24.0f;
    }
}

VectorIntentFusionDecision VectorIntentFuser::update(
    const VectorIntentFusionInput& input, float dt_seconds) noexcept {
    VectorIntentFusionDecision decision;
    auto exact_manual = [&](FusionFallbackReason reason) {
        decision.fallback = true;
        decision.reason = reason;
        decision.candidate = FusionCandidate::ManualOnly;
        decision.target_manual_weight = 1.0f;
        decision.target_ai_weight = 0.0f;
        decision.applied_manual_weight = 1.0f;
        decision.applied_ai_weight = 0.0f;
        decision.fused_stick = input.manual_stick;
        applied_manual_weight_ = 1.0f;
        applied_ai_weight_ = 0.0f;
        previous_candidate_ = FusionCandidate::ManualOnly;
        previous_output_ = input.manual_stick;
        initialized_ = false;
        return decision;
    };
    const bool input_finite = finite_vec(input.manual_stick) &&
        finite_vec(input.shaped_ai_stick) &&
        std::isfinite(input.manual_confidence) &&
        std::isfinite(dt_seconds) && pipeline_contract::valid(input.plan);
    if (!input_finite) {
        return exact_manual(FusionFallbackReason::NonFinite);
    }
    const float manual_magnitude = length(input.manual_stick);
    if (manual_magnitude >= config_.manual_escape_threshold &&
        config_.manual_escape_threshold > 0.0f) {
        decision = exact_manual(FusionFallbackReason::ManualEscape);
        decision.fallback = false;
        decision.manual_escape = true;
        target_id_ = input.plan.target_id;
        return decision;
    }
    const bool no_target = input.plan.target_id == 0 ||
        input.plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        input.plan.mode == pipeline_contract::ControlMode::Manual;
    if (no_target) {
        target_id_ = 0;
        return exact_manual(FusionFallbackReason::NoTarget);
    }

    FusionFallbackReason fallback_reason = FusionFallbackReason::None;
    if (target_id_ != 0 && target_id_ != input.plan.target_id) {
        fallback_reason = FusionFallbackReason::TargetChanged;
    } else if (input.plan.lifecycle ==
               pipeline_contract::TargetLifecycle::Reacquiring) {
        fallback_reason = FusionFallbackReason::Reacquiring;
    } else if (input.plan.reliability < 0.65f) {
        fallback_reason = FusionFallbackReason::LowReliability;
    } else if (input.plan.response_confidence < 0.35f) {
        fallback_reason = FusionFallbackReason::LowResponseConfidence;
    }
    target_id_ = input.plan.target_id;

    const float dt_ms = std::clamp(dt_seconds, 0.0f, 0.05f) * 1000.0f;
    const float max_weight_delta = std::clamp(
        dt_ms / config_.weight_transition_ms, 0.0f, 1.0f);
    auto apply_weights = [&](FusionWeights target, FusionFallbackReason reason) {
        decision.reason = reason;
        decision.fallback = reason != FusionFallbackReason::None;
        decision.target_manual_weight = target.manual;
        decision.target_ai_weight = target.ai;
        applied_manual_weight_ = approach(
            applied_manual_weight_, target.manual, max_weight_delta);
        applied_ai_weight_ = approach(
            applied_ai_weight_, target.ai, max_weight_delta);
        decision.applied_manual_weight = applied_manual_weight_;
        decision.applied_ai_weight = applied_ai_weight_;
        decision.fused_stick = mix(
            input.manual_stick, input.shaped_ai_stick,
            {applied_manual_weight_, applied_ai_weight_});
        previous_output_ = decision.fused_stick;
        return decision;
    };
    if (fallback_reason != FusionFallbackReason::None) {
        decision.candidate = FusionCandidate::ExistingMix;
        previous_candidate_ = FusionCandidate::ExistingMix;
        initialized_ = false;
        return apply_weights({1.0f, 1.0f}, fallback_reason);
    }

    if (!initialized_) {
        applied_manual_weight_ = 1.0f;
        applied_ai_weight_ = 1.0f;
    }

    if (manual_magnitude <= 0.02f || input.manual_confidence <= 0.0f) {
        decision.candidate = FusionCandidate::ExistingMix;
        previous_candidate_ = FusionCandidate::ExistingMix;
        initialized_ = true;
        return apply_weights(
            candidate_weights(FusionCandidate::ExistingMix),
            FusionFallbackReason::None);
    }
    if (dot(input.manual_stick, input.shaped_ai_stick) > 0.0f) {
        decision.candidate = FusionCandidate::ExistingMix;
        previous_candidate_ = FusionCandidate::ExistingMix;
        initialized_ = true;
        return apply_weights(
            candidate_weights(FusionCandidate::ExistingMix),
            FusionFallbackReason::None);
    }

    decision.fallback = false;
    constexpr std::array<FusionCandidate, 6> kCandidates{
        FusionCandidate::ExistingMix,
        FusionCandidate::ManualSupported,
        FusionCandidate::AiSupported,
        FusionCandidate::ManualOnly,
        FusionCandidate::AiOnly,
        FusionCandidate::ReducedMix,
    };
    constexpr float kSelectionMargin = 2.0f;

    std::array<CandidateScore, kCandidates.size()> scores{};
    std::size_t best_index = 0;
    for (std::size_t index = 0; index < kCandidates.size(); ++index) {
        scores[index] = score_candidate(
            kCandidates[index], input, previous_output_);
        if (kCandidates[index] == FusionCandidate::AiOnly &&
            input.manual_confidence >= 0.35f) {
            scores[index].cost = std::numeric_limits<float>::infinity();
        }
        decision.candidate_costs[index] = scores[index].cost;
        if (scores[index].cost < scores[best_index].cost) best_index = index;
    }
    const float best_cost = scores[best_index].cost;
    if (initialized_) {
        const auto previous = std::find_if(
            scores.begin(), scores.end(), [&](const CandidateScore& score) {
                return score.candidate == previous_candidate_;
            });
        if (previous != scores.end() &&
            previous->cost <= best_cost + kSelectionMargin) {
            best_index = static_cast<std::size_t>(previous - scores.begin());
        }
    } else {
        const bool opposing = dot(
            input.manual_stick, input.shaped_ai_stick) < 0.0f;
        const bool manual_wrong_way = dot(
            input.manual_stick, control_error(input.plan.error_px)) < 0.0f;
        for (std::size_t index = 0; index < scores.size(); ++index) {
            const bool eligible_tie = !opposing || !manual_wrong_way ||
                scores[index].weights.manual < 1.0f;
            if (eligible_tie &&
                scores[index].cost <= best_cost + kSelectionMargin &&
                scores[index].weights.manual > scores[best_index].weights.manual) {
                best_index = index;
            }
        }
    }
    const CandidateScore& selected = scores[best_index];
    float second_cost = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (index != best_index) second_cost = std::min(second_cost, scores[index].cost);
    }
    decision.candidate = selected.candidate;
    decision.winner_margin = std::max(0.0f, second_cost - selected.cost);
    const FusionWeights weights = selected.weights;
    decision.target_manual_weight = weights.manual;
    decision.target_ai_weight = weights.ai;
    previous_candidate_ = decision.candidate;
    initialized_ = true;
    return apply_weights(weights, FusionFallbackReason::None);
}

void VectorIntentFuser::reset() noexcept {
    previous_candidate_ = FusionCandidate::ManualOnly;
    previous_output_ = {};
    applied_manual_weight_ = 1.0f;
    applied_ai_weight_ = 1.0f;
    target_id_ = 0;
    initialized_ = false;
}

}  // namespace controller_native
