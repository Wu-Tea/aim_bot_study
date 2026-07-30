#include "native_gamepad_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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
        controller.build_output(physical);
        if (controller.last_output_components().ai_aim_stick.x > 0.02f) {
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
    require(components.intent_fusion_mode == "causal_vector",
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

    now += 0.010;
    physical.left_trigger = 0.0f;
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "manual",
            "ADS release must return to manual mode");

    now += 0.010;
    physical.left_trigger = 1.0f;
    controller.submit_vision_snapshot(target(3, now, 60.0f, 0.0f, 202));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "ads_snap",
            "a new physical ADS press must rearm ADS snap");
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
        test_vision_gap_uses_smooth_short_continuity();
        test_opposing_manual_intent_yields_without_braking_bodylock();
        test_benchmark_mix_override_updates_delivered_feedback();
        test_benchmark_vector_fusion_is_one_reported_pipeline_stage();
        test_vector_mode_generates_ai_before_manual_arbitration();
        test_only_worsening_wrong_way_axis_stops_suppressing_assist();
        test_ads_and_bodylock_share_one_resolved_target_geometry();
        test_held_ads_target_change_does_not_rearm_snap();
        test_physical_fire_is_never_cleared_by_autofire();
        test_100hz_vision_1000hz_control_emits_stable_fire_cadence();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
