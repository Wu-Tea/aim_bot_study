#include "native_gamepad_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    float dy = -40.0f,
    std::uint64_t observation_id = 42) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selected_observation_id = observation_id;
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
    detection.id = observation_id;
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

ControllerVisionSnapshot fire_target(std::uint64_t frame_id, double now) {
    auto snapshot = target(frame_id, now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    return snapshot;
}

ControllerVisionSnapshot sized_target(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    float body_height_px,
    std::uint64_t observation_id) {
    auto snapshot = target(frame_id, now, dx, dy, observation_id);
    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = observation_id;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {
        snapshot.state.target_x, snapshot.state.target_y};
    constexpr float kAimHeightRatio = 0.365f;
    const float body_width_px = body_height_px * 0.40f;
    candidate.body_box_px = {
        snapshot.state.target_x - body_width_px * 0.5f,
        snapshot.state.target_y - body_height_px * kAimHeightRatio,
        body_width_px,
        body_height_px};
    candidate.confidence = 0.95f;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

ControllerVisionSnapshot empty_fresh_snapshot(
    std::uint64_t frame_id,
    double now) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
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

void test_production_remaining_work_consumes_only_confirmed_delivery() {
    double now = 15.0;
    auto enabled_config = config();
    enabled_config.tracker.remaining_work_enabled = true;
    enabled_config.tracker.remaining_work_scale = 0.60f;
    NativeGamepadController enabled(
        enabled_config, [&now] { return now; });
    auto physical = aiming();

    enabled.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)enabled.build_output(physical);
    enabled.report_output_delivery(true, true, now + 0.000001);
    const float initial_error =
        std::fabs(enabled.last_target_plan().error_px.x);

    now += 0.010;
    (void)enabled.build_output(physical);
    const auto delivered_plan = enabled.last_target_plan();
    require(
        delivered_plan.remaining_work_valid,
        "confirmed output delivery must activate remaining-work accounting");
    require(
        std::fabs(delivered_plan.remaining_work_px.x) <
            initial_error,
        "delivered camera motion must reduce remaining X work");

    enabled.report_output_delivery(false, true, now + 0.000001);
    now += 0.001;
    (void)enabled.build_output(physical);
    require(
        !enabled.last_target_plan().remaining_work_valid,
        "failed delivery must reset remaining-work authority");

    now = 15.0;
    auto disabled_config = enabled_config;
    disabled_config.tracker.remaining_work_enabled = false;
    NativeGamepadController disabled(
        disabled_config, [&now] { return now; });
    disabled.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)disabled.build_output(physical);
    disabled.report_output_delivery(true, true, now + 0.000001);
    now += 0.010;
    (void)disabled.build_output(physical);
    require(
        !disabled.last_target_plan().remaining_work_valid,
        "rollback config must preserve the legacy error path");
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
        const auto delivered = controller.build_output(physical);
        if (delivered.right_x > 0.02f) {
            ++conflict_ticks;
        }
    }
    require(conflict_ticks < 16, "assist fought strong opposing manual intent too long");
}

void test_benchmark_mix_override_updates_delivered_feedback() {
    double now = 32.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_mix_transform(
        [](float manual_x, float manual_y, float, float,
           const controller_native::NativeControllerOutputComponents&) {
            return pipeline_contract::Vec2f{
                manual_x * 0.5f, manual_y * 0.5f};
        });
    auto physical = aiming();
    physical.right_x = 0.20f;
    physical.right_y = -0.12f;

    const auto output = controller.build_output(physical);

    require(std::fabs(output.right_x - 0.10f) < 1e-6f,
            "override must replace delivered X");
    require(std::fabs(output.right_y + 0.06f) < 1e-6f,
            "override must replace delivered Y");
    require(std::fabs(
                controller.last_output_components().before_recoil_stick.x -
                output.right_x) < 1e-6f,
            "feedback components must record overridden X");
    require(std::fabs(
                controller.last_tracker_motion_output().right_x -
                output.right_x) < 1e-6f,
            "tracker feedback must match delivered output");
}

