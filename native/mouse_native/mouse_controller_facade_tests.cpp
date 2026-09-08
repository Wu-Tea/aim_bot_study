#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"

#include "common_native/authority_types.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

mouse_native::MouseResponseProfile profile() {
    mouse_native::MouseResponseProfile result{};
    result.px_per_count_x = 0.05f;
    result.px_per_count_y = 0.05f;
    result.counts_per_u_second_x = 10'000.0f;
    result.counts_per_u_second_y = 10'000.0f;
    result.confidence = 1.0f;
    result.generation = 1;
    result.calibrated = true;
    return result;
}

controller_native::ControllerVisionSnapshot target_snapshot(
    std::uint64_t frame_id,
    std::uint64_t generation,
    double now_seconds,
    float error_x,
    float error_y = 0.0f) {
    controller_native::ControllerVisionSnapshot snapshot{};
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.selected_observation_id = 77;
    snapshot.selector_target_generation = generation;
    snapshot.capture_time_seconds = now_seconds;
    snapshot.ready_time_seconds = now_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.has_target = true;
    snapshot.state.target_x = 320.0f + error_x;
    snapshot.state.target_y = 256.0f + error_y;
    snapshot.state.fresh_observation = true;

    pipeline_contract::VisionCandidateSnapshot candidate{};
    candidate.id = 77;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {320.0f + error_x, 256.0f + error_y};
    candidate.body_box_px = {
        candidate.aim_point_px.x - 24.0f,
        candidate.aim_point_px.y - 44.8f,
        48.0f,
        112.0f};
    candidate.confidence = 0.95f;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

controller_native::PhysicalGamepadState physical_from(
    const mouse_native::MouseControllerTickResult& result,
    bool right_button_down,
    bool left_button_down) {
    controller_native::PhysicalGamepadState physical{};
    physical.connected = true;
    physical.right_x = result.manual_input.x;
    physical.right_y = result.manual_input.y;
    physical.left_trigger = right_button_down ? 1.0f : 0.0f;
    physical.right_trigger = left_button_down ? 1.0f : 0.0f;
    return physical;
}

void compare_controller_state(
    const mouse_native::MouseControllerFacade& mouse,
    const controller_native::NativeGamepadController& direct,
    const controller_native::GamepadOutputState& direct_output,
    const mouse_native::MouseControllerTickResult& mouse_output) {
    require_true(mouse_output.controller_used && !mouse_output.transparent,
        "in-envelope calibrated mouse input must use the shared controller");
    require_near(mouse_output.final_u_x, direct_output.right_x, 1.0e-6f,
        "mouse and direct gamepad final X must be identical");
    require_near(mouse_output.final_u_y, direct_output.right_y, 1.0e-6f,
        "mouse and direct gamepad final Y must be identical");

    const auto& mouse_plan = mouse.controller().last_target_plan();
    const auto& direct_plan = direct.last_target_plan();
    require_true(mouse_plan.target_id == direct_plan.target_id,
        "mouse and direct target identity must match");
    require_true(mouse_plan.mode == direct_plan.mode,
        "mouse and direct target mode must match");
    require_true(mouse_plan.selector_target_generation ==
        direct_plan.selector_target_generation,
        "mouse and direct target generation must match");

    const auto& mouse_components = mouse.controller().last_output_components();
    const auto& direct_components = direct.last_output_components();
    require_near(
        mouse_components.requested_assist_stick.x,
        direct_components.requested_assist_stick.x,
        1.0e-6f,
        "mouse and direct requested assist X must match");
    require_near(
        mouse_components.shaped_assist_stick.x,
        direct_components.shaped_assist_stick.x,
        1.0e-6f,
        "mouse and direct shaped assist X must match");
    require_true(mouse.controller().last_ai_aim_mode() == direct.last_ai_aim_mode(),
        "mouse and direct assist mode labels must match");
}

void test_manual_round_trip_and_button_mapping() {
    mouse_native::MouseControllerFacade mouse;
    auto initial = mouse.tick({{}, profile(), 0.001, 1, false, false});
    require_true(initial.controller_used, "calibrated facade must initialize controller");

    const auto result = mouse.tick({{3, 2}, profile(), 0.002, 2, false, true});
    require_true(result.controller_used && !result.transparent,
        "bounded manual mouse input must use controller");
    require_true(result.actuation.dx == 3 && result.actuation.dy == 2,
        "no-target controller path must preserve source counts");
    require_true(mouse.controller().last_output_components().fire_button,
        "left mouse must map only to the controller's physical fire semantic");
}

void test_ads_and_bodylock_are_metamorphically_identical(bool use_cod_default = false) {
    const auto config = mouse_native::make_mouse_controller_config();
    double direct_clock = 0.0;
    controller_native::NativeGamepadController direct(config, &direct_clock);
    mouse_native::MouseControllerFacade mouse;
    const std::vector<float> errors{80.0f, 45.0f, 12.0f, 5.0f, 3.0f, 2.0f, 2.0f, 2.0f};
    bool saw_ads = false;
    bool saw_bodylock = false;
    const auto response = use_cod_default
        ? mouse_native::make_cod_default_profile({}, mouse_native::MouseAimMode::Ads)
        : profile();

    for (std::size_t index = 0; index < errors.size(); ++index) {
        const double now = 0.001 + static_cast<double>(index) * 0.010;
        const auto snapshot = target_snapshot(index + 1, 9, now, errors[index]);
        mouse.submit_vision_snapshot(snapshot);
        const auto mouse_output = mouse.tick(
            {{}, response, now, index + 1, true, false});

        direct_clock = now;
        const auto physical = physical_from(mouse_output, true, false);
        direct.begin_tick(physical, index + 1);
        direct.submit_vision_snapshot(snapshot);
        const auto direct_output = direct.build_output_from_sampled_input();
        compare_controller_state(mouse, direct, direct_output, mouse_output);
        saw_ads = saw_ads || direct.last_ai_aim_mode() == "ads_snap";
        saw_bodylock = saw_bodylock || direct.last_ai_aim_mode() == "body_lock";

        if (index == 0) {
            mouse_native::MouseCalibrationObservation calibration{};
            require_true(mouse.last_calibration_observation(&calibration),
                "fresh selected person must expose calibration observation");
            require_true(calibration.target_id == snapshot.selector_target_generation &&
                calibration.target_generation == snapshot.selector_target_generation &&
                calibration.frame_id == snapshot.frame_id &&
                calibration.person_x_px == snapshot.state.target_x,
                "calibration must reuse Vision's selected point and generation");
        }
    }
    require_true(saw_ads, "parity replay must exercise ADS");
    require_true(saw_bodylock, "parity replay must reach BodyLock");
}

void test_large_flick_and_missing_profile_are_exactly_transparent() {
    mouse_native::MouseControllerFacade mouse;
    mouse.tick({{}, profile(), 0.001, 1, false, false});
    auto result = mouse.tick({{20, -13}, profile(), 0.002, 2, false, false});
    require_true(!result.controller_used && result.transparent,
        "out-of-envelope flick must bypass controller output");
    require_true(result.actuation.dx == 20 && result.actuation.dy == -13,
        "large flick must preserve original counts without clamp");

    auto invalid = profile();
    invalid.calibrated = false;
    result = mouse.tick({{-7, 4}, invalid, 0.003, 3, true, false});
    require_true(!result.controller_used && result.transparent,
        "uncalibrated active mode must not grant controller output");
    require_true(result.actuation.dx == -7 && result.actuation.dy == 4,
        "uncalibrated mode must preserve physical movement");
}

void test_uncalibrated_start_can_still_observe_range_dummy() {
    mouse_native::MouseControllerFacade mouse;
    const auto snapshot = target_snapshot(1, 12, 0.100, 24.0f);
    mouse.submit_vision_snapshot(snapshot);

    mouse_native::MouseResponseProfile missing{};
    const auto output = mouse.tick({{3, -2}, missing, 0.100, 1, false, false});
    require_true(output.transparent, "startup without a profile must remain transparent");

    mouse_native::MouseCalibrationObservation observation{};
    require_true(
        mouse.last_calibration_observation(&observation),
        "calibration must see the range dummy before a profile exists");
    require_true(
        observation.target_generation == 12 && observation.person_x_px == 344.0f,
        "startup calibration observation must come directly from fresh Vision");
}

void test_mouse_config_disables_device_specific_outputs_only() {
    controller_native::GamepadRuntimeConfig source{};
    source.recoil.enabled = true;
    source.auto_fire.enabled = true;
    source.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    const auto result = mouse_native::make_mouse_controller_config(source);
    require_true(!result.recoil.enabled, "mouse facade must disable gamepad recoil output");
    require_true(result.auto_fire.enabled, "mouse facade must preserve shared AutoFire");
    require_true(!result.ai_aim.aim_response_learning_enabled,
        "mouse calibration must replace online gamepad response learning");
    require_true(result.aim_response_curve.algorithm ==
        controller_native::AimResponseCurveAlgorithm::Linear,
        "mouse actuator must use a linear response curve");
    require_true(result.auto_fire.manual_fire_activates_ai_aim ==
        source.auto_fire.manual_fire_activates_ai_aim,
        "mouse facade must preserve the existing manual-fire aim lifecycle");
}

void test_profile_switch_uses_controller_reset_tick_duration() {
    mouse_native::MouseControllerFacade switched;
    mouse_native::MouseControllerFacade fresh;
    switched.tick({{}, profile(), 0.001, 1, false, false});
    auto ads_profile = profile();
    ++ads_profile.generation;
    const auto snapshot = target_snapshot(2, 22, 0.021, 80.0f);
    switched.submit_vision_snapshot(snapshot);
    fresh.submit_vision_snapshot(snapshot);
    const auto after_switch = switched.tick({{}, ads_profile, 0.021, 2, true, false});
    const auto after_reset = fresh.tick({{}, ads_profile, 0.021, 2, true, false});
    require_true(after_switch.controller_used && after_reset.controller_used,
        "both paths must execute the shared controller");
    require_true(after_switch.actuation.dx == after_reset.actuation.dx &&
        after_switch.actuation.dy == after_reset.actuation.dy,
        "profile switch cannot stretch a 1ms controller command over the old tick interval");
}

void test_shared_autofire_reaches_mouse_output_and_yields_to_manual() {
    mouse_native::MouseControllerFacade mouse;
    controller_native::incident_fixture::TargetSpec target;
    target.observation_id = 77;
    target.selector_generation = 9;
    target.fire_authority = true;
    bool saw_down = false;
    bool saw_pulse_gap = false;
    const auto response = mouse_native::make_cod_default_profile({}, mouse_native::MouseAimMode::Ads);
    for (std::uint64_t frame = 1; frame <= 80; ++frame) {
        const double now = frame * 0.01;
        auto snapshot = controller_native::incident_fixture::observed_snapshot(target, frame, now, 0, 0);
        snapshot.state.auto_fire_requested = true;
        mouse.submit_vision_snapshot(snapshot);
        const auto result = mouse.tick({{}, response, now, frame, true, false});
        require_true(result.auto_fire_active == mouse.controller().last_output_components().auto_fire_active,
            "mouse must carry the shared controller's fire decision in the same tick");
        saw_down = saw_down || result.auto_fire_active;
        saw_pulse_gap = saw_pulse_gap || (saw_down && !result.auto_fire_active);
    }
    require_true(saw_down && saw_pulse_gap, "mouse output must expose shared AutoFire pulse and release edges");

    auto snapshot = controller_native::incident_fixture::observed_snapshot(target, 81, 0.81, 0, 0);
    snapshot.state.auto_fire_requested = true;
    mouse.submit_vision_snapshot(snapshot);
    const auto manual = mouse.tick({{}, response, 0.81, 81, true, true});
    require_true(!manual.auto_fire_active && mouse.controller().last_output_components().fire_button,
        "physical left fire must retain manual authority and suppress synthetic fire");
    const auto lost = controller_native::incident_fixture::empty_snapshot(target, 82, 0.82);
    mouse.submit_vision_snapshot(lost);
    require_true(!mouse.tick({{}, response, 0.82, 82, true, false}).auto_fire_active,
        "target loss must not leave a stale mouse fire command");
}

}  // namespace

int main() {
    try {
        test_shared_autofire_reaches_mouse_output_and_yields_to_manual();
        test_profile_switch_uses_controller_reset_tick_duration();
        test_manual_round_trip_and_button_mapping();
        test_ads_and_bodylock_are_metamorphically_identical();
        test_ads_and_bodylock_are_metamorphically_identical(true);
        test_large_flick_and_missing_profile_are_exactly_transparent();
        test_uncalibrated_start_can_still_observe_range_dummy();
        test_mouse_config_disables_device_specific_outputs_only();
    } catch (const std::exception& error) {
        std::cerr << "[NativeMouseControllerFacadeTests] FAIL: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "[NativeMouseControllerFacadeTests] PASS\n";
    return 0;
}
