#include "vector_intent_fuser.h"

#include <algorithm>
#include <cmath>

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
    : config_(config) {}

VectorIntentFusionDecision VectorIntentFuser::update(
    const VectorIntentFusionInput& input, float) noexcept {
    VectorIntentFusionDecision decision;
    decision.fallback = false;
    decision.candidate = FusionCandidate::ExistingMix;

    const float manual_progress = dot(input.manual_stick, input.plan.error_px);
    const float ai_progress = dot(input.shaped_ai_stick, input.plan.error_px);
    const bool opposing = dot(input.manual_stick, input.shaped_ai_stick) < 0.0f;
    if (opposing && manual_progress < 0.0f && ai_progress > 0.0f &&
        length(input.manual_stick) < config_.manual_escape_threshold) {
        decision.candidate = FusionCandidate::AiSupported;
    }

    const FusionWeights weights = candidate_weights(decision.candidate);
    decision.target_manual_weight = weights.manual;
    decision.target_ai_weight = weights.ai;
    decision.applied_manual_weight = weights.manual;
    decision.applied_ai_weight = weights.ai;
    decision.fused_stick = mix(
        input.manual_stick, input.shaped_ai_stick, weights);
    return decision;
}

void VectorIntentFuser::reset() noexcept {}

}  // namespace controller_native