void test_benchmark_vector_fusion_is_one_reported_pipeline_stage() {
    double now = 34.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = 0.20f;
    physical.right_y = -0.12f;

    const auto output = controller.build_output(physical);
    const auto& components = controller.last_output_components();
    require(components.intent_fusion_mode == "continuous_vector",
            "benchmark vector mode must be identified in output diagnostics");
    require(components.intent_fusion_fallback,
            "no-target vector mode must report its manual fallback");
    require(std::fabs(output.right_x - physical.right_x) < 1e-6f &&
            std::fabs(output.right_y - physical.right_y) < 1e-6f,
            "vector fallback must apply physical manual input exactly once");
}

float vector_mode_shaped_assist_for(float manual_x) {
    double now = 34.5;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = manual_x;
    for (std::uint64_t frame = 1; frame <= 8; ++frame) {
        controller.submit_vision_snapshot(target(frame, now, 80.0f, 0.0f));
        controller.build_output(physical);
        now += 0.010;
    }
    return std::fabs(
        controller.last_output_components().shaped_assist_stick.x);
}

void test_vector_mode_generates_ai_before_manual_arbitration() {
    const float neutral_ai = vector_mode_shaped_assist_for(0.0f);
    const float opposing_ai = vector_mode_shaped_assist_for(-0.32f);
    require(neutral_ai > 0.05f,
            "fixture must produce material unopposed AI assistance");
    require(opposing_ai >= neutral_ai * 0.95f,
            "vector mode must not weaken AI before its single fusion point");
}

void test_ads_strong_cooperative_mix_uses_ai_as_the_radial_proposal() {
    double now = 34.7;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 300;
    controller_config.ai_aim.ads_completion_fresh_frames = 1000;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = 0.80f;

    controller_native::GamepadOutputState output{};
    for (std::uint64_t frame = 1; frame <= 20; ++frame) {
        controller.submit_vision_snapshot(
            sized_target(frame, now, 35.0f, 0.0f, 70.0f, 710));
        output = controller.build_output(physical);
        now += 0.010;
    }

    const auto& components = controller.last_output_components();
    require(controller.last_ai_aim_mode() == "ads_snap",
            "strong-mix ADS fixture left acquisition mode");
    require(components.shaped_assist_stick.x > 0.05f,
            "strong-mix ADS fixture did not generate material AI");
    require(components.intent_fusion_candidate == static_cast<int>(
                controller_native::FusionCandidate::RadialCorrected),
            "ADS strong mix did not enter assisted proposal ownership");
    require(components.intent_fusion_manual_weight >= 0.15f &&
                components.intent_fusion_manual_weight <= 0.20f,
            "ADS strong mix did not retain bounded participation from both proposals");
    require(output.right_x < physical.right_x - 0.10f,
            "ADS strong mix still behaved like additive manual + AI force");
}

