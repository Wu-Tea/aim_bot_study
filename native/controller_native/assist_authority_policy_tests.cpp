#include "assist_authority_policy.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

controller_native::AssistAuthorityPolicyInput base_input() {
    controller_native::AssistAuthorityPolicyInput input;
    input.selected.has_selection = true;
    input.selected.selected_observation_id = 501;
    input.selected.track_id = 42;
    input.selected.backing_frame_id = 7;
    input.has_estimate = true;
    input.estimate.track_id = 42;
    input.estimate.backing_observation_id = 501;
    input.estimate.backing_frame_id = 7;
    input.estimate.source = pipeline_contract::TrackEstimateSource::Observed;
    input.estimate.lifecycle = pipeline_contract::TrackLifecycle::Confirmed;
    input.estimate.confidence = 0.85f;
    input.estimate.position_sigma = 0.02f;
    input.estimate.ambiguity = 0.10f;
    input.estimate.aim_error_px = {80.0f, 0.0f};
    input.estimate.last_observed_at = {10.000};
    input.estimate.query_time = {10.010};
    input.estimate.observation_age_ms = 10.0;
    input.evidence_tier = "observed_strong";
    input.current_observed_aim_authority = true;
    input.current_observed_fire_authority = true;
    input.fire_requested = true;
    input.prior_observed_track_id = 42;
    input.prior_observed_at = {10.000};
    input.query_time = {10.010};
    input.max_continuity_age_ms = 80.0f;
    input.max_position_sigma = 0.10f;
    return input;
}

void require_no_authority(
    const pipeline_contract::AssistAuthorityDecision& decision,
    const std::string& message) {
    require(decision.assist_authority == common_native::AssistAuthority::None, message);
    require(decision.fire_authority == common_native::FireAuthority::None,
            message + " (fire)");
}

void test_current_strong_observation_grants_observed_authority_and_requested_fire() {
    const pipeline_contract::AssistAuthorityDecision decision =
        controller_native::decide_assist_authority(base_input());
    require(decision.state == pipeline_contract::AssistAuthorityState::ObservedStrong,
            "fresh strong evidence must grant observed authority");
    require(decision.assist_authority == common_native::AssistAuthority::AimObserved,
            "fresh strong evidence must grant observed aim");
    require(decision.fire_authority == common_native::FireAuthority::ObservedOnly,
            "fresh strong evidence may grant requested observed-only fire");
}

void test_short_same_track_gap_grants_continuity_without_fire() {
    auto input = base_input();
    input.estimate.source = pipeline_contract::TrackEstimateSource::Projected;
    input.estimate.lifecycle = pipeline_contract::TrackLifecycle::Coasting;
    input.estimate.backing_observation_id = 0;
    input.evidence_tier = "none";
    input.current_observed_aim_authority = false;
    input.current_observed_fire_authority = false;
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.state == pipeline_contract::AssistAuthorityState::Continuity,
            "bounded same-track gap must use explicit continuity state");
    require(decision.assist_authority == common_native::AssistAuthority::AimCoast,
            "bounded same-track gap may grant coast aim");
    require(decision.fire_authority == common_native::FireAuthority::None,
            "continuity must never grant fire");
}

void test_weak_or_cue_evidence_remains_track_only() {
    for (const char* tier : {"associated_weak", "cue_hold"}) {
        auto input = base_input();
        input.evidence_tier = tier;
        input.current_observed_aim_authority = true;
        const auto decision = controller_native::decide_assist_authority(input);
        require(decision.state == pipeline_contract::AssistAuthorityState::TrackOnly,
                std::string(tier) + " must remain estimator-only");
        require_no_authority(decision, std::string(tier) + " must not grant authority");
    }
}

void test_projected_track_without_prior_observed_grant_is_track_only() {
    auto input = base_input();
    input.estimate.source = pipeline_contract::TrackEstimateSource::Projected;
    input.estimate.lifecycle = pipeline_contract::TrackLifecycle::Coasting;
    input.evidence_tier = "projected";
    input.current_observed_aim_authority = false;
    input.prior_observed_track_id = 0;
    input.prior_observed_at = {};
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.state == pipeline_contract::AssistAuthorityState::TrackOnly,
            "projection cannot bootstrap its own authority");
    require_no_authority(decision, "projection without prior grant must not control output");
}

