#include "target_snapshot_provider.h"

#include "../runtime_app/vision_controller_adapter.h"

#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    const float delta = actual > expected ? actual - expected : expected - actual;
    if (delta > tolerance) {
        throw std::runtime_error(message);
    }
}

controller_native::ControllerVisionSnapshot snapshot_with_target(float dx, double now_seconds) {
    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.frame_id = 10;
    snapshot.capture_time_seconds = now_seconds;
    snapshot.ready_time_seconds = now_seconds;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = dx;
    snapshot.state.dy = 0.0f;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_tier = "strong";
    snapshot.state.observed_at_seconds = now_seconds;
    return snapshot;
}

pipeline_contract::VisionCandidateSnapshot candidate(
    std::uint64_t id,
    float target_x,
    float target_y,
    float confidence) {
    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = id;
    candidate.valid = true;
    candidate.body_box_px = {target_x - 20.0f, target_y - 40.0f, 40.0f, 100.0f};
    candidate.aim_point_px = {target_x, target_y};
    candidate.has_aim_point = true;
    candidate.confidence = confidence;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    return candidate;
}

tracking_native::TrackerDetection tracker_detection(
    std::uint64_t id,
    float target_x,
    float target_y,
    float confidence) {
    tracking_native::TrackerDetection detection;
    detection.id = id;
    detection.body_box_px = {target_x - 20.0f, target_y - 40.0f, 40.0f, 100.0f};
    detection.aim_point_px = {target_x, target_y};
    detection.has_aim_point = true;
    detection.confidence = confidence;
    detection.target_tier = "observed_strong";
    return detection;
}

void test_provider_preserves_latest_target_on_no_update_snapshot() {
    controller_native::GamepadAiAimConfig ai_config;
    ai_config.target_max_age_ms = 0.0f;
    ai_config.target_projection_max_age_ms = 50.0f;
    controller_native::TargetSnapshotProvider provider(
        ai_config,
        tracking_native::TrackerBackendKind::LegacyProjection);

    provider.submit_vision_state(snapshot_with_target(42.0f, 10.0).state, 10.0, false);
    controller_native::NativeControllerVisionState state =
        provider.vision_state_for_frame(10.0, false);
    require_true(state.has_target, "provider should expose submitted target");
    require_near(state.dx, 42.0f, 0.001f, "provider should preserve submitted dx");

    controller_native::ControllerVisionSnapshot no_update;
    no_update.frame_updated = false;
    no_update.state.has_target = false;
    provider.submit_vision_snapshot(no_update, 10.010, false);

    state = provider.vision_state_for_frame(10.010, false);
    require_true(state.has_target, "provider should preserve target across no-update snapshot");
    require_near(state.dx, 42.0f, 0.001f, "provider should preserve dx across no-update snapshot");
}

void test_provider_binds_only_the_selector_owned_observation() {
    controller_native::GamepadAiAimConfig ai_config;
    ai_config.target_max_age_ms = 0.0f;
    ai_config.target_projection_max_age_ms = 50.0f;
    controller_native::TargetSnapshotProvider provider(
        ai_config,
        tracking_native::TrackerBackendKind::FpsReference);

    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.frame_id = 11;
    snapshot.capture_time_seconds = 20.0;
    snapshot.ready_time_seconds = 20.0;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_x = 430.0f;
    snapshot.state.target_y = 256.0f;
    snapshot.state.dx = 110.0f;
    snapshot.state.dy = 0.0f;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = 20.0;
    snapshot.user_intent.valid = true;
    snapshot.user_intent.aiming = true;
    snapshot.user_intent.has_direction = true;
    snapshot.user_intent.strength = 1.0f;
    snapshot.user_intent.direction.x = 1.0f;
    snapshot.user_intent.direction.y = 0.0f;
    snapshot.selected_observation_id = 2;
    snapshot.candidates.push_back(candidate(1, 320.0f, 256.0f, 0.99f));
    snapshot.candidates.push_back(candidate(2, 430.0f, 256.0f, 0.70f));
    snapshot.tracker_detections.push_back(tracker_detection(1, 320.0f, 256.0f, 0.99f));
    snapshot.tracker_detections.push_back(tracker_detection(2, 430.0f, 256.0f, 0.70f));

    provider.submit_vision_snapshot(snapshot, 20.0, true);

    snapshot.frame_id = 12;
    snapshot.capture_time_seconds = 20.010;
    snapshot.ready_time_seconds = 20.010;
    snapshot.state.observed_at_seconds = 20.010;
    snapshot.selected_observation_id = 102;
    snapshot.candidates.clear();
    snapshot.tracker_detections.clear();
    snapshot.candidates.push_back(candidate(101, 320.0f, 256.0f, 0.99f));
    snapshot.candidates.push_back(candidate(102, 430.0f, 256.0f, 0.70f));
    snapshot.tracker_detections.push_back(tracker_detection(101, 320.0f, 256.0f, 0.99f));
    snapshot.tracker_detections.push_back(tracker_detection(102, 430.0f, 256.0f, 0.70f));
    provider.submit_vision_snapshot(snapshot, 20.010, true);
    const controller_native::NativeControllerVisionState state =
        provider.vision_state_for_frame(20.010, true);

    require_true(state.has_target, "selector-owned target should stay assistable");
    require_near(state.target_x, 430.0f, 0.001f, "provider must not reselect the centered tracker favorite");
    require_near(state.dx, 110.0f, 0.001f, "provider must keep selector-owned target geometry");
    require_true(state.selected_observation_id == 102,
                 "controller state must retain selector observation identity");
    require_true(state.selected_track_id != 0,
                 "selector observation must bind to a concrete track id");
}