void test_near_bodylock_strong_mix_prefers_ai_but_far_stays_legacy() {
    auto run_fixture = [](float body_height_px,
                          std::uint64_t observation_id) {
        double now = 34.9;
        auto controller_config = config();
        controller_config.ai_aim.ads_completion_fresh_frames = 1;
        controller_config.ai_aim.ads_completion_radius_px = 8.0f;
        controller_config.ai_aim.body_lock_confidence_frames = 1;
        NativeGamepadController controller(
            controller_config, [&now] { return now; });
        controller.set_benchmark_intent_fusion_mode(
            controller_native::BenchmarkIntentFusionMode::CausalVector);
        auto physical = aiming();
        physical.right_x = 0.80f;

        controller.submit_vision_snapshot(sized_target(
            1, now, 2.0f, 0.0f, body_height_px, observation_id));
        (void)controller.build_output(physical);
        for (std::uint64_t frame = 2; frame <= 20; ++frame) {
            now += 0.010;
            controller.submit_vision_snapshot(sized_target(
                frame, now, 35.0f, 0.0f,
                body_height_px, observation_id));
            (void)controller.build_output(physical);
        }
        return std::pair{
            controller.last_target_plan(),
            controller.last_output_components()};
    };

    const auto [near_plan, near_components] = run_fixture(180.0f, 711);
    require(near_plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
            "near strong-mix fixture did not enter BodyLock");
    require(near_plan.normalized_size >= 0.24f,
            "near strong-mix fixture did not enter the size policy scope");
    require(near_components.shaped_assist_stick.x > 0.05f,
            "near strong-mix fixture did not generate material AI");
    require(near_components.intent_fusion_candidate == static_cast<int>(
                controller_native::FusionCandidate::RadialCorrected),
            "near BodyLock did not enter assisted proposal ownership");
    require(near_components.intent_fusion_manual_weight >= 0.10f &&
                near_components.intent_fusion_manual_weight <= 0.50f,
            "near BodyLock did not retain bounded participation from both proposals");
    require(near_components.intent_fusion_ai_weight >= 0.50f,
            "near BodyLock attenuated AI instead of arbitrating both proposals");

    const auto [far_plan, far_components] = run_fixture(60.0f, 712);
    require(far_plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
            "far comparison fixture did not enter BodyLock");
    require(far_plan.normalized_size <= 0.13f,
            "far comparison fixture accidentally entered near-target scope");
    require(far_components.shaped_assist_stick.x > 0.05f,
            "far comparison fixture did not generate material AI");
    require(far_components.intent_fusion_manual_weight >= 0.999f,
            "far BodyLock was changed by the near-target ownership policy");
    require(far_components.intent_fusion_candidate != static_cast<int>(
                controller_native::FusionCandidate::RadialCorrected),
            "far BodyLock incorrectly entered near-target proposal ownership");
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

void test_held_ads_target_change_does_not_rearm_snap() {
    double now = 60.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 101));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "close opening target must settle into BodyLock");

    now += 0.060;
    controller.submit_vision_snapshot(target(2, now, 60.0f, 0.0f, 202));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "new target while ADS is held must not rearm ADS snap");

    now += 0.005;
    physical.left_trigger = 0.72f;
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "a partial LT axis drop must not create a false release");

    now += 0.001;
    physical.left_trigger = 0.0f;
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "a one-tick LT zero must be treated as input dropout");

    for (unsigned int sample = 1;
         sample <
             controller_native::kAimLeftTriggerIdleDebounceSamples;
         ++sample) {
        now += 0.001;
        controller.build_output(physical);
    }
    require(controller.last_ai_aim_mode() == "manual",
            "a sustained LT release must return to manual mode");

    now += 0.001;
    physical.left_trigger = 1.0f;
    controller.submit_vision_snapshot(target(3, now, 60.0f, 0.0f, 202));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "ads_snap",
            "a deliberate debounced release must rearm snap immediately");
}

void test_same_track_geometry_shift_keeps_final_output_bounded() {
    double now = 62.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 301));
    auto output = controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "same-track jump fixture must first settle into BodyLock");
    const auto target_id = controller.last_target_plan().target_id;

    for (std::uint64_t frame = 2; frame <= 3; ++frame) {
        now += 0.010;
        const float dx = frame == 2 ? 12.0f : 24.0f;
        controller.submit_vision_snapshot(target(frame, now, dx, 0.0f, 301));
        output = controller.build_output(physical);
    }
    require(std::fabs(output.right_x) > 0.03f,
            "same-track jump fixture must establish material BodyLock output");
    const auto before_plan = controller.last_target_plan();
    const auto before_output = output;

    now += 0.010;
    controller.submit_vision_snapshot(target(4, now, 94.0f, 0.0f, 301));
    output = controller.build_output(physical);
    const auto after_plan = controller.last_target_plan();
    const float output_delta = std::hypot(
        output.right_x - before_output.right_x,
        output.right_y - before_output.right_y);

    require(after_plan.target_id == target_id,
            "a body-geometry shift must not invent a new canonical target");
    require(after_plan.error_px.x - before_plan.error_px.x >= 60.0f,
            "fixture must deliver the approximately 70px fresh Vision shift");
    require(controller.last_ai_aim_mode() == "body_lock",
            "same-track geometry must remain a BodyLock observation");
    require(output_delta <= 0.0805f,
            "same-track geometry shift bypassed the final vector slew envelope");
}

