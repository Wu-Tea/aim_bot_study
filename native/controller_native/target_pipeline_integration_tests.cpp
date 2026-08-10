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

constexpr float kCenterX = 320.0f;
constexpr float kCenterY = 256.0f;
constexpr float kBodyHeight = 140.0f;
constexpr float kBodyWidth = 56.0f;
constexpr float kAimHeightRatio = 0.365f;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const std::string& message) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

GamepadRuntimeConfig current_config() {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.recoil.adaptive_feedback_enabled = false;
    config.recoil.profile_playback_enabled = false;
    config.recoil.selection_log_enabled = false;
    config.tracker.max_observation_age_ms = 50.0f;
    config.ai_aim.ads_activation_radius_px = 180.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_start_delay_ms = 0.0f;
    config.ai_aim.ads_start_ramp_ms = 0.0f;
    return config;
}

PhysicalGamepadState physical_input(
    float right_x = 0.0f,
    float right_y = 0.0f,
    bool aiming = true) {
    PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = aiming ? 1.0f : 0.0f;
    physical.right_x = right_x;
    physical.right_y = right_y;
    return physical;
}

pipeline_contract::VisionCandidateSnapshot candidate(
    std::uint64_t id,
    float dx,
    float dy) {
    pipeline_contract::VisionCandidateSnapshot value;
    value.id = id;
    value.valid = true;
    value.has_aim_point = true;
    value.aim_point_px = {kCenterX + dx, kCenterY + dy};
    value.body_box_px = {
        value.aim_point_px.x - kBodyWidth * 0.5f,
        value.aim_point_px.y - kBodyHeight * kAimHeightRatio,
        kBodyWidth,
        kBodyHeight,
    };
    value.confidence = 0.95f;
    value.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    return value;
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    std::uint64_t observation_id,
    std::uint64_t selector_generation,
    bool selector_changed = false,
    bool fire_requested = false) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selected_observation_id = observation_id;
    snapshot.selector_target_generation = selector_generation;
    snapshot.selector_target_changed = selector_changed;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.auto_fire_requested = fire_requested;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.target_x = kCenterX + dx;
    snapshot.state.target_y = kCenterY + dy;
    snapshot.state.screen_center_x = kCenterX;
    snapshot.state.screen_center_y = kCenterY;
    snapshot.state.has_body_box = true;
    snapshot.state.body_x1 = snapshot.state.target_x - kBodyWidth * 0.5f;
    snapshot.state.body_y1 = snapshot.state.target_y -
        kBodyHeight * kAimHeightRatio;
    snapshot.state.body_x2 = snapshot.state.body_x1 + kBodyWidth;
    snapshot.state.body_y2 = snapshot.state.body_y1 + kBodyHeight;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = now;
    snapshot.candidates.push_back(candidate(observation_id, dx, dy));
    return snapshot;
}

void add_alternative(
    ControllerVisionSnapshot& snapshot,
    std::uint64_t observation_id,
    float dx,
    float dy = 0.0f) {
    snapshot.candidates.push_back(candidate(observation_id, dx, dy));
}

ControllerVisionSnapshot empty_snapshot(std::uint64_t frame_id, double now) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.state.screen_center_x = kCenterX;
    snapshot.state.screen_center_y = kCenterY;
    return snapshot;
}

ControllerVisionSnapshot cue_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    std::uint64_t selector_generation) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selector_target_generation = selector_generation;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = false;
    snapshot.state.auto_fire_requested = false;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.target_x = kCenterX + dx;
    snapshot.state.target_y = kCenterY + dy;
    snapshot.state.screen_center_x = kCenterX;
    snapshot.state.screen_center_y = kCenterY;
    snapshot.state.has_body_box = true;
    snapshot.state.body_x1 = snapshot.state.target_x - kBodyWidth * 0.5f;
    snapshot.state.body_y1 = snapshot.state.target_y -
        kBodyHeight * kAimHeightRatio;
    snapshot.state.body_x2 = snapshot.state.body_x1 + kBodyWidth;
    snapshot.state.body_y2 = snapshot.state.body_y1 + kBodyHeight;
    snapshot.state.target_tier = "cue_hold";
    snapshot.state.observed_at_seconds = now;
    return snapshot;
}

