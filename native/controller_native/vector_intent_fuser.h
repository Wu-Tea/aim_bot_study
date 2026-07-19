#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>

namespace controller_native {

enum class FusionCandidate : unsigned char {
    ExistingMix,
    ManualSupported,
    AiSupported,
    ManualOnly,
    AiOnly,
    ReducedMix,
};

struct FusionWeights {
    float manual = 1.0f;
    float ai = 0.0f;
};

FusionWeights candidate_weights(FusionCandidate candidate) noexcept;

struct VectorIntentFusionConfig {
    float manual_escape_threshold = 0.45f;
    float weight_transition_ms = 24.0f;
};

struct VectorIntentFusionInput {
    pipeline_contract::Vec2f manual_stick{};
    pipeline_contract::Vec2f shaped_ai_stick{};
    pipeline_contract::TargetPlan plan{};
    float manual_confidence = 1.0f;
};

struct VectorIntentFusionDecision {
    pipeline_contract::Vec2f fused_stick{};
    FusionCandidate candidate = FusionCandidate::ManualOnly;
    float target_manual_weight = 1.0f;
    float target_ai_weight = 0.0f;
    float applied_manual_weight = 1.0f;
    float applied_ai_weight = 0.0f;
    float winner_margin = 0.0f;
    std::array<float, 6> candidate_costs{};
    bool fallback = true;
    bool manual_escape = false;
};

class VectorIntentFuser {
public:
    explicit VectorIntentFuser(VectorIntentFusionConfig config = {});

    VectorIntentFusionDecision update(
        const VectorIntentFusionInput& input, float dt_seconds) noexcept;
    void reset() noexcept;

private:
    VectorIntentFusionConfig config_{};
    FusionCandidate previous_candidate_ = FusionCandidate::ManualOnly;
    pipeline_contract::Vec2f previous_output_{};
    bool initialized_ = false;
};

}  // namespace controller_native