void test_held_ads_new_track_after_gap_starts_from_zero_ai() {
    double now = 64.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, -2.0f, 0.0f, 401));
    auto output = controller.build_output(physical);
    for (std::uint64_t frame = 2; frame <= 5; ++frame) {
        now += 0.010;
        controller.submit_vision_snapshot(target(frame, now, -45.0f, 0.0f, 401));
        output = controller.build_output(physical);
    }
    require(output.right_x < -0.05f,
            "new-track fixture must establish old-target BodyLock ownership");
    const auto old_target_id = controller.last_target_plan().target_id;

    now += 0.010;
    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 6;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);
    (void)controller.build_output(physical);

    // Expire the bounded identity hold without releasing LT. The next selected
    // person is 95px from the last observed point (-45 -> +50).
    now += 0.190;
    output = controller.build_output(physical);
    require(controller.last_target_plan().target_id == 0,
            "identity hold must expire before the replacement target arrives");
    require(std::hypot(output.right_x, output.right_y) <= 0.001f,
            "expired target must release to the manual/zero-AI baseline");

    now += 0.001;
    controller.submit_vision_snapshot(target(7, now, 50.0f, 0.0f, 402));
    output = controller.build_output(physical);
    const auto replacement_plan = controller.last_target_plan();
    const auto first_components = controller.last_output_components();
    require(replacement_plan.target_id != 0 &&
                replacement_plan.target_id != old_target_id,
            "replacement observation must receive a new canonical target id");
    require(controller.last_ai_aim_mode() == "body_lock",
            "held LT must not rearm ADS snap for the replacement target");
    require(std::fabs(first_components.requested_assist_stick.x) > 0.05f,
            "fixture must generate a material fresh-target BodyLock request");
    require(std::hypot(output.right_x, output.right_y) <= 0.001f,
            "replacement target must spend its first tick at manual/zero AI; output=" +
                std::to_string(output.right_x) + "," +
                std::to_string(output.right_y) +
                " fallback=" +
                std::to_string(first_components.intent_fusion_fallback));
    require(first_components.intent_fusion_fallback &&
                first_components.intent_fusion_ai_weight <= 0.001f,
            "replacement target must pass through the target-change admission gate");

    now += 0.001;
    output = controller.build_output(physical);
    require(output.right_x > 0.001f && output.right_x <= 0.0805f,
            "replacement BodyLock must re-enter gradually after the zero-AI tick");
}

void test_close_lateral_runner_keeps_fresh_bodylock_authority() {
    double now = 66.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 501));
    auto output = controller.build_output(physical);
    const auto target_id = controller.last_target_plan().target_id;
    float previous_output_x = output.right_x;

    for (std::uint64_t frame = 2; frame <= 6; ++frame) {
        now += 0.010;
        const float dx = 2.0f + 16.0f * static_cast<float>(frame - 1);
        controller.submit_vision_snapshot(target(frame, now, dx, 0.0f, 501));
        output = controller.build_output(physical);
        const auto plan = controller.last_target_plan();
        const auto components = controller.last_output_components();
        require(plan.target_id == target_id,
                "legitimate lateral runner must retain canonical ownership");
        require(controller.last_ai_aim_mode() == "body_lock",
                "legitimate lateral runner must remain in BodyLock");
        require(plan.error_px.x >= dx - 1.0f,
                "fresh runner position must remain authoritative");
        require(!components.intent_fusion_fallback,
                "ordinary same-track motion must not trip target admission");
        require(output.right_x + 0.001f >= previous_output_x,
                "ordinary lateral tracking must not become lazy or reverse");
        previous_output_x = output.right_x;
    }
    require(output.right_x >= 0.20f,
            "ordinary close lateral motion must retain material follow authority");
}

