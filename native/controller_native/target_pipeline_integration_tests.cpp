#include "native_gamepad_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ControllerVisionSnapshot target(
    std::uint64_t frame_id,
    double now,
    float dx = 80.0f,
    float dy = -40.0f) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selected_observation_id = 42;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_x = 320.0f + dx;
    snapshot.state.target_y = 256.0f + dy;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = now;

    tracking_native::TrackerDetection detection;
    detection.id = 42;
    detection.body_box_px = {snapshot.state.target_x - 35.0f,
                             snapshot.state.target_y - 70.0f,
                             70.0f,
                             180.0f};
    detection.aim_point_px = {snapshot.state.target_x, snapshot.state.target_y};
    detection.has_aim_point = true;
    detection.confidence = 0.95f;
    detection.target_tier = "observed_strong";
    snapshot.tracker_detections.push_back(detection);
    return snapshot;
}

GamepadRuntimeConfig config() {
    GamepadRuntimeConfig value;
    value.recoil.enabled = false;
    value.aim_assist_dynamics.enabled = true;
    value.ai_aim.max_pixels = 100.0f;
    value.ai_aim.max_ai_force = 1.0f;
    value.ai_aim.max_ai_force_y = 1.0f;
    return value;
}

PhysicalGamepadState aiming(float left_x = 0.0f) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.left_x = left_x;
    return state;
}

float warm_ads_force(float left_x) {
    double now = 10.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming(left_x);
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        controller.submit_vision_snapshot(target(frame, now));
        controller.build_output(physical);
        now += 0.010;
    }
    return std::fabs(controller.last_output_components().ai_aim_stick.x);
}

void test_ads_is_bounded_and_drift_is_ignored() {
    const float neutral = warm_ads_force(0.0f);
    const float drift = warm_ads_force(-0.0118f);
    require(neutral > 0.05f && neutral <= 1.0f, "ADS did not produce bounded assist");
    require(std::fabs(neutral - drift) <= 0.01f,
            "left-stick drift incorrectly weakened ADS owner hold");
}

void test_vision_gap_uses_smooth_short_continuity() {
    double now = 20.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming();
    controller.submit_vision_snapshot(target(1, now));
    controller.build_output(physical);
    now += 0.010;
    controller.submit_vision_snapshot(target(2, now));
    controller.build_output(physical);

    now += 0.010;
    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 3;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);

    float previous = 2.0f;
    float first = -1.0f;
    float last = 0.0f;
    int continuity_ticks = 0;
    int sign_reversals = 0;
    float previous_signed = 0.0f;
    for (int tick = 0; tick < 240; ++tick) {
        now += 0.001;
        controller.build_output(physical);
        const float signed_force = controller.last_output_components().ai_aim_stick.x;
        const float force = std::fabs(signed_force);
        if (first < 0.0f) first = force;
        last = force;
        require(force <= previous + 0.001f, "continuity assist rose during occlusion");
        if (previous_signed * signed_force < 0.0f) ++sign_reversals;
        previous = force;
        previous_signed = signed_force;
        if (controller.last_frame_vision_state().assist_authority_state ==
            pipeline_contract::AssistAuthorityState::Continuity) {
            ++continuity_ticks;
        }
    }
    require(continuity_ticks > 0, "brief occlusion did not enter continuity hold");
    require(first > last, "continuity hold did not decay");
    require(sign_reversals == 0, "continuity hold reversed direction");
}

void test_opposing_manual_intent_yields_without_braking_bodylock() {
    double now = 30.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming();
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        controller.submit_vision_snapshot(target(frame, now, 24.0f, 0.0f));
        controller.build_output(physical);
        now += 0.010;
    }
    physical.right_x = -0.9f;
    int conflict_ticks = 0;
    for (int tick = 0; tick < 20; ++tick) {
        now += 0.001;
        controller.build_output(physical);
        if (controller.last_output_components().ai_aim_stick.x > 0.02f) {
            ++conflict_ticks;
        }
    }
    require(conflict_ticks < 16, "assist fought strong opposing manual intent too long");
}

