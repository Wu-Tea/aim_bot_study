#include "mouse_native/mouse_controller_runtime_core.h"

#include "common_native/authority_types.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}

controller_native::ControllerVisionSnapshot dummy_snapshot(
    std::uint64_t frame,
    std::uint64_t generation,
    double seconds,
    float person_x) {
    controller_native::ControllerVisionSnapshot snapshot{};
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame;
    snapshot.selected_observation_id = 77;
    snapshot.selector_target_generation = generation;
    snapshot.capture_time_seconds = seconds;
    snapshot.ready_time_seconds = seconds;
    snapshot.state.has_target = true;
    snapshot.state.target_x = person_x;
    snapshot.state.target_y = 250.0f;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.fresh_observation = true;

    pipeline_contract::VisionCandidateSnapshot candidate{};
    candidate.id = 77;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {person_x, 250.0f};
    candidate.body_box_px = {person_x - 24.0f, 194.0f, 48.0f, 112.0f};
    candidate.confidence = 0.95f;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

void test_uncalibrated_ads_runs_with_cod_default() {
    mouse_native::MouseControllerRuntimeCore runtime;
    std::int64_t delivered_x = 0;
    bool saw_controller = false;
    for (std::uint64_t index = 0; index < 12; ++index) {
        const double now = 0.100 + static_cast<double>(index) * 0.010;
        runtime.submit_vision_snapshot(
            dummy_snapshot(index + 1, 70, now, 400.0f));
        const auto tick = runtime.tick({{}, now, index + 1, true, false});
        saw_controller = saw_controller || tick.controller.controller_used;
        delivered_x += tick.output.counts.dx;
    }
    require_true(saw_controller,
        "uncalibrated runtime must run the shared controller using the COD default");
    require_true(delivered_x > 0,
        "uncalibrated ADS must emit a correction toward the selected target");
    require_true(!mouse_native::valid(
        runtime.profile(mouse_native::MouseAimMode::Ads)),
        "provisional runtime scale must not masquerade as a measured profile");
}

void complete_calibration(
    mouse_native::MouseControllerRuntimeCore& runtime,
    bool ads,
    std::uint64_t generation,
    double start_seconds) {
    runtime.submit_vision_snapshot(
        dummy_snapshot(1, generation, start_seconds, 400.0f));
    const auto begin = runtime.begin_calibration(
        ads, static_cast<std::uint64_t>(start_seconds * 1.0e9));
    require_true(begin.started, "hotkey must start calibration from current dummy");
    require_true(
        begin.output.kind == mouse_native::MouseRuntimeOutputKind::CalibrationProbe &&
            begin.output.counts.dx == 40,
        "calibration must request one known +40 count probe");
    require_true(
        !runtime.acknowledge_calibration_output(true).failed,
        "delivered probe must advance calibration");

    runtime.submit_vision_snapshot(
        dummy_snapshot(2, generation, start_seconds + 0.010, 380.0f));
    const auto probe_observed = runtime.tick(
        {{}, start_seconds + 0.010, 1, ads, false});
    require_true(
        probe_observed.output.kind ==
                mouse_native::MouseRuntimeOutputKind::CalibrationReturn &&
            probe_observed.output.counts.dx == -40,
        "same dummy displacement must request the inverse return count");
    require_true(
        !runtime.acknowledge_calibration_output(true).failed,
        "delivered return must advance calibration");

    runtime.submit_vision_snapshot(
        dummy_snapshot(3, generation, start_seconds + 0.020, 400.5f));
    const auto returned = runtime.tick(
        {{}, start_seconds + 0.020, 2, ads, false});
    require_true(returned.calibration_completed, "dummy return must commit profile");
}

void test_hotkey_dummy_calibration_enables_manual_round_trip() {
    mouse_native::MouseControllerRuntimeCore runtime;
    complete_calibration(runtime, false, 9, 0.100);

    const auto& profile = runtime.profile(mouse_native::MouseAimMode::Hipfire);
    require_true(mouse_native::valid(profile), "hipfire slot must become valid in memory");
    require_near(profile.px_per_count_x, 0.5f, 1.0e-6f,
        "20px / 40 counts must calibrate 0.5 px/count");
    require_near(profile.counts_per_u_second_x, 1'000.0f, 1.0e-3f,
        "profile must map the existing 500 px/(u*s) controller response");

    const auto manual = runtime.tick({{1, -1}, 0.121, 3, false, false});
    require_true(!manual.controller.transparent, "calibrated manual input must use facade");
    require_true(
        manual.output.counts.dx == 1 && manual.output.counts.dy == -1,
        "manual/no-target path must preserve mouse counts exactly");
}

void test_hipfire_and_ads_are_independent_memory_slots() {
    mouse_native::MouseControllerRuntimeCore runtime;
    complete_calibration(runtime, false, 10, 0.100);
    const auto hipfire_generation =
        runtime.profile(mouse_native::MouseAimMode::Hipfire).generation;
    require_true(
        !mouse_native::valid(runtime.profile(mouse_native::MouseAimMode::Ads)),
        "hipfire calibration must not populate ADS slot");

    complete_calibration(runtime, true, 11, 0.200);
    require_true(
        mouse_native::valid(runtime.profile(mouse_native::MouseAimMode::Ads)),
        "held right button must populate ADS slot");
    require_true(
        runtime.profile(mouse_native::MouseAimMode::Hipfire).generation ==
            hipfire_generation,
        "ADS calibration must not replace hipfire profile");
}

void test_physical_motion_aborts_probe_and_is_not_lost() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 20, 0.100, 400.0f));
    const auto begin = runtime.begin_calibration(false, 100'000'000);
    require_true(begin.started, "calibration must start");
    runtime.acknowledge_calibration_output(true);
    runtime.submit_vision_snapshot(dummy_snapshot(2, 20, 0.110, 380.0f));

    const auto moved = runtime.tick({{3, -2}, 0.110, 1, false, false});
    require_true(
        moved.calibration_failure ==
            mouse_native::MouseCalibrationFailure::PhysicalMouseMoved,
        "physical movement must reject a contaminated dummy measurement");
    require_true(
        moved.output.counts.dx == 3 && moved.output.counts.dy == -2,
        "aborted calibration must preserve the user's physical movement");
    require_true(
        !mouse_native::valid(runtime.profile(mouse_native::MouseAimMode::Hipfire)),
        "failed calibration must not publish a profile");
}

