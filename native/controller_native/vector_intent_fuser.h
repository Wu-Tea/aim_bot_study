#pragma once

#include "causal_mix_evaluator.h"
#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace controller_native {

enum class FusionCandidate : unsigned char {
    ExistingMix,
    ManualSupported,
    AiSupported,
    ManualOnly,
    AiOnly,
    ReducedMix,
    RadialCorrected,
    RadialReplaced,
    TangentialCorrected,
    TangentialReplaced,
    FreshVisionCounterCorrected,
};

inline constexpr std::size_t kFusionCandidateCount =
    static_cast<std::size_t>(FusionCandidate::FreshVisionCounterCorrected) + 1;

enum class FusionFallbackReason : unsigned char {
    None,
    NoTarget,
    TargetChanged,
    Reacquiring,
    LowReliability,
    LowResponseConfidence,
    NonFinite,
    ManualEscape,
};

struct FusionWeights {
    // `manual` is the component parallel to current target error.  The
    // tangential component is separate so a wrong closing input can be
    // corrected without swallowing useful target-follow input.
    float manual = 1.0f;
    float ai = 0.0f;
    float tangential_manual = 1.0f;
};

FusionWeights candidate_weights(FusionCandidate candidate) noexcept;

struct VectorIntentFusionConfig {
    float manual_escape_threshold = 0.45f;
    float manual_preservation_floor = 0.55f;
};

struct VectorIntentFusionInput {
    pipeline_contract::Vec2f manual_stick{};
    pipeline_contract::Vec2f shaped_ai_stick{};
    pipeline_contract::TargetPlan plan{};
    float manual_confidence = 1.0f;
    bool fresh_single_target_observation = false;
    std::array<pipeline_contract::Vec2f, kCausalMixHorizonCount>
        pending_camera_px{};
    bool pending_camera_valid = false;
    // Experimental controller-rate arbitration stays benchmark-only until its
    // moving-player guardrails pass. Production callers leave this disabled.
    bool causal_mix_enabled = false;
};

struct VectorIntentFusionDecision {
    pipeline_contract::Vec2f fused_stick{};
    FusionCandidate candidate = FusionCandidate::ManualOnly;
    float target_manual_weight = 1.0f;
    float target_ai_weight = 0.0f;
    float target_tangential_manual_weight = 1.0f;
    float applied_manual_weight = 1.0f;
    float applied_ai_weight = 0.0f;
    float applied_tangential_manual_weight = 1.0f;
    float winner_margin = 0.0f;
    std::array<float, kFusionCandidateCount> candidate_costs{};
    FusionFallbackReason reason = FusionFallbackReason::None;
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
    std::uint64_t target_id_ = 0;
    pipeline_contract::Vec2f previous_output_{};
    bool output_initialized_ = false;
    bool reentry_pending_ = false;
};

}  // namespace controller_native
