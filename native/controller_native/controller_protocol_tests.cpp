#include "../runtime_app/vision_controller_adapter.h"
#include "../vision_native/include/vision_native/vision_result_copy.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

vision_native::Detection detection(
    float x1,
    float y1,
    float x2,
    float y2,
    float confidence,
    float color_bonus,
    int class_id,
    bool is_friendly = false) {
    vision_native::Detection result;
    result.x1 = x1;
    result.y1 = y1;
    result.x2 = x2;
    result.y2 = y2;
    result.conf = confidence;
    result.color_bonus = color_bonus;
    result.class_id = class_id;
    result.is_friendly = is_friendly;
    return result;
}

void test_adapter_ignores_unupdated_frame() {
    vision_native::VisionResult result;
    result.frame_updated = false;
    result.has_target = true;
    result.dx = 18.0f;
    result.detections.push_back(detection(10.0f, 20.0f, 30.0f, 60.0f, 0.9f, 0.0f, 1));

    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);

    require_true(!snapshot.frame_updated, "adapter should preserve frame_updated=false");
    require_true(!snapshot.state.has_target, "adapter should not expose stale target state");
    require_true(snapshot.candidates.empty(), "adapter should not forward stale candidate list");
}

void test_adapter_maps_target_fields_and_timestamps() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 42;
    result.has_target = true;
    result.auto_fire = true;
    result.dx = -12.5f;
    result.dy = 8.25f;
    result.target_x = 307.5f;
    result.target_y = 264.25f;
    result.screen_center_x = 320.0f;
    result.screen_center_y = 256.0f;
    result.has_body_box = true;
    result.body_x1 = 280.0f;
    result.body_y1 = 210.0f;
    result.body_x2 = 360.0f;
    result.body_y2 = 300.0f;
    result.aim_authority = true;
    result.fire_authority = false;
    result.target_tier = "cue_hold";
    result.captured_at_ns = 2'000'000'000ull;
    result.result_at_ns = 2'012'000'000ull;

    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);

    require_true(snapshot.frame_updated, "adapter should consume updated frame");
    require_true(snapshot.frame_id == 42, "adapter should preserve frame id");
    require_true(snapshot.state.has_target, "adapter should preserve target presence");
    require_true(snapshot.state.auto_fire_requested, "adapter should preserve auto fire request");
    require_near(snapshot.state.dx, -12.5f, 0.001f, "adapter should map dx");
    require_near(snapshot.state.dy, 8.25f, 0.001f, "adapter should map dy");
    require_near(snapshot.state.target_x, 307.5f, 0.001f, "adapter should map target x");
    require_near(snapshot.state.target_y, 264.25f, 0.001f, "adapter should map target y");
    require_true(snapshot.state.has_body_box, "adapter should map body box flag");
    require_near(snapshot.state.body_x1, 280.0f, 0.001f, "adapter should map body x1");
    require_true(snapshot.state.aim_authority, "adapter should map aim authority");
    require_true(!snapshot.state.fire_authority, "adapter should preserve no-fire authority");
    require_true(snapshot.state.target_tier == "cue_hold", "adapter should map target tier");
    require_near(
        snapshot.state.observed_at_seconds,
        2.012,
        0.000001,
        "adapter should use ready/result timestamp for observed time");
    require_near(
        snapshot.capture_time_seconds,
        2.0,
        0.000001,
        "adapter should map capture timestamp");
    require_near(
        snapshot.ready_time_seconds,
        2.012,
        0.000001,
        "adapter should map ready timestamp");
}

