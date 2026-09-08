#include "mouse_native/mouse_sensitivity_calibrator.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

mouse_native::MouseCalibrationObservation observation(
    std::uint64_t frame,
    std::uint64_t generation,
    std::uint64_t time_ns,
    float x) {
    mouse_native::MouseCalibrationObservation result{};
    result.frame_id = frame;
    result.target_id = 77;
    result.target_generation = generation;
    result.observed_at_ns = time_ns;
    result.person_x_px = x;
    result.person_y_px = 200.0f;
    result.fresh = true;
    return result;
}

mouse_native::MouseCalibrationUpdate run_successful_calibration(
    mouse_native::MouseSensitivityCalibrator& calibrator,
    mouse_native::MouseAimMode mode,
    std::uint64_t generation,
    std::uint64_t base_time) {
    auto update = calibrator.begin(
        mode, observation(1, generation, base_time, 300.0f));
    require_true(update.output_requested && update.output_counts.dx == 40,
        "begin must request one positive horizontal probe");
    update = calibrator.acknowledge_output(true);
    require_true(!update.failed, "delivered probe must be accepted");
    update = calibrator.observe(
        observation(2, generation, base_time + 20'000'000, 280.0f), false);
    require_true(update.output_requested && update.output_counts.dx == -40,
        "probe observation must request the exact reverse count");
    update = calibrator.acknowledge_output(true);
    require_true(!update.failed, "delivered return must be accepted");
    update = calibrator.observe(
        observation(3, generation, base_time + 40'000'000, 299.0f), false);
    require_true(update.completed && !update.failed,
        "same-target return near origin must complete calibration");
    return update;
}

