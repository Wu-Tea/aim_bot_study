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

struct AssistAuthorityDecision {
    std::uint64_t selected_track_id = 0;
    AssistAuthorityState state = AssistAuthorityState::Reject;
    AssistAuthorityReason reason = AssistAuthorityReason::None;
    common_native::AssistAuthority assist_authority = common_native::AssistAuthority::None;
    common_native::FireAuthority fire_authority = common_native::FireAuthority::None;
    float assist_scale = 0.0f;
};

}  // namespace pipeline_contract
