#pragma once

#include "aim_response_curve_plugin.h"
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
    NoAuthority,
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
    // Preserves manual authority against opposing AI. Its complement also
    // bounds dangerous close-target same-direction proposal overlap.
    float manual_preservation_floor = 0.55f;
    // Fresh, reliable positional evidence may reduce only the manual radial
    // component that points away from the current target error.  The value is
    // the retained fraction of that wrong-way radial manual proposal.
    float fresh_vision_wrong_way_manual_floor = 0.35f;
    // While ADS owns acquisition, or a near BodyLock target has strong
    // authority, manual and AI are alternative absolute stick proposals.
    // This fraction of the normalized cooperative manual proposal may add
    // parallel headroom. Both proposals therefore enter arbitration even when
    // AI is larger, without recreating raw manual + AI force addition.
    float ai_priority_parallel_headroom = 0.20f;
    float ai_priority_opposing_manual_retention = 0.50f;
    // The game camera stays calibrated for sensitivity 2.4.  During assisted
    // proposal ownership, normalize only the cooperative manual component to
    // the operator's measured effective sensitivities. Raw physical input
    // still owns admission, counter-steer, tangent intent, and escape.
    float ads_same_direction_manual_scale = 1.90f / 2.40f;
    float near_bodylock_same_direction_manual_scale = 2.00f / 2.40f;
    // Keep assisted proposal ownership independent from the older takeover
    // threshold.  The latter is deliberately low for escape detection, while
    // proposal replacement is reserved for a genuinely strong stick push.
    float ai_priority_manual_start = 0.45f;
    float ai_priority_manual_full = 0.70f;
    bool assisted_ai_priority_enabled = true;
    bool contextual_manual_normalization_enabled = true;
};

struct VectorIntentFusionInput {
    pipeline_contract::Vec2f manual_stick{};
    pipeline_contract::Vec2f shaped_ai_stick{};
    pipeline_contract::TargetPlan plan{};
    float manual_confidence = 1.0f;
    bool fresh_single_target_observation = false;
    // Filled by the active ADS/BodyLock controller. These are the same
    // anisotropic response envelope used before fusion, not a global gain.
    float response_horizon_seconds = 0.0f;
    float response_horizon_y_seconds = 0.0f;
    pipeline_contract::Vec2f response_max_force{};
    bool response_envelope_valid = false;
    const char* response_envelope_source = "unavailable";
    AimResponseCurveConfig response_curve{};
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
    bool fresh_vision_wrong_way_policy_applied = false;
    float fresh_vision_manual_radial_scale = 1.0f;
    // Validated proposal only; this is not a contribution allocated from
    // the final actuator output.
    pipeline_contract::Vec2f fresh_vision_validated_manual_proposal{};
    bool fresh_vision_ai_radial_bound_applied = false;
    float fresh_vision_ai_radial_scale = 1.0f;
    bool fresh_vision_predictive_envelope_applied = false;
    bool manual_escape_latched = false;
    float fresh_vision_manual_radial = 0.0f;
    // Radial demand from the strongest validated proposal, not an allocation
    // of manual and AI contributions into the final output.
    float fresh_vision_proposed_radial = 0.0f;
    float fresh_vision_raw_ai_radial = 0.0f;
    float fresh_vision_strongest_valid_radial = 0.0f;
    float fresh_vision_strongest_valid_demand = 0.0f;
    // Validated proposal only; this is not a final-output contribution.
    pipeline_contract::Vec2f fresh_vision_validated_ai_proposal{};
    float fresh_vision_permitted_radial = 0.0f;
    float fresh_vision_stopping_radial = 0.0f;
    float fresh_vision_pre_slew_radial = 0.0f;
    float fresh_vision_final_radial = 0.0f;
    float fresh_vision_envelope_horizon_seconds = 0.0f;
    float fresh_vision_envelope_horizon_y_seconds = 0.0f;
    pipeline_contract::Vec2f fresh_vision_envelope_max_force{};
    pipeline_contract::Vec2f fresh_vision_envelope_target_stick{};
    pipeline_contract::Vec2f fresh_vision_authoritative_error_px{};
    pipeline_contract::Vec2f fresh_vision_predicted_error_px{};
    const char* fresh_vision_envelope_reason = "none";
    const char* fresh_vision_envelope_source = "unavailable";
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
    bool manual_escape_pending_ = false;
    std::uint64_t manual_escape_pending_target_id_ = 0;
    bool manual_escape_latched_ = false;
    std::uint64_t manual_escape_latched_target_id_ = 0;
};

}  // namespace controller_native
