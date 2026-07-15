#include "assist_authority_policy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace controller_native {

namespace {

std::string normalized(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool is_strong_observed_tier(std::string_view tier) {
    const std::string value = normalized(tier);
    return value == "observed_strong" || value == "strong" || value == "observed";
}

bool is_weak_or_cue_tier(std::string_view tier) {
    const std::string value = normalized(tier);
    return value == "associated_weak" || value == "weak" ||
        value == "weak_association" || value == "weak_observed" ||
        value == "cue_hold" || value == "cue_only";
}

bool is_projected_tier(std::string_view tier) {
    const std::string value = normalized(tier);
    return value == "projected" || value == "predicted";
}

bool has_no_current_evidence(std::string_view tier) {
    const std::string value = normalized(tier);
    return value.empty() || value == "none";
}

bool strongly_opposes_selected_target(const AssistAuthorityPolicyInput& input) {
    const pipeline_contract::UserAimIntent& intent = input.user_intent;
    if (!intent.valid || !intent.aiming || !intent.has_direction ||
        intent.strength < input.opposing_intent_min_strength) {
        return false;
    }
    const float target_length = std::hypot(
        input.estimate.aim_error_px.x,
        input.estimate.aim_error_px.y);
    const float intent_length = std::hypot(intent.direction.x, intent.direction.y);
    if (target_length <= 1.0f || intent_length <= 0.001f) {
        return false;
    }
    const float alignment =
        ((input.estimate.aim_error_px.x / target_length) *
         (intent.direction.x / intent_length)) +
        ((input.estimate.aim_error_px.y / target_length) *
         (intent.direction.y / intent_length));
    return alignment <= input.opposing_intent_alignment;
}

pipeline_contract::AssistAuthorityDecision decision_for(
    const AssistAuthorityPolicyInput& input,
    pipeline_contract::AssistAuthorityState state,
    pipeline_contract::AssistAuthorityReason reason) {
    pipeline_contract::AssistAuthorityDecision decision;
    decision.selected_track_id = input.selected.track_id;
    decision.state = state;
    decision.reason = reason;
    return decision;
}

}  // namespace

pipeline_contract::AssistAuthorityDecision decide_assist_authority(
    const AssistAuthorityPolicyInput& input) {
    if (!input.selected.has_selection || !input.has_estimate ||
        input.selected.track_id == 0 || input.estimate.track_id != input.selected.track_id) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Reject,
            pipeline_contract::AssistAuthorityReason::InvalidTarget);
    }

    const bool stale = input.max_continuity_age_ms > 0.0f &&
        input.estimate.observation_age_ms > input.max_continuity_age_ms;
    if (stale) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Reject,
            pipeline_contract::AssistAuthorityReason::Stale);
    }
    const bool directly_observed =
        input.estimate.source == pipeline_contract::TrackEstimateSource::Observed &&
        input.estimate.backing_observation_id == input.selected.selected_observation_id;
    const bool current_strong_observation =
        directly_observed && is_strong_observed_tier(input.evidence_tier) &&
        input.current_observed_aim_authority;

    if (input.prior_observed_track_id != 0 &&
        input.selected.track_id != input.prior_observed_track_id &&
        !current_strong_observation) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Reject,
            pipeline_contract::AssistAuthorityReason::TargetSwitched);
    }

    if (strongly_opposes_selected_target(input)) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Reject,
            pipeline_contract::AssistAuthorityReason::UserYield);
    }

    if (input.identity_hold_only) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::TrackOnly,
            pipeline_contract::AssistAuthorityReason::ShortEvidenceGap);
    }

    if (is_weak_or_cue_tier(input.evidence_tier)) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::TrackOnly,
            is_projected_tier(input.evidence_tier)
                ? pipeline_contract::AssistAuthorityReason::ProjectedOnly
                : pipeline_contract::AssistAuthorityReason::WeakEvidence);
    }

    if (current_strong_observation) {
        pipeline_contract::AssistAuthorityDecision decision = decision_for(
            input,
            pipeline_contract::AssistAuthorityState::ObservedStrong,
            pipeline_contract::AssistAuthorityReason::StrongObserved);
        decision.assist_authority = common_native::AssistAuthority::AimObserved;
        decision.assist_scale = 1.0f;
        const bool uncertainty_bounded_for_fire =
            input.max_position_sigma <= 0.0f ||
            input.estimate.position_sigma <= input.max_position_sigma;
        const bool confirmed_for_fire =
            input.estimate.lifecycle == pipeline_contract::TrackLifecycle::Confirmed;
        if (confirmed_for_fire && uncertainty_bounded_for_fire && input.fire_requested &&
            input.current_observed_fire_authority) {
            decision.fire_authority = common_native::FireAuthority::ObservedOnly;
        }
        return decision;
    }

    // Position sigma describes estimator uncertainty. It can veto projected
    // continuity, but it must not erase a current selector-owned strong
    // observation whose aim geometry comes from the present detection.
    if (input.max_position_sigma > 0.0f &&
        input.estimate.position_sigma > input.max_position_sigma) {
        return decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Reject,
            pipeline_contract::AssistAuthorityReason::HighUncertainty);
    }

    const bool same_prior_observed_track =
        input.prior_observed_track_id != 0 &&
        input.prior_observed_track_id == input.selected.track_id &&
        input.prior_observed_at.value > 0.0;
    const bool estimate_is_continuity =
        input.estimate.source == pipeline_contract::TrackEstimateSource::Projected ||
        input.estimate.lifecycle == pipeline_contract::TrackLifecycle::Coasting ||
        has_no_current_evidence(input.evidence_tier);
    if (same_prior_observed_track && estimate_is_continuity &&
        !is_weak_or_cue_tier(input.evidence_tier)) {
        pipeline_contract::AssistAuthorityDecision decision = decision_for(
            input,
            pipeline_contract::AssistAuthorityState::Continuity,
            pipeline_contract::AssistAuthorityReason::ShortEvidenceGap);
        decision.assist_authority = common_native::AssistAuthority::AimCoast;
        decision.assist_scale = 1.0f;
        return decision;
    }

    return decision_for(
        input,
        pipeline_contract::AssistAuthorityState::TrackOnly,
        estimate_is_continuity || is_projected_tier(input.evidence_tier)
            ? pipeline_contract::AssistAuthorityReason::ProjectedOnly
            : pipeline_contract::AssistAuthorityReason::WeakEvidence);
}

}  // namespace controller_native