void test_sensor_jitter_does_not_abort_calibration() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 21, 0.100, 400.0f));
    const auto begin = runtime.begin_calibration(false, 100'000'000);
    require_true(begin.started, "calibration must start");
    runtime.acknowledge_calibration_output(true);
    runtime.submit_vision_snapshot(dummy_snapshot(2, 21, 0.110, 380.0f));

    const auto probe = runtime.tick({{1, -1}, 0.110, 1, false, false});
    require_true(probe.output.counts.dx == -39 && probe.output.counts.dy == -1,
        "a calibration return report must include this tick's physical movement");
    require_true(
        probe.calibration_failure == mouse_native::MouseCalibrationFailure::None &&
            probe.output.kind == mouse_native::MouseRuntimeOutputKind::CalibrationReturn,
        "two counts of sensor jitter must not reject the probe");
    runtime.acknowledge_calibration_output(true);

    runtime.submit_vision_snapshot(dummy_snapshot(3, 21, 0.120, 400.0f));
    const auto returned = runtime.tick({{1, 0}, 0.120, 2, false, false});
    require_true(
        returned.calibration_completed &&
            mouse_native::valid(runtime.profile(mouse_native::MouseAimMode::Hipfire)),
        "small post-return sensor jitter must still publish the profile");
}

void test_failed_virtual_probe_never_publishes_profile() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 30, 0.100, 400.0f));
    require_true(runtime.begin_calibration(false, 100'000'000).started,
        "calibration must start");
    const auto failed = runtime.acknowledge_calibration_output(false);
    require_true(
        failed.failed &&
            failed.failure == mouse_native::MouseCalibrationFailure::OutputFailed,
        "failed virtual output must terminate calibration");
    require_true(
        !mouse_native::valid(runtime.profile(mouse_native::MouseAimMode::Hipfire)),
        "output failure must leave the slot uncalibrated");
}