void test_adapter_forwards_valid_vision_candidates() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 7;
    result.has_target = true;
    result.has_selected_detection = true;
    result.selected_detection_index = 0;
    result.target_x = 31.0f;
    result.target_y = 58.0f;
    result.has_aim_region = true;
    result.aim_region_x1 = 18.0f;
    result.aim_region_y1 = 38.0f;
    result.aim_region_x2 = 42.0f;
    result.aim_region_y2 = 92.0f;
    result.user_aim_intent.valid = true;
    result.user_aim_intent.intent_id = 99;
    result.user_aim_intent.strength = 0.75f;
    result.user_aim_intent.has_direction = true;
    result.user_aim_intent.direction.x = -1.0f;
    result.user_aim_intent.aiming = true;
    result.detections.push_back(detection(10.0f, 20.0f, 50.0f, 120.0f, 0.30f, 0.20f, 1));
    result.detections.push_back(detection(80.0f, 90.0f, 80.5f, 100.0f, 0.90f, 0.0f, 1));
    result.detections.push_back(detection(100.0f, 110.0f, 140.0f, 210.0f, 0.20f, 0.0f, 2, true));

    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);

    require_true(snapshot.user_intent.valid, "adapter should forward user intent to middle layer");
    require_true(
        snapshot.user_intent.intent_id == 99,
        "adapter should preserve user intent id");
    require_near(
        snapshot.user_intent.strength,
        0.75f,
        0.001f,
        "adapter should preserve user intent strength");

    require_true(
        snapshot.rejected_low_reliability_count == 1,
        "adapter should count low-reliability detections separately");
    require_true(
        snapshot.rejected_friendly_count == 1,
        "adapter should count friendly detections separately");
    require_true(
        snapshot.candidates.size() == 1,
        "adapter should expose only eligible detections as candidate snapshots");
    const pipeline_contract::VisionCandidateSnapshot& first_candidate = snapshot.candidates[0];
    require_true(
        first_candidate.id == ((7ull << 32ull) | 1ull),
        "adapter should derive a source-generation candidate id");
    require_near(first_candidate.body_box_px.x, 10.0f, 0.001f, "adapter should map box x");
    require_near(first_candidate.body_box_px.w, 40.0f, 0.001f, "adapter should map box width");
    require_near(first_candidate.aim_point_px.x, 31.0f, 0.001f,
                 "adapter must preserve selector-owned aim point x");
    require_near(first_candidate.confidence, 0.50f, 0.001f, "adapter should combine confidence evidence");
    require_true(
        first_candidate.suggested_authority_state ==
            common_native::TargetAuthorityState::StrongAssist,
        "candidate should expose strong suggested authority for confident enemy evidence");
    require_near(
        first_candidate.aim_point_px.y,
        58.0f,
        0.001f,
        "candidate must not recompute the selector-owned aim point");
    require_true(first_candidate.has_aim_region,
                 "selected candidate must carry selector-owned R");
    require_near(first_candidate.aim_region_px.y, 38.0f, 0.001f,
                 "adapter must preserve selector-owned R geometry");
}

void test_selector_identity_survives_engine_result_copy_and_adapter() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 19;

    vision_native::VisionResult targeting;
    targeting.selector_identity_protocol = true;
    targeting.has_selected_detection = true;
    targeting.selected_detection_index = 1;
    targeting.detections.push_back(
        detection(10.0f, 20.0f, 50.0f, 120.0f, 0.80f, 0.0f, 1));
    targeting.detections.push_back(
        detection(70.0f, 30.0f, 120.0f, 150.0f, 0.90f, 0.2f, 1));

    vision_native::copy_selector_identity_fields(result, targeting);
    result.detections = std::move(targeting.detections);
    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);

    require_true(
        snapshot.selector_identity_protocol,
        "VisionEngine result copy should preserve selector-owned identity protocol");
    require_true(
        snapshot.selected_observation_id == ((19ull << 32ull) | 2ull),
        "adapter should derive the selected observation from the copied detection index");
}

}  // namespace

int main() {
    test_adapter_ignores_unupdated_frame();
    test_adapter_maps_target_fields_and_timestamps();
    test_adapter_forwards_valid_vision_candidates();
    test_selector_identity_survives_engine_result_copy_and_adapter();
    return 0;
}
