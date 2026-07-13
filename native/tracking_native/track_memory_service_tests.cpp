#include "../pipeline_contract/assist_authority.h"
#include "../pipeline_contract/track_memory.h"
#include "track_memory_service.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

pipeline_contract::TrackObservationDetection make_detection(
    std::uint64_t observation_id,
    float x,
    float confidence = 0.90f) {
    pipeline_contract::TrackObservationDetection detection;
    detection.observation_id = observation_id;
    detection.body_box_px = {x, 160.0f, 60.0f, 140.0f};
    detection.aim_point_px = {x + 30.0f, 216.0f};
    detection.has_aim_point = true;
    detection.confidence = confidence;
    detection.class_id = 0;
    detection.evidence_tier = "observed_strong";
    return detection;
}

pipeline_contract::TrackObservationBatch make_batch(
    std::uint64_t frame_id,
    double captured_at,
    std::vector<pipeline_contract::TrackObservationDetection> detections) {
    pipeline_contract::TrackObservationBatch batch;
    batch.frame_id = frame_id;
    batch.captured_at = {captured_at};
    batch.ready_at = {captured_at + 0.002};
    batch.screen_center_px = {320.0f, 256.0f};
    batch.detections = std::move(detections);
    return batch;
}

void test_track_memory_ingests_all_candidates_without_assist_authority() {
    tracking_native::TrackMemoryService memory;
    memory.ingest(make_batch(10, 12.000, {
        make_detection(101, 80.0f),
        make_detection(102, 480.0f),
    }));
    memory.ingest(make_batch(11, 12.010, {
        make_detection(201, 84.0f),
        make_detection(202, 476.0f),
    }));

    const std::vector<pipeline_contract::TrackEstimate> estimates =
        memory.estimates({12.015});
    require(estimates.size() == 2,
            "track memory must expose every live candidate, not only a preferred target");

    const pipeline_contract::SelectedTrackRef left =
        memory.resolve_selected_observation(201, {12.015});
    const pipeline_contract::SelectedTrackRef right =
        memory.resolve_selected_observation(202, {12.015});
    require(left.has_selection && right.has_selection,
            "each current observation must resolve to its exact track");
    require(left.track_id != right.track_id,
            "different candidates must retain different track identities");
    require(left.selected_observation_id == 201 && right.selected_observation_id == 202,
            "selection resolution must preserve the selector-owned observation id");
    require(left.backing_frame_id == 11 && right.backing_frame_id == 11,
            "resolved tracks must be backed by the selected frame");

    const pipeline_contract::SelectedTrackRef missing =
        memory.resolve_selected_observation(9999, {12.015});
    require(!missing.has_selection,
            "track memory must not substitute a center-nearest track for an unknown observation");

    for (const pipeline_contract::TrackEstimate& estimate : estimates) {
        require_near(estimate.last_observed_at.value, 12.010, 1e-9,
                     "track estimate must keep the real capture timestamp");
        require_near(estimate.observation_age_ms, 5.0, 1e-6,
                     "observation age must be derived from capture time, not query time rewriting");
    }
}

std::vector<pipeline_contract::TrackEstimate> run_fps_memory_with_legacy_knobs(
    float velocity_alpha,
    float weak_decay) {
    pipeline_contract::TargetTrackerConfig config;
    config.velocity_lowpass_alpha = velocity_alpha;
    config.weak_observation_velocity_decay = weak_decay;
    tracking_native::TrackMemoryService memory(config);
    memory.ingest(make_batch(20, 20.000, {make_detection(301, 200.0f)}));
    memory.ingest(make_batch(21, 20.010, {make_detection(302, 208.0f)}));
    auto weak = make_detection(303, 216.0f, 0.30f);
    weak.evidence_tier = "associated_weak";
    memory.ingest(make_batch(22, 20.020, {weak}));
    return memory.estimates({20.030});
}

void test_fps_memory_legacy_velocity_knobs_are_explicitly_inactive() {
    const auto zero = run_fps_memory_with_legacy_knobs(0.0f, 0.0f);
    const auto one = run_fps_memory_with_legacy_knobs(1.0f, 1.0f);
    require(zero.size() == one.size() && !zero.empty(),
            "characterization requires the same live FPS tracks");
    for (std::size_t index = 0; index < zero.size(); ++index) {
        require_near(zero[index].aim_error_px.x, one[index].aim_error_px.x, 1e-6,
                     "legacy velocity alpha must not pretend to tune FPS memory");
        require_near(zero[index].aim_error_px.y, one[index].aim_error_px.y, 1e-6,
                     "legacy weak decay must not pretend to tune FPS memory");
        require_near(zero[index].velocity_model_units_per_sec.x,
                     one[index].velocity_model_units_per_sec.x, 1e-6,
                     "FPS velocity must be independent of legacy backend knobs");
    }
}

}  // namespace

int main() {
    try {
        test_estimate_keeps_identity_geometry_and_real_observation_time();
        test_selection_and_authority_are_separate_value_types();
        test_track_memory_ingests_all_candidates_without_assist_authority();
        test_fps_memory_legacy_velocity_knobs_are_explicitly_inactive();
        std::cout << "[TrackMemoryServiceTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TrackMemoryServiceTests][FAIL] " << error.what() << "\n";
        return 1;
    }
}