controller_native::ControllerVisionSnapshot owned_snapshot(
    std::uint64_t frame_id,
    std::uint64_t observation_id,
    double time_seconds,
    const char* tier,
    bool aim_authority,
    bool fire_authority) {
    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = time_seconds;
    snapshot.ready_time_seconds = time_seconds;
    snapshot.selected_observation_id = observation_id;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = aim_authority;
    snapshot.state.fire_authority = fire_authority;
    snapshot.state.auto_fire_requested = fire_authority;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_x = 400.0f;
    snapshot.state.target_y = 256.0f;
    snapshot.state.dx = 80.0f;
    snapshot.state.target_tier = tier;
    snapshot.state.observed_at_seconds = time_seconds;
    snapshot.tracker_detections.push_back(
        tracker_detection(observation_id, 400.0f, 256.0f, 0.90f));
    snapshot.tracker_detections.back().target_tier = tier;
    return snapshot;
}

void test_provider_keeps_weak_evidence_track_only() {
    controller_native::GamepadAiAimConfig ai_config;
    ai_config.target_max_age_ms = 100.0f;
    ai_config.target_projection_max_age_ms = 80.0f;
    controller_native::TargetSnapshotProvider provider(ai_config);

    provider.submit_vision_snapshot(
        owned_snapshot(20, 201, 30.000, "associated_weak", true, true),
        30.000,
        true);
    provider.submit_vision_snapshot(
        owned_snapshot(21, 211, 30.010, "associated_weak", true, true),
        30.010,
        true);
    const auto state = provider.vision_state_for_frame(30.010, true);
    require_true(state.has_target, "weak evidence may remain available to track memory");
    require_true(!state.aim_authority, "weak evidence must not grant aim authority");
    require_true(!state.fire_authority, "weak evidence must never grant fire authority");
}

void test_provider_continuity_keeps_real_observation_time_and_no_fire() {
    controller_native::GamepadAiAimConfig ai_config;
    ai_config.target_max_age_ms = 100.0f;
    ai_config.target_projection_max_age_ms = 80.0f;
    controller_native::TargetSnapshotProvider provider(ai_config);

    provider.submit_vision_snapshot(
        owned_snapshot(30, 301, 40.000, "observed_strong", true, true),
        40.000,
        true);
    provider.submit_vision_snapshot(
        owned_snapshot(31, 311, 40.010, "observed_strong", true, true),
        40.010,
        true);

    const auto observed_frame = provider.vision_state_for_frame(40.010, true);
    require_true(observed_frame.fire_authority,
                 "the current consumed controller frame may retain observed-only fire");

    const auto between_frames = provider.vision_state_for_frame(40.011, true);
    require_true(between_frames.aim_authority,
                 "same-track time between vision frames should retain continuity aim");
    require_true(!between_frames.fire_authority,
                 "a consumed observation must not keep fire authority between frames");
    require_true(
        between_frames.assist_authority_state ==
            pipeline_contract::AssistAuthorityState::Continuity,
        "a consumed observation must become explicit continuity between frames");

    controller_native::ControllerVisionSnapshot gap;
    gap.frame_updated = true;
    gap.selector_identity_protocol = true;
    gap.frame_id = 32;
    gap.capture_time_seconds = 40.020;
    gap.ready_time_seconds = 40.020;
    gap.state.screen_center_x = 320.0f;
    gap.state.screen_center_y = 256.0f;
    provider.submit_vision_snapshot(gap, 40.020, true);

    const auto state = provider.vision_state_for_frame(40.020, true);
    require_true(state.has_target, "short same-track gap should retain a continuity target");
    require_true(state.aim_authority, "short same-track gap should receive coast authority");
    require_true(!state.fire_authority, "continuity must revoke fire authority");
    require_near(
        static_cast<float>(state.observed_at_seconds),
        40.010f,
        0.0001f,
        "continuity must keep the real last observation capture time");
}

void test_adapter_uses_one_stable_id_for_selector_and_tracker() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.selector_identity_protocol = true;
    result.frame_id = 77;
    result.has_selected_detection = true;
    result.selected_detection_index = 1;
    result.screen_center_x = 320.0f;
    result.screen_center_y = 256.0f;
    vision_native::Detection first;
    first.x1 = 100.0f;
    first.y1 = 100.0f;
    first.x2 = 160.0f;
    first.y2 = 240.0f;
    first.conf = 0.90f;
    vision_native::Detection second = first;
    second.x1 = 400.0f;
    second.x2 = 460.0f;
    second.conf = 0.80f;
    result.detections.push_back(first);
    result.detections.push_back(second);

    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);
    const std::uint64_t expected_id = (77ull << 32ull) | 2ull;
    require_true(snapshot.selected_observation_id == expected_id,
                 "adapter must translate selector index to the stable observation id");
    require_true(snapshot.tracker_detections.size() == 2,
                 "adapter must transport every detector candidate");
    require_true(snapshot.tracker_detections[1].id == expected_id,
                 "selected observation and tracker detection must share one id function");
}

}  // namespace

int main() {
    try {
        test_provider_preserves_latest_target_on_no_update_snapshot();
        test_adapter_uses_one_stable_id_for_selector_and_tracker();
        test_provider_binds_only_the_selector_owned_observation();
        test_provider_keeps_weak_evidence_track_only();
        test_provider_continuity_keeps_real_observation_time_and_no_fire();
        std::cout << "[TargetSnapshotProviderTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetSnapshotProviderTests][FAIL] " << error.what() << "\n";
        return 1;
    }
}