void require_finite_unit_output(
    const controller_native::GamepadOutputState& output,
    const std::string& context) {
    require(std::isfinite(output.right_x) && std::isfinite(output.right_y),
            context + ": non-finite right stick");
    require(std::fabs(output.right_x) <= 1.0f + 1.0e-6f &&
                std::fabs(output.right_y) <= 1.0f + 1.0e-6f,
            context + ": right stick exceeded unit bounds");
}

void test_no_target_is_physical_passthrough() {
    double now = 10.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    const auto physical = physical_input(0.42f, -0.31f);
    const auto output = controller.build_output(physical);

    require_near(output.right_x, physical.right_x, 1.0e-6f,
                 "no target changed physical X");
    require_near(output.right_y, physical.right_y, 1.0e-6f,
                 "no target changed physical Y");
    require(controller.last_target_plan().target_id == 0,
            "no-target tick created target ownership");
    const auto& components = controller.last_output_components();
    require(components.assist_control_phase == "manual" &&
                components.manual_passthrough_x &&
                components.manual_passthrough_y,
            "no-target tick did not stay in manual passthrough");
}

void test_fresh_target_has_one_current_plan_and_one_output_owner() {
    double now = 20.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 64.0f, -18.0f, 101, 7));
    const auto output = controller.build_output(physical_input());
    const auto plan = controller.last_target_plan();
    const auto components = controller.last_output_components();

    require(plan.target_id != 0 &&
                plan.source_frame_id == 1 &&
                plan.source_observation_id == 101,
            "fresh selected observation did not become the current target plan");
    require(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
                plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                pipeline_contract::valid(plan),
            "fresh target produced an invalid current plan");
    require(std::hypot(
                components.requested_assist_stick.x,
                components.requested_assist_stick.y) > 1.0e-4f,
            "fresh target produced no solver request");
    require(components.assist_control_phase == "track",
            "ordinary acquisition created an extra control phase");
    require_near(
        components.final_stick.x,
        output.right_x,
        1.0e-6f,
        "diagnostics and delivered X disagree");
    require_near(
        components.final_stick.y,
        output.right_y,
        1.0e-6f,
        "diagnostics and delivered Y disagree");
    require_finite_unit_output(output, "fresh target");
}

void test_controller_tick_without_source_does_not_project_observation() {
    double now = 30.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 48.0f, 12.0f, 201, 11));
    (void)controller.build_output(physical_input());
    const auto source_plan = controller.last_target_plan();

    now += 0.001;
    const auto output = controller.build_output(physical_input());
    const auto replay_tick = controller.last_target_plan();

    require(replay_tick.target_id == source_plan.target_id &&
                replay_tick.source_frame_id == source_plan.source_frame_id,
            "controller-only tick changed source ownership");
    require(replay_tick.source_observation_id == 0,
            "controller-only tick fabricated a fresh observation id");
    require_near(replay_tick.aim_px.x, source_plan.aim_px.x, 1.0e-6f,
                 "controller-only tick projected target X");
    require_near(replay_tick.aim_px.y, source_plan.aim_px.y, 1.0e-6f,
                 "controller-only tick projected target Y");
    require_near(replay_tick.velocity_px_per_sec.x,
                 source_plan.velocity_px_per_sec.x, 1.0e-6f,
                 "controller-only tick updated source velocity");
    require(!replay_tick.source_decision_available,
            "controller-only tick fabricated a source decision");
    require_finite_unit_output(output, "controller-only tick");
}