void test_stale_origin_is_rejected_without_poisoning_next_hotkey() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 80, 0.100, 400.0f));
    const auto stale = runtime.begin_calibration(false, 1'000'000'000);
    require_true(!stale.started &&
        stale.failure == mouse_native::MouseCalibrationFailure::InvalidObservation,
        "old Vision baseline must reject before emitting a calibration probe");

    runtime.submit_vision_snapshot(dummy_snapshot(2, 80, 1.010, 400.0f));
    const auto fresh = runtime.begin_calibration(false, 1'010'000'000);
    require_true(fresh.started && fresh.output.counts.dx == 40,
        "a fresh frame after stale rejection must start immediately");
}

void test_lost_target_cannot_reuse_last_calibration_origin() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 90, 0.100, 400.0f));
    auto lost = dummy_snapshot(2, 90, 0.110, 400.0f);
    lost.state.has_target = false;
    lost.selected_observation_id = 0;
    runtime.submit_vision_snapshot(lost);
    require_true(!runtime.begin_calibration(false, 110'000'000).started,
        "target loss must revoke the cached calibration origin immediately");
}

void test_scope_change_cancels_calibration() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 91, 0.100, 400.0f));
    require_true(runtime.begin_calibration(false, 100'000'000).started,
        "hipfire probe must start");
    runtime.acknowledge_calibration_output(true);
    runtime.submit_vision_snapshot(dummy_snapshot(2, 91, 0.110, 380.0f));
    const auto switched = runtime.tick({{}, 0.110, 1, true, false});
    require_true(!switched.calibration_active &&
        switched.calibration_failure != mouse_native::MouseCalibrationFailure::None,
        "RMB change must reject a measurement spanning two sensitivities");
}

void test_cod_default_units_and_dpi_are_not_double_applied() {
    mouse_native::MouseCodDefaultConfig settings;
    const auto hip = mouse_native::make_cod_default_profile(settings, mouse_native::MouseAimMode::Hipfire);
    require_true(mouse_native::valid(hip) && hip.estimated && !hip.calibrated,
        "COD default must be usable without claiming a measurement");
    require_near(static_cast<float>(mouse_native::cod_cm_per_360(settings)), 23.090909f, 1e-4f,
        "1200 DPI x COD 5 must give 23.09 cm/360");
    require_near(hip.px_per_count_x, 0.43198869f, 1e-6f,
        "1080p at FOV 104 must project 0.033 degrees/count near screen centre");
    settings.dpi = 2400;
    const auto double_dpi = mouse_native::make_cod_default_profile(settings, mouse_native::MouseAimMode::Hipfire);
    require_near(double_dpi.px_per_count_x, hip.px_per_count_x, 1e-6f,
        "DPI already determines raw count production and must not scale counts twice");
    require_near(static_cast<float>(mouse_native::cod_cm_per_360(settings)), 11.545455f, 1e-4f,
        "double DPI must halve physical turn distance");
    settings.ads_multiplier = 0.5f;
    const auto ads = mouse_native::make_cod_default_profile(settings, mouse_native::MouseAimMode::Ads);
    require_near(ads.px_per_count_x, hip.px_per_count_x * 0.5f, 1e-6f,
        "ADS multiplier must affect only its estimated response");
    settings.horizontal_fov_16_9 = 180;
    require_true(!mouse_native::valid(mouse_native::make_cod_default_profile(
        settings, mouse_native::MouseAimMode::Ads)), "invalid projection cannot authorize output");
}