void test_stale_or_uncertain_estimate_is_rejected() {
    auto stale = base_input();
    stale.estimate.observation_age_ms = 81.0;
    require(controller_native::decide_assist_authority(stale).state ==
                pipeline_contract::AssistAuthorityState::Reject,
            "expired observation must be rejected");

    auto uncertain = base_input();
    uncertain.estimate.position_sigma = 0.11f;
    uncertain.estimate.source = pipeline_contract::TrackEstimateSource::Projected;
    uncertain.estimate.lifecycle = pipeline_contract::TrackLifecycle::Coasting;
    uncertain.estimate.backing_observation_id = 0;
    uncertain.evidence_tier = "none";
    uncertain.current_observed_aim_authority = false;
    require(controller_native::decide_assist_authority(uncertain).state ==
                pipeline_contract::AssistAuthorityState::Reject,
            "high-uncertainty projection must be rejected");
}

void test_current_strong_observation_aims_but_does_not_fire_with_high_sigma() {
    auto input = base_input();
    input.estimate.position_sigma = 0.11f;
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.state == pipeline_contract::AssistAuthorityState::ObservedStrong,
            "current selector-owned strong geometry must survive estimator sigma");
    require(decision.assist_authority == common_native::AssistAuthority::AimObserved,
            "current strong geometry must retain aim authority");
    require(decision.fire_authority == common_native::FireAuthority::None,
            "high sigma must still block fire authority");
}

void test_current_selector_observation_does_not_wait_for_tracker_confirmation() {
    auto input = base_input();
    input.estimate.lifecycle = pipeline_contract::TrackLifecycle::Tentative;
    input.prior_observed_track_id = 99;
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.state == pipeline_contract::AssistAuthorityState::ObservedStrong,
            "current selector observation must not be rejected as an old-track switch");
    require(decision.assist_authority == common_native::AssistAuthority::AimObserved,
            "tentative tracker lifecycle must not veto current observation aim");
    require(decision.fire_authority == common_native::FireAuthority::None,
            "tentative tracker lifecycle must not grant fire");
}

void test_selected_track_change_rejects_old_continuity() {
    auto input = base_input();
    input.selected.track_id = 43;
    input.estimate.track_id = 43;
    input.estimate.source = pipeline_contract::TrackEstimateSource::Projected;
    input.estimate.lifecycle = pipeline_contract::TrackLifecycle::Coasting;
    input.evidence_tier = "none";
    input.current_observed_aim_authority = false;
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.reason == pipeline_contract::AssistAuthorityReason::TargetSwitched,
            "changed track must report target-switch rejection");
    require_no_authority(decision, "old-track continuity must not transfer to a new track");
}

void test_strong_opposing_user_intent_yields_authority() {
    auto input = base_input();
    input.user_intent.valid = true;
    input.user_intent.aiming = true;
    input.user_intent.has_direction = true;
    input.user_intent.strength = 1.0f;
    input.user_intent.direction = {-1.0f, 0.0f};
    const auto decision = controller_native::decide_assist_authority(input);
    require(decision.reason == pipeline_contract::AssistAuthorityReason::UserYield,
            "strong opposing intent must yield with an explicit reason");
    require_no_authority(decision, "strong opposing intent must retain manual control");
}

}  // namespace

int main() {
    try {
        test_current_strong_observation_grants_observed_authority_and_requested_fire();
        test_short_same_track_gap_grants_continuity_without_fire();
        test_weak_or_cue_evidence_remains_track_only();
        test_projected_track_without_prior_observed_grant_is_track_only();
        test_stale_or_uncertain_estimate_is_rejected();
        test_current_strong_observation_aims_but_does_not_fire_with_high_sigma();
        test_current_selector_observation_does_not_wait_for_tracker_confirmation();
        test_selected_track_change_rejects_old_continuity();
        test_strong_opposing_user_intent_yields_authority();
        std::cout << "[AssistAuthorityPolicyTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AssistAuthorityPolicyTests][FAIL] " << error.what() << "\n";
        return 1;
    }
}