void test_same_target_reacquire_preserves_pipeline_continuity() {
    double now = 67.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 8; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 80.0f, 0.0f, 601));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    require(
        std::fabs(controller.last_output_components().shaped_assist_stick.x) >
            0.20f,
        "reacquire fixture must establish material shaped AI");

    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 9;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);
    (void)controller.build_output(physical);
    require(
        controller.last_target_plan().lifecycle ==
            pipeline_contract::TargetLifecycle::Coasting,
        "empty fresh snapshot must enter the real Coasting lifecycle");

    now += 0.001;
    controller.submit_vision_snapshot(
        target(10, now, 80.0f, 0.0f, 601));
    const auto output = controller.build_output(physical);
    const auto& components = controller.last_output_components();
    require(
        controller.last_target_plan().lifecycle ==
            pipeline_contract::TargetLifecycle::Reacquiring,
        "same source after an empty miss must enter Reacquiring");
    require(
        std::fabs(components.requested_assist_stick.x) > 0.20f &&
            std::fabs(components.shaped_assist_stick.x) > 0.20f,
        "reacquire pipeline must retain requested and shaped AI evidence");
    require(
        !components.intent_fusion_fallback,
        "same-target Reacquiring must not take the fuser manual fallback");
    require(
        components.intent_fusion_ai_weight > 0.99f,
        "same-target Reacquiring must retain the fused AI proposal");
    require(
        std::fabs(components.post_ai_stick.x) >=
            std::fabs(components.shaped_assist_stick.x) - 0.081f,
        "same-target Reacquiring must not unload then reassert hidden AI");
    require(
        std::fabs(components.post_ai_stick.x - output.right_x) < 0.001f &&
            std::fabs(components.before_recoil_stick.x - output.right_x) <
                0.001f,
        "pipeline fixture must expose fused/post-output before recoil");
}

void test_cover_retreat_retires_pipeline_actuation_without_identity_loss() {
    double now = 69.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.target_max_age_ms = 180.0f;
    controller_config.ai_aim.target_projection_max_age_ms = 180.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 8; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 80.0f, 0.0f, 701));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    const auto target_id = controller.last_target_plan().target_id;
    const auto initial_plan = controller.last_target_plan();
    const auto initial_components = controller.last_output_components();
    require(
        controller.last_target_plan().aim_authority > 0.70f &&
            std::fabs(initial_components.requested_assist_stick.x) > 0.20f &&
            std::fabs(initial_components.shaped_assist_stick.x) > 0.20f,
        "cover-retreat pipeline must establish material AI before the miss");

    now += 0.0025;
    controller.submit_vision_snapshot(empty_fresh_snapshot(9, now));
    (void)controller.build_output(physical);
    const auto grace_plan = controller.last_target_plan();
    const auto grace_components = controller.last_output_components();
    require(
        grace_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "fresh empty frame must enter the real Coasting pipeline");
    require(
        grace_plan.target_id == target_id && grace_plan.aim_authority > 0.70f,
        "12.5ms miss must preserve identity and near-full actuation");
    require(
        std::fabs(grace_components.shaped_assist_stick.x) <=
            std::fabs(initial_components.shaped_assist_stick.x) + 0.001f,
        "Coasting shaper must not blind-rise during the grace frame");

    now += 0.0125;
    controller.submit_vision_snapshot(empty_fresh_snapshot(10, now));
    (void)controller.build_output(physical);
    now += 0.0250;
    controller.submit_vision_snapshot(empty_fresh_snapshot(11, now));
    (void)controller.build_output(physical);
    now += 0.0160;
    controller.submit_vision_snapshot(empty_fresh_snapshot(12, now));
    const auto output = controller.build_output(physical);
    const auto release_plan = controller.last_target_plan();
    const auto release_components = controller.last_output_components();
    require(
        release_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "actuation release must not retire the identity before hold_ms");
    require(
        release_plan.target_id == target_id,
        "actuation release must preserve canonical identity");
    const float expected_reliability = initial_plan.reliability *
        std::clamp(1.0f - release_plan.observation_age_ms / 180.0f,
                   0.0f, 1.0f);
    require(
        std::fabs(release_plan.reliability - expected_reliability) < 0.02f,
        "actuation release must preserve the independent 180ms reliability lease: initial=" +
            std::to_string(initial_plan.reliability) +
            " final=" + std::to_string(release_plan.reliability) +
            " age=" + std::to_string(release_plan.observation_age_ms) +
            " expected=" + std::to_string(expected_reliability));
    require(
        release_plan.aim_authority < 0.05f,
        "Coasting actuation must be retired by the 65ms candidate release");
    require(
        std::fabs(release_components.requested_assist_stick.x) <=
            std::fabs(grace_components.requested_assist_stick.x) + 0.001f &&
            std::fabs(release_components.shaped_assist_stick.x) <=
            std::fabs(grace_components.shaped_assist_stick.x) + 0.001f,
        "retiring Coasting authority must not leave a growing AI request");
    require(
        release_components.intent_fusion_ai_weight <=
            grace_components.intent_fusion_ai_weight + 0.001f &&
            std::fabs(release_components.post_ai_stick.x) <=
            std::fabs(grace_components.post_ai_stick.x) + 0.081f,
        "fused/post-output must follow the retiring Coasting proposal");
    require(
        std::fabs(output.right_x) <=
            std::fabs(grace_components.post_ai_stick.x) + 0.081f,
        "final output must not preserve a material old-direction pull after release");
}