void test_fresh_no_target_removes_authority_immediately() {
    double now = 40.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 52.0f, 0.0f, 301, 15));
    (void)controller.build_output(physical_input());
    require(controller.last_target_plan().target_id != 0,
            "fixture did not establish target ownership");

    now += 0.005;
    controller.submit_vision_snapshot(empty_snapshot(2, now));
    const auto physical = physical_input(-0.37f, 0.24f);
    const auto output = controller.build_output(physical);
    const auto& plan = controller.last_target_plan();

    require(plan.target_id == 0 && plan.aim_authority == 0.0f &&
                plan.mode == pipeline_contract::ControlMode::Manual,
            "fresh no-selection frame retained generic aim authority");
    require_near(output.right_x, physical.right_x, 1.0e-6f,
                 "fresh no-target did not restore physical X");
    require_near(output.right_y, physical.right_y, 1.0e-6f,
                 "fresh no-target did not restore physical Y");
}

void test_cue_continuation_is_same_generation_aim_only_evidence() {
    double now = 50.0;
    auto config = current_config();
    config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    NativeGamepadController controller(config, [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 42.0f, 0.0f, 401, 21));
    (void)controller.build_output(physical_input());
    const auto observed = controller.last_target_plan();

    now += 0.005;
    controller.submit_vision_snapshot(cue_snapshot(
        2, now, 25.0f, -12.0f, 21));
    const auto cue_output = controller.build_output(physical_input());
    const auto cue = controller.last_target_plan();

    require(cue.target_id == observed.target_id && cue.cue_continuation,
            "same-generation cue did not continue the owned target");
    require(cue.source_observation_id == 0 && cue.aim_authority > 0.0f &&
                cue.aim_authority <= 0.35f + 1.0e-5f,
            "cue continuation escaped its bounded aim-only authority");
    require(!cue.fire_authority && !cue.fire_requested && !cue_output.rb,
            "cue continuation acquired synthetic fire authority");

    double cue_only_now = 51.0;
    NativeGamepadController cue_only(config, [&cue_only_now] {
        return cue_only_now;
    });
    cue_only.submit_vision_snapshot(cue_snapshot(
        1, cue_only_now, 20.0f, 0.0f, 21));
    (void)cue_only.build_output(physical_input());
    require(cue_only.last_target_plan().target_id == 0,
            "cue acquired a target without prior person ownership");

    now += 0.005;
    controller.submit_vision_snapshot(cue_snapshot(
        3, now, 20.0f, 0.0f, 22));
    (void)controller.build_output(physical_input());
    require(controller.last_target_plan().target_id == 0,
            "wrong-generation cue retained the prior target");
}

void test_multi_target_flick_handover_releases_then_captures() {
    double now = 60.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    auto first = observed_snapshot(1, now, -52.0f, 0.0f, 501, 31);
    add_alternative(first, 502, 58.0f);
    controller.submit_vision_snapshot(first);
    (void)controller.build_output(physical_input());
    const auto old_target_id = controller.last_target_plan().target_id;
    require(old_target_id != 0 &&
                controller.last_target_plan().ads_candidate_count == 2,
            "handover fixture did not establish two-target ownership");

    now += 0.001;
    const auto seek_output = controller.build_output(
        physical_input(0.90f, 0.0f));
    const auto seek_components = controller.last_output_components();
    require(seek_components.assist_control_phase == "handover_seek" &&
                seek_components.handover_requested,
            "opposed two-target flick did not request handover");
    require_near(seek_output.right_x, 0.90f, 1.0e-5f,
                 "handover seek kept steering toward the old target");

    now += 0.005;
    auto replacement = observed_snapshot(
        2, now, 35.0f, 0.0f, 602, 32, true);
    add_alternative(replacement, 601, -48.0f);
    controller.submit_vision_snapshot(replacement);
    const auto capture_output = controller.build_output(
        physical_input(0.90f, 0.0f));
    const auto replacement_plan = controller.last_target_plan();
    const auto capture_components = controller.last_output_components();
    require(replacement_plan.target_id != 0 &&
                replacement_plan.target_id != old_target_id &&
                replacement_plan.selector_target_generation == 32,
            "next fresh selector generation did not replace the target");
    require(capture_components.assist_control_phase == "capture" &&
                capture_components.handover_braking,
            "confirmed replacement did not enter bounded capture braking");
    require(std::fabs(capture_output.right_x) < 0.50f,
            "capture continued the full physical flick");

    for (std::uint64_t frame = 3; frame <= 4; ++frame) {
        now += 0.005;
        auto centered = observed_snapshot(
            frame, now, 0.0f, 0.0f, 600 + frame, 32);
        add_alternative(centered, 700 + frame, -55.0f);
        controller.submit_vision_snapshot(centered);
        (void)controller.build_output(physical_input());
    }
    now += 0.001;
    const auto micro_output = controller.build_output(
        physical_input(0.12f, 0.0f));
    const auto& settled = controller.last_output_components();
    require(settled.assist_control_phase == "track" &&
                settled.manual_passthrough_x,
            "settled replacement did not restore idle-axis manual control");
    require_near(micro_output.right_x, 0.12f, 1.0e-4f,
                 "settled replacement swallowed micro manual X");
}

