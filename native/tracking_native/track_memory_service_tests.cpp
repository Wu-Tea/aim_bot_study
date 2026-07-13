#include "../pipeline_contract/assist_authority.h"
#include "../pipeline_contract/track_memory.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void test_estimate_keeps_identity_geometry_and_real_observation_time() {
    pipeline_contract::TrackEstimate estimate;
    estimate.track_id = 42;
    estimate.backing_observation_id = 9001;
    estimate.backing_frame_id = 77;
    estimate.source = pipeline_contract::TrackEstimateSource::Observed;
    estimate.lifecycle = pipeline_contract::TrackLifecycle::Confirmed;
    estimate.aim_error_px = {16.0f, -8.0f};
    estimate.last_observed_at = {12.0};
    estimate.query_time = {12.010};
    estimate.observation_age_ms = 10.0;

    require(estimate.track_id == 42, "track identity must survive transport");
    require(estimate.backing_observation_id == 9001,
            "backing observation identity must survive transport");
    require(estimate.source == pipeline_contract::TrackEstimateSource::Observed,
            "estimate source must remain explicit");
    require_near(estimate.last_observed_at.value, 12.0, 1e-9,
                 "query must not rewrite real observation time");
    require_near(estimate.observation_age_ms, 10.0, 1e-9,
                 "observation age must be measured from capture time");
}

void test_selection_and_authority_are_separate_value_types() {
    static_assert(!std::is_same_v<
                  pipeline_contract::SelectedTrackRef,
                  pipeline_contract::AssistAuthorityDecision>);
    static_assert(!std::is_constructible_v<
                  pipeline_contract::AssistAuthorityDecision,
                  pipeline_contract::SelectedTrackRef>);

    pipeline_contract::SelectedTrackRef selected;
    selected.has_selection = true;
    selected.selected_observation_id = 9001;
    selected.track_id = 42;
    selected.backing_frame_id = 77;
    selected.confidence = 0.8f;
    selected.reason = pipeline_contract::SelectedTrackReason::VisionSelector;

    pipeline_contract::AssistAuthorityDecision authority;
    authority.selected_track_id = selected.track_id;
    authority.state = pipeline_contract::AssistAuthorityState::TrackOnly;
    authority.reason = pipeline_contract::AssistAuthorityReason::WeakEvidence;

    require(selected.reason == pipeline_contract::SelectedTrackReason::VisionSelector,
            "selector reason must stay with selection ownership");
    require(authority.state == pipeline_contract::AssistAuthorityState::TrackOnly,
            "authority must be decided independently from selection");
    require(authority.assist_authority == common_native::AssistAuthority::None,
            "track-only state must not implicitly grant assist");
    require(authority.fire_authority == common_native::FireAuthority::None,
            "track-only state must not implicitly grant fire");
}

}  // namespace

int main() {
    try {
        test_estimate_keeps_identity_geometry_and_real_observation_time();
        test_selection_and_authority_are_separate_value_types();
        std::cout << "[TrackMemoryServiceTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TrackMemoryServiceTests][FAIL] " << error.what() << "\n";
        return 1;
    }
}
