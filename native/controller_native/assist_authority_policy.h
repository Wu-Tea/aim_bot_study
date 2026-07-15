#pragma once

#include "../pipeline_contract/assist_authority.h"
#include "../pipeline_contract/target_snapshot.h"
#include "../pipeline_contract/track_memory.h"

#include <cstdint>
#include <string>

namespace controller_native {

struct AssistAuthorityPolicyInput {
    pipeline_contract::SelectedTrackRef selected;
    bool has_estimate = false;
    pipeline_contract::TrackEstimate estimate;
    std::string evidence_tier = "none";
    bool current_observed_aim_authority = false;
    bool current_observed_fire_authority = false;
    bool identity_hold_only = false;
    bool fire_requested = false;
    std::uint64_t prior_observed_track_id = 0;
    common_native::TimeSeconds prior_observed_at;
    pipeline_contract::UserAimIntent user_intent;
    common_native::TimeSeconds query_time;
    float max_continuity_age_ms = 80.0f;
    float max_position_sigma = 0.10f;
    float opposing_intent_alignment = -0.35f;
    float opposing_intent_min_strength = 0.55f;
};

[[nodiscard]] pipeline_contract::AssistAuthorityDecision decide_assist_authority(
    const AssistAuthorityPolicyInput& input);

}  // namespace controller_native