void test_only_worsening_wrong_way_axis_stops_suppressing_assist() {
    double now = 35.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming();
    physical.right_x = -0.30f;
    physical.right_y = 0.20f;

    bool intervened_x = false;
    bool intervened_y = false;
    float minimum_retention_x = 1.0f;
    float minimum_retention_y = 1.0f;
    for (std::uint64_t frame = 1; frame <= 6; ++frame) {
        const float growing_x_error = 10.0f + static_cast<float>(frame) * 3.0f;
        controller.submit_vision_snapshot(
            target(frame, now, growing_x_error, -20.0f));
        controller.build_output(physical);
        const auto& components = controller.last_output_components();
        intervened_x = intervened_x || components.axis_intent_intervention.x > 0.5f;
        intervened_y = intervened_y || components.axis_intent_intervention.y > 0.5f;
        minimum_retention_x = std::min(
            minimum_retention_x, components.axis_manual_retention.x);
        minimum_retention_y = std::min(
            minimum_retention_y, components.axis_manual_retention.y);
        now += 0.010;
    }

    require(intervened_x,
            "worsening wrong-way X input did not stop suppressing assist");
    require(!intervened_y,
            "helpful/non-worsening Y input was incorrectly overridden");
    require(minimum_retention_x < 0.90f && minimum_retention_x >= 0.65f,
            "confirmed wrong X must attenuate only to the configured floor");
    require(std::fabs(minimum_retention_y - 1.0f) < 0.0001f,
            "wrong X must preserve the complete Y-axis input");
}

void test_left_reversal_projects_before_the_next_vision_frame() {
    double now = 37.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming(0.5f);
    for (std::uint64_t frame = 1; frame <= 80; ++frame) {
        auto snapshot = target(frame, now, 40.0f, 0.0f);
        snapshot.state.has_camera_attributed_velocity = true;
        snapshot.state.camera_attributed_velocity_x_px_per_sec = -100.0f;
        controller.submit_vision_snapshot(snapshot);
        controller.build_output(physical);
        now += 0.010;
    }

    auto fresh = target(81, now, 40.0f, 0.0f);
    fresh.state.has_camera_attributed_velocity = true;
    fresh.state.camera_attributed_velocity_x_px_per_sec = -100.0f;
    controller.submit_vision_snapshot(fresh);
    controller.build_output(physical);
    require(std::fabs(
        controller.last_output_components().left_intent_projection_px.x) < 0.0001f,
        "fresh vision must consume previous left intent");

    physical.left_x = -0.5f;
    now += 0.005;
    controller.build_output(physical);
    const auto& components = controller.last_output_components();
    require(components.left_intent_projection_px.x > 0.20f,
            "left reversal must project a correction before the next vision frame");
    require(std::fabs(components.left_intent_projection_px.y) < 0.0001f,
            "left reversal projection must remain X-only");
}

void test_ads_and_bodylock_share_one_resolved_target_geometry() {
    double now = 40.0;
    auto controller_config = config();
    controller_config.tracker.aim_height_ratio = 0.365f;
    controller_config.ai_aim.body_lock_confidence_frames = 2;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming();
    bool saw_ads = false;
    bool saw_bodylock = false;
    constexpr float expected_y = 180.0f + 200.0f * 0.365f;

    for (std::uint64_t frame = 1; frame <= 16; ++frame) {
        auto snapshot = target(frame, now, 2.0f, -3.0f);
        pipeline_contract::VisionCandidateSnapshot candidate;
        candidate.id = 42;
        candidate.valid = true;
        candidate.has_aim_point = true;
        candidate.aim_point_px = {322.0f, 253.0f};
        candidate.body_box_px = {282.0f, 180.0f, 80.0f, 200.0f};
        candidate.confidence = 0.95f;
        snapshot.candidates.push_back(candidate);
        controller.submit_vision_snapshot(snapshot);
        controller.build_output(physical);
        saw_ads = saw_ads || controller.last_ai_aim_mode() == "ads_snap";
        saw_bodylock = saw_bodylock || controller.last_ai_aim_mode() == "body_lock";
        require(
            std::fabs(controller.last_frame_vision_state().target_y - expected_y) < 0.001f,
            "ADS/BodyLock applied inconsistent or repeated target geometry");
        now += 0.010;
    }
    require(saw_ads, "geometry handoff never entered ADS");
    require(saw_bodylock, "geometry handoff never entered BodyLock");

    now += 0.001;
    controller.build_output(physical);
    require(
        std::fabs(controller.last_frame_vision_state().target_y - expected_y) < 0.001f,
        "coasting applied target height ratio a second time");
}

}  // namespace

int main() {
    try {
        test_ads_is_bounded_and_drift_is_ignored();
        test_vision_gap_uses_smooth_short_continuity();
        test_opposing_manual_intent_yields_without_braking_bodylock();
        test_only_worsening_wrong_way_axis_stops_suppressing_assist();
        test_left_reversal_projects_before_the_next_vision_frame();
        test_ads_and_bodylock_share_one_resolved_target_geometry();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
