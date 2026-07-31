#pragma once

#include "causal_mix_evaluator.h"
#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstddef>

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
    float weight_transition_ms = 24.0f;
    float fresh_vision_wrong_way_manual_floor = 0.35f;
    float fallback_response_px_per_stick_second = 500.0f;
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
    FusionCandidate previous_candidate_ = FusionCandidate::ManualOnly;
    pipeline_contract::Vec2f previous_output_{};
    float applied_manual_weight_ = 1.0f;
    float applied_ai_weight_ = 1.0f;
    float applied_tangential_manual_weight_ = 1.0f;
    std::uint64_t target_id_ = 0;
    float fresh_evidence_remaining_ms_ = 0.0f;
    std::uint64_t strong_approach_target_id_ = 0;
    pipeline_contract::Vec2f strong_approach_direction_{};
    pipeline_contract::Vec2f applied_control_radial_{};
    float obsolete_manual_window_ms_ = 0.0f;
    bool control_radial_initialized_ = false;
    bool initialized_ = false;
};

}  // namespace controller_native