void test_36ms_moving_occlusion_preserves_pipeline_tracking() {
    double now = 70.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 6; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 20.0f + 4.0f * static_cast<float>(frame_id - 1),
                   0.0f, 702));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    const auto target_id = controller.last_target_plan().target_id;

    // The sixth observation is at 70.050. Submit a fresh empty frame at
    // 70.086 so the occlusion itself is 36ms and frame ids advance.
    now -= 0.004;
    controller.submit_vision_snapshot(empty_fresh_snapshot(7, now));
    const auto occlusion_output = controller.build_output(physical);
    const auto occlusion_plan = controller.last_target_plan();
    require(
        occlusion_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting &&
            occlusion_plan.target_id == target_id,
        "36ms moving occlusion must remain on the same Coasting owner");
    require(
        occlusion_plan.mode != pipeline_contract::ControlMode::Manual,
        "36ms moving occlusion must not false-stop the assisted mode");

    now += 0.001;
    controller.submit_vision_snapshot(target(8, now, 44.0f, 0.0f, 702));
    const auto output = controller.build_output(physical);
    const auto reacquired_plan = controller.last_target_plan();
    const auto components = controller.last_output_components();
    require(
        reacquired_plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring &&
            reacquired_plan.target_id == target_id,
        "36ms moving occlusion must reacquire the same canonical target");
    require(
        std::fabs(reacquired_plan.error_px.x - 44.0f) < 1.0f,
        "fresh moving-target position must remain authoritative after occlusion");
    require(
        !components.intent_fusion_fallback &&
            std::fabs(components.requested_assist_stick.x) > 0.01f &&
            std::fabs(components.shaped_assist_stick.x) > 0.01f &&
            std::fabs(components.post_ai_stick.x) > 0.01f &&
            std::fabs(output.right_x) > 0.01f,
        "36ms same-target reacquire must not false-stop or unload the pipeline");
    require(
        output.right_x * occlusion_output.right_x >= -0.001f,
        "36ms moving occlusion must not create a material direction reversal");
}

void test_firing_fresh_cross_center_reverses_bodylock_request() {
    double now = 68.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_trigger = 1.0f;

    const float observations[] = {2.0f, 11.0f, -8.0f, -16.0f, -28.0f};
    for (std::uint64_t index = 0; index < 5; ++index) {
        controller.submit_vision_snapshot(
            target(index + 1, now, observations[index], 0.0f, 602));
        (void)controller.build_output(physical);
        now += 0.010;
    }

    require(
        controller.last_target_plan().error_px.x < 0.0f,
        "fresh firing observation must cross the coordinator error center");
    require(
        controller.last_output_components().requested_assist_stick.x < 0.0f,
        "BodyLock request must follow the fresh firing position across center");
}

void test_bodylock_countersteer_has_no_escape_threshold_impulse() {
    double now = 31.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 40.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    auto physical = aiming();

    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        controller.submit_vision_snapshot(
            target(frame, now, 24.0f, 0.0f));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    require(controller.last_ai_aim_mode() == "body_lock",
            "precondition: countersteer fixture did not enter BodyLock");

    const float manual_sequence[] = {
        -0.30f, -0.36f, -0.42f, -0.46f, -0.42f, -0.36f, -0.30f};
    float previous_output = controller.build_output(physical).right_x;
    float maximum_threshold_jump = 0.0f;
    float previous_effective_ai = 2.0f;
    for (std::size_t index = 0;
         index < sizeof(manual_sequence) / sizeof(manual_sequence[0]);
         ++index) {
        now += 0.001;
        physical.right_x = manual_sequence[index];
        controller.submit_vision_snapshot(target(
            5 + static_cast<std::uint64_t>(index), now, 24.0f, 0.0f));
        const float output = controller.build_output(physical).right_x;
        if (index >= 3) {
            maximum_threshold_jump = std::max(
                maximum_threshold_jump,
                std::fabs(output - previous_output));
        }
        const float effective_ai = output - physical.right_x;
        if (index <= 3) {
            require(
                effective_ai <= previous_effective_ai + 0.025f,
                "AI authority reasserted while opposing manual commitment increased");
            previous_effective_ai = effective_ai;
        }
        previous_output = output;
    }
    require(maximum_threshold_jump <= 0.12f,
            "crossing the manual-escape threshold created a BodyLock output impulse");
}