void test_default_active_profiles_and_calibration_priority() {
    mouse_native::MouseControllerRuntimeCore runtime;
    const auto hip = runtime.effective_profile(mouse_native::MouseAimMode::Hipfire);
    const auto ads = runtime.effective_profile(mouse_native::MouseAimMode::Ads);
    require_true(hip.estimated && ads.estimated && hip.generation != ads.generation,
        "both default modes need distinct generations at startup");
    const auto manual = runtime.tick({{1, -1}, 0.001, 1, false, false});
    require_true(manual.controller.controller_used && manual.output.counts.dx == 1 &&
        manual.output.counts.dy == -1, "default manual mode must preserve physical movement");

    runtime.set_default_view_height(2160);
    require_near(runtime.effective_profile(mouse_native::MouseAimMode::Hipfire).px_per_count_x,
        hip.px_per_count_x * 2, 1e-6f, "full view height must scale projection, independent of ROI");
    require_true(runtime.effective_profile(mouse_native::MouseAimMode::Hipfire).generation != hip.generation,
        "changed projection must reset controller and actuator coordinates");

    complete_calibration(runtime, false, 10, 0.100);
    const auto calibrated = runtime.effective_profile(mouse_native::MouseAimMode::Hipfire);
    require_true(calibrated.calibrated && !calibrated.estimated,
        "measured hipfire must take priority over the default");
    require_near(calibrated.px_per_count_x, 0.5f, 1e-6f,
        "successful calibration must replace the estimate");
    require_true(runtime.effective_profile(mouse_native::MouseAimMode::Ads).estimated,
        "hipfire calibration must leave ADS running its own default");
    runtime.submit_vision_snapshot(dummy_snapshot(4, 10, 0.130, 400.0f));
    require_true(runtime.begin_calibration(false, 130'000'000).started, "recalibration must start");
    runtime.acknowledge_calibration_output(false);
    require_true(runtime.effective_profile(mouse_native::MouseAimMode::Hipfire).generation == calibrated.generation,
        "failed recalibration must preserve the previously working measurement");
}

void test_probe_temporarily_suspends_default_ai_and_failure_resumes_it() {
    mouse_native::MouseControllerRuntimeCore runtime;
    runtime.submit_vision_snapshot(dummy_snapshot(1, 44, 0.100, 400.0f));
    require_true(runtime.begin_calibration(true, 100'000'000).started, "ADS probe must start");
    runtime.acknowledge_calibration_output(true);
    const auto probe = runtime.tick({{}, 0.101, 1, true, false});
    require_true(!probe.controller.controller_used && probe.output.counts.dx == 0,
        "default AI cannot contaminate a calibration probe");
    runtime.cancel_calibration();
    runtime.submit_vision_snapshot(dummy_snapshot(2, 44, 0.110, 400.0f));
    require_true(runtime.tick({{}, 0.110, 2, true, false}).controller.controller_used,
        "cancelled calibration must resume default control");
}

}  // namespace

int main() {
    try {
        test_cod_default_units_and_dpi_are_not_double_applied();
        test_default_active_profiles_and_calibration_priority();
        test_probe_temporarily_suspends_default_ai_and_failure_resumes_it();
        test_lost_target_cannot_reuse_last_calibration_origin();
        test_scope_change_cancels_calibration();
        test_uncalibrated_ads_runs_with_cod_default();
        test_hotkey_dummy_calibration_enables_manual_round_trip();
        test_hipfire_and_ads_are_independent_memory_slots();
        test_physical_motion_aborts_probe_and_is_not_lost();
        test_sensor_jitter_does_not_abort_calibration();
        test_failed_virtual_probe_never_publishes_profile();
        test_stale_origin_is_rejected_without_poisoning_next_hotkey();
        std::cout << "[MouseControllerRuntimeCoreTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseControllerRuntimeCoreTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