void test_range_dummy_calibrates_memory_only_slots() {
    mouse_native::MouseSensitivityCalibrator calibrator;
    auto hip = run_successful_calibration(
        calibrator, mouse_native::MouseAimMode::Hipfire, 4, 1'000'000'000);
    require_true(mouse_native::valid(hip.profile), "completed hipfire profile must be valid");
    require_near(hip.profile.px_per_count_x, 0.5f, 1.0e-6f,
        "twenty pixels over forty counts must measure 0.5 px/count");
    require_near(hip.profile.counts_per_u_second_x, 1'000.0f, 1.0e-3f,
        "profile must normalize the controller to 500 px/u/s");
    require_true(mouse_native::valid(
        calibrator.profile(mouse_native::MouseAimMode::Hipfire)),
        "hipfire slot must be retained in process memory");
    require_true(!mouse_native::valid(
        calibrator.profile(mouse_native::MouseAimMode::Ads)),
        "ADS slot must remain independent until RMB calibration");

    const auto ads = run_successful_calibration(
        calibrator, mouse_native::MouseAimMode::Ads, 5, 2'000'000'000);
    require_true(mouse_native::valid(ads.profile), "ADS calibration must complete");
    require_true(
        calibrator.profile(mouse_native::MouseAimMode::Ads).generation !=
            calibrator.profile(mouse_native::MouseAimMode::Hipfire).generation,
        "hipfire and ADS slots must publish different generations");
}

void test_target_change_and_physical_move_reject_without_overwrite() {
    mouse_native::MouseSensitivityCalibrator calibrator;
    auto update = calibrator.begin(
        mouse_native::MouseAimMode::Hipfire,
        observation(1, 8, 1'000'000'000, 300.0f));
    require_true(update.output_requested, "probe must start");
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(2, 9, 1'020'000'000, 280.0f), false);
    require_true(update.failed &&
        update.failure == mouse_native::MouseCalibrationFailure::TargetChanged,
        "target-generation replacement must reject calibration");
    require_true(!mouse_native::valid(
        calibrator.profile(mouse_native::MouseAimMode::Hipfire)),
        "failed target calibration must not publish a profile");

    update = calibrator.begin(
        mouse_native::MouseAimMode::Hipfire,
        observation(3, 10, 2'000'000'000, 300.0f));
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(4, 10, 2'020'000'000, 280.0f), true);
    require_true(update.failed &&
        update.failure == mouse_native::MouseCalibrationFailure::PhysicalMouseMoved,
        "physical movement during probe must reject calibration");
}

void test_probe_polarity_return_and_timeout_are_bounded() {
    mouse_native::MouseSensitivityCalibrator calibrator;
    auto update = calibrator.begin(
        mouse_native::MouseAimMode::Ads,
        observation(1, 11, 1'000'000'000, 300.0f));
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(2, 11, 1'020'000'000, 320.0f), false);
    require_true(update.failed &&
        update.failure == mouse_native::MouseCalibrationFailure::WrongResponseDirection,
        "opposite response polarity cannot authorize a fixed-polarity actuator");

    update = calibrator.begin(
        mouse_native::MouseAimMode::Ads,
        observation(4, 12, 2'000'000'000, 300.0f));
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(5, 12, 2'020'000'000, 280.0f), false);
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(6, 12, 2'040'000'000, 290.0f), false);
    require_true(!update.failed && !update.completed,
        "return must allow time for the rendered camera to catch up");
    update = calibrator.check_timeout(2'750'000'001);
    require_true(update.failed &&
        update.failure == mouse_native::MouseCalibrationFailure::ReturnMissed,
        "reverse probe missing the origin tolerance must fail");

    update = calibrator.begin(
        mouse_native::MouseAimMode::Hipfire,
        observation(7, 13, 3'000'000'000, 300.0f),
        3'500'000'000);
    require_true(update.output_requested, "timeout case must start");
    update = calibrator.check_timeout(4'100'000'000);
    require_true(!update.failed,
        "timeout must start at the hotkey request, not the older Vision frame");
    update = calibrator.check_timeout(4'300'000'001);
    require_true(update.failed &&
        update.failure == mouse_native::MouseCalibrationFailure::TimedOut,
        "calibration must time out without a fresh result");
}

void test_stale_frame_is_ignored_not_remeasured() {
    mouse_native::MouseSensitivityCalibrator calibrator;
    auto update = calibrator.begin(
        mouse_native::MouseAimMode::Hipfire,
        observation(10, 20, 1'000'000'000, 300.0f));
    calibrator.acknowledge_output(true);
    update = calibrator.observe(
        observation(10, 20, 1'001'000'000, 280.0f), false);
    require_true(!update.failed && !update.output_requested && !update.completed,
        "the baseline frame repeated by a controller tick must be ignored");
    require_true(
        calibrator.state() ==
            mouse_native::MouseCalibrationState::AwaitingProbeObservation,
        "stale frame must not advance calibration state");
}

void test_inflight_capture_and_display_latency_do_not_calibrate() {
    mouse_native::MouseSensitivityCalibrator calibrator;
    calibrator.begin(mouse_native::MouseAimMode::Ads,
        observation(1, 30, 100'000'000, 300.0f), 150'000'000);
    calibrator.acknowledge_output(true, 155'000'000);
    auto update = calibrator.observe(observation(2, 30, 152'000'000, 280.0f), false);
    require_true(!update.failed && !update.output_requested,
        "a newer frame captured before the probe cannot measure its response");
    update = calibrator.observe(observation(3, 30, 160'000'000, 300.0f), false);
    require_true(!update.failed && !update.output_requested,
        "a rendered frame without the probe response must wait within the deadline");
    update = calibrator.observe(observation(4, 30, 180'000'000, 280.0f), false);
    require_true(update.output_requested && update.output_counts.dx == -40,
        "the first actual response must request the return");
    calibrator.acknowledge_output(true, 185'000'000);
    update = calibrator.observe(observation(5, 30, 182'000'000, 300.0f), false);
    require_true(!update.failed && !update.completed,
        "a return-looking frame captured before delivery must not authorize a profile");
    update = calibrator.observe(observation(6, 30, 190'000'000, 280.0f), false);
    require_true(!update.failed && !update.completed,
        "return output may not yet be visible in the next rendered frame");
    update = calibrator.observe(observation(7, 30, 210'000'000, 300.0f), false);
    require_true(update.completed, "visible return must finish calibration");
}

}  // namespace

int main() {
    try {
        test_inflight_capture_and_display_latency_do_not_calibrate();
        test_range_dummy_calibrates_memory_only_slots();
        test_target_change_and_physical_move_reject_without_overwrite();
        test_probe_polarity_return_and_timeout_are_bounded();
        test_stale_frame_is_ignored_not_remeasured();
    } catch (const std::exception& error) {
        std::cerr << "[NativeMouseSensitivityCalibratorTests] FAIL: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "[NativeMouseSensitivityCalibratorTests] PASS\n";
    return 0;
}