void test_bodylock_capture_alignment_has_no_remaining_authority() {
    double now = 17.0;
    auto controller_config = config();
    controller_config.tracker.remaining_work_enabled = true;
    controller_config.tracker.remaining_work_scale = 0.60f;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f));
    (void)controller.build_output(physical);
    require(
        controller.last_ai_aim_mode() == "body_lock",
        "precondition: close target did not settle into BodyLock");

    controller.report_output_delivery(true, true, now + 0.000001);
    now += 0.010;
    controller.submit_vision_snapshot(target(2, now - 0.006, 2.0f, 0.0f));
    (void)controller.build_output(physical);
    require(
        !controller.last_target_plan().remaining_work_valid,
        "fresh delayed Vision may align coordinates but must not re-arm Remaining");

    now += 0.001;
    (void)controller.build_output(physical);
    require(
        !controller.last_target_plan().remaining_work_valid,
        "BodyLock Remaining authority must stay off between Vision frames");
}

void test_physical_fire_is_never_cleared_by_autofire() {
    double now = 70.0;
    auto controller_config = config();
    controller_config.auto_fire.require_aim_ready = false;
    controller_config.auto_fire.max_source_age_ms = 1000.0f;
    controller_config.auto_fire.pulse_width_ms = 30.0f;
    controller_config.auto_fire.pulse_period_ms = 100.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    auto physical = aiming();
    auto snapshot = target(1, now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    controller.submit_vision_snapshot(snapshot);
    auto output = controller.build_output(physical);
    require(output.rb, "precondition: synthetic RB pulse did not start");

    now += 0.001;
    physical.rb = true;
    output = controller.build_output(physical);
    require(output.rb, "physical RB was cleared during synthetic takeover");

    now += 0.001;
    physical.rb = false;
    controller.build_output(physical);
    now += 0.001;
    physical.right_trigger = 1.0f;
    output = controller.build_output(physical);
    require(output.right_trigger >= 0.999f,
            "physical RT was cleared during synthetic takeover guard");

    double wait_now = 80.0;
    NativeGamepadController wait_controller(
        controller_config, [&wait_now] { return wait_now; });
    physical = aiming();
    snapshot = target(1, wait_now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    wait_controller.submit_vision_snapshot(snapshot);
    require(wait_controller.build_output(physical).rb,
            "precondition: cadence-wait pulse did not start");
    wait_now += 0.031;
    wait_controller.build_output(physical);
    wait_now += 0.001;
    physical.rb = true;
    output = wait_controller.build_output(physical);
    require(output.rb, "physical RB was cleared during cadence wait");
}

void test_100hz_vision_1000hz_control_emits_stable_fire_cadence() {
    double now = 90.0;
    auto controller_config = config();
    controller_config.auto_fire.require_aim_ready = false;
    controller_config.auto_fire.max_source_age_ms = 50.0f;
    controller_config.auto_fire.pulse_width_ms = 30.0f;
    controller_config.auto_fire.pulse_period_ms = 100.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming();

    bool previous = false;
    int current_width = 0;
    int minimum_width = 1000;
    std::vector<int> start_ticks;
    for (int tick = 0; tick < 1000; ++tick) {
        now = 90.0 + tick * 0.001;
        if (tick % 10 == 0) {
            controller.submit_vision_snapshot(
                fire_target(static_cast<std::uint64_t>(tick / 10 + 1), now));
        }
        const bool pressed = controller.build_output(physical).rb;
        if (tick % 10 != 0) {
            require(!controller.last_frame_vision_state().fresh_observation,
                    "no-publication tick was mislabeled as a fresh Vision frame");
            require(
                controller.last_frame_vision_state().current_observed_target_present,
                "no-publication tick lost current observed target continuity");
        }
        if (pressed && !previous) start_ticks.push_back(tick);
        if (pressed) ++current_width;
        if (!pressed && previous) {
            minimum_width = std::min(minimum_width, current_width);
            current_width = 0;
        }
        previous = pressed;
    }
    require(start_ticks.size() == 10,
            "100Hz/1000Hz chain must emit exactly ten pulse starts");
    require(minimum_width >= 30,
            "Vision publication gaps shortened a pulse below 30ms");
    for (std::size_t index = 1; index < start_ticks.size(); ++index) {
        require(std::abs(start_ticks[index] - start_ticks[index - 1] - 100) <= 1,
                "pulse period drifted beyond one controller tick");
    }

    double miss_now = 100.0;
    NativeGamepadController miss_controller(
        controller_config, [&miss_now] { return miss_now; });
    bool pressed_before_miss = false;
    for (int tick = 0; tick <= 515; ++tick) {
        miss_now = 100.0 + tick * 0.001;
        if (tick % 10 == 0) {
            miss_controller.submit_vision_snapshot(
                fire_target(static_cast<std::uint64_t>(tick / 10 + 1), miss_now));
        }
        if (tick == 515) {
            ControllerVisionSnapshot miss;
            miss.frame_updated = true;
            miss.selector_identity_protocol = true;
            miss.frame_id = 1000;
            miss.capture_time_seconds = miss_now;
            miss.ready_time_seconds = miss_now;
            miss.state.screen_center_x = 320.0f;
            miss.state.screen_center_y = 256.0f;
            miss_controller.submit_vision_snapshot(miss);
        }
        const bool pressed = miss_controller.build_output(physical).rb;
        if (tick == 514) pressed_before_miss = pressed;
        if (tick == 515) {
            require(pressed_before_miss, "precondition: miss did not interrupt an active pulse");
            require(!pressed, "fresh processed miss must revoke fire on the same tick");
        }
    }

    for (const auto* tier : {"cue_hold", "weak"}) {
        double tier_now = 110.0;
        NativeGamepadController tier_controller(
            controller_config, [&tier_now] { return tier_now; });
        auto snapshot = fire_target(1, tier_now);
        snapshot.state.target_tier = tier;
        snapshot.state.fire_authority = std::string(tier) != "weak";
        tier_controller.submit_vision_snapshot(snapshot);
        require(!tier_controller.build_output(physical).rb,
                "cue/weak target must remain aim-only");
    }
}

}  // namespace

int main() {
    try {
        test_ads_is_bounded_and_drift_is_ignored();
        test_production_remaining_work_consumes_only_confirmed_delivery();
        test_bodylock_capture_alignment_has_no_remaining_authority();
        test_vision_gap_uses_smooth_short_continuity();
        test_opposing_manual_intent_yields_without_braking_bodylock();
        test_bodylock_countersteer_has_no_escape_threshold_impulse();
        test_benchmark_mix_override_updates_delivered_feedback();
        test_benchmark_vector_fusion_is_one_reported_pipeline_stage();
        test_vector_mode_generates_ai_before_manual_arbitration();
        test_ads_strong_cooperative_mix_uses_ai_as_the_radial_proposal();
        test_near_bodylock_strong_mix_prefers_ai_but_far_stays_legacy();
        test_only_worsening_wrong_way_axis_stops_suppressing_assist();
        test_ads_and_bodylock_share_one_resolved_target_geometry();
        test_held_ads_target_change_does_not_rearm_snap();
        test_same_track_geometry_shift_keeps_final_output_bounded();
        test_held_ads_new_track_after_gap_starts_from_zero_ai();
        test_close_lateral_runner_keeps_fresh_bodylock_authority();
        test_firing_fresh_cross_center_reverses_bodylock_request();
        test_same_target_reacquire_preserves_pipeline_continuity();
        test_cover_retreat_retires_pipeline_actuation_without_identity_loss();
        test_36ms_moving_occlusion_preserves_pipeline_tracking();
        test_physical_fire_is_never_cleared_by_autofire();
        test_100hz_vision_1000hz_control_emits_stable_fire_cadence();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