void test_autofire_requires_fresh_observed_authority_and_preserves_physical_fire() {
    double now = 70.0;
    auto config = current_config();
    config.ai_aim.auto_fire_ready_frames = 2;
    NativeGamepadController controller(config, [&now] { return now; });

    bool synthetic_fire_seen = false;
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame, now, 0.0f, 0.0f, 800 + frame, 41, false, true));
        const auto output = controller.build_output(physical_input());
        synthetic_fire_seen = synthetic_fire_seen || output.rb;
        now += 0.005;
    }
    require(synthetic_fire_seen,
            "fresh observed target never reached configured AutoFire readiness");

    controller.submit_vision_snapshot(cue_snapshot(
        4, now, 0.0f, 0.0f, 41));
    const auto cue_output = controller.build_output(physical_input());
    require(!cue_output.rb &&
                !controller.last_output_components().auto_fire_active,
            "aim-only cue continued synthetic fire");

    now += 0.001;
    auto manual_fire = physical_input();
    manual_fire.rb = true;
    const auto physical_fire_output = controller.build_output(manual_fire);
    require(physical_fire_output.rb,
            "AutoFire safety path cleared the physical fire button");
}

void test_current_chain_outputs_remain_finite_and_bounded() {
    double now = 80.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    for (std::uint64_t frame = 1; frame <= 200; ++frame) {
        const float phase = static_cast<float>(frame) * 0.13f;
        const float dx = std::sin(phase) * 120.0f;
        const float dy = std::cos(phase * 0.7f) * 70.0f;
        controller.submit_vision_snapshot(observed_snapshot(
            frame, now, dx, dy, 900 + frame, 51));
        const auto output = controller.build_output(physical_input(
            std::sin(phase * 0.4f) * 0.18f,
            std::cos(phase * 0.5f) * 0.18f));
        require_finite_unit_output(output, "bounded sequence");
        require(pipeline_contract::valid(controller.last_target_plan()),
                "bounded sequence emitted invalid plan data");
        now += 0.005;
    }
}

}  // namespace

int main() {
    try {
        test_no_target_is_physical_passthrough();
        test_fresh_target_has_one_current_plan_and_one_output_owner();
        test_controller_tick_without_source_does_not_project_observation();
        test_fresh_no_target_removes_authority_immediately();
        test_cue_continuation_is_same_generation_aim_only_evidence();
        test_multi_target_flick_handover_releases_then_captures();
        test_autofire_requires_fresh_observed_authority_and_preserves_physical_fire();
        test_current_chain_outputs_remain_finite_and_bounded();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] "
                  << error.what() << '\n';
        return 1;
    }
}
