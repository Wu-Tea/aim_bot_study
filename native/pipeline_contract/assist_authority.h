#pragma once

#include "../common_native/authority_types.h"

#include <cstdint>

namespace pipeline_contract {

enum class AssistAuthorityState : std::uint8_t {
    Reject = 0,
    TrackOnly = 1,
    Continuity = 2,
    ObservedStrong = 3,
};

enum class AssistAuthorityReason : std::uint8_t {
    None = 0,
    StrongObserved = 1,
    ShortEvidenceGap = 2,
    WeakEvidence = 3,
    CueOnly = 4,
    ProjectedOnly = 5,
    Stale = 6,
    HighUncertainty = 7,
    TargetSwitched = 8,
    UserYield = 9,
    InvalidTarget = 10,
};

enum class BodylockLifecycleState : std::uint8_t {
    Inactive = 0,
    Warm = 1,
    Tracking = 2,
    Coast = 3,
    Yield = 4,
};

[[nodiscard]] inline const char* assist_authority_state_name(
    AssistAuthorityState state) noexcept {
    switch (state) {
    case AssistAuthorityState::Reject: return "reject";
    case AssistAuthorityState::TrackOnly: return "track_only";
    case AssistAuthorityState::Continuity: return "continuity";
    case AssistAuthorityState::ObservedStrong: return "observed_strong";
    }
    return "unknown";
}

[[nodiscard]] inline const char* assist_authority_reason_name(
    AssistAuthorityReason reason) noexcept {
    switch (reason) {
    case AssistAuthorityReason::None: return "none";
    case AssistAuthorityReason::StrongObserved: return "strong_observed";
    case AssistAuthorityReason::ShortEvidenceGap: return "short_evidence_gap";
    case AssistAuthorityReason::WeakEvidence: return "weak_evidence";
    case AssistAuthorityReason::CueOnly: return "cue_only";
    case AssistAuthorityReason::ProjectedOnly: return "projected_only";
    case AssistAuthorityReason::Stale: return "stale";
    case AssistAuthorityReason::HighUncertainty: return "high_uncertainty";
    case AssistAuthorityReason::TargetSwitched: return "target_switched";
    case AssistAuthorityReason::UserYield: return "user_yield";
    case AssistAuthorityReason::InvalidTarget: return "invalid_target";
    }
    return "unknown";
}

[[nodiscard]] inline const char* bodylock_lifecycle_state_name(
    BodylockLifecycleState state) noexcept {
    switch (state) {
    case BodylockLifecycleState::Inactive: return "inactive";
    case BodylockLifecycleState::Warm: return "warm";
    case BodylockLifecycleState::Tracking: return "tracking";
    case BodylockLifecycleState::Coast: return "coast";
    case BodylockLifecycleState::Yield: return "yield";
    }
    return "unknown";
}

struct AssistAuthorityDecision {
    std::uint64_t selected_track_id = 0;
    AssistAuthorityState state = AssistAuthorityState::Reject;
    AssistAuthorityReason reason = AssistAuthorityReason::None;
    common_native::AssistAuthority assist_authority = common_native::AssistAuthority::None;
    common_native::FireAuthority fire_authority = common_native::FireAuthority::None;
    float assist_scale = 0.0f;
};

}  // namespace pipeline_contract
