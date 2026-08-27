#include "sustained_aimlab_simulator.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace controller_native::sustained_aimlab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ControllerStepResult neutral_bodylock(
    const ControllerObservation& input) {
    ControllerStepResult result;
    result.bodylock_mode = input.target_present;
    result.target_observed = input.target_present && input.fresh_vision;
    result.tracker_reliable = input.target_present;
    result.controller_target_id = input.target_present ? input.target_id : 0;
    result.predicted_terminal_error_px = input.observed_error_px;
    return result;
}

ControllerStepResult refuse_bodylock(
    const ControllerObservation& input) {
    ControllerStepResult result;
    result.target_observed = input.target_present && input.fresh_vision;
    result.tracker_reliable = input.target_present;
    result.controller_target_id = input.target_present ? input.target_id : 0;
    result.predicted_terminal_error_px = input.observed_error_px;
    return result;
}

void test_script_and_run_are_deterministic() {
    BenchmarkConfig config;
    config.duration_ms = 2'000;
    config.fixed_target_slot_ms = 400;
    config.vision_interval_ms = 6;
    const ScenarioScript first = generate_script(1337, config);
    const ScenarioScript second = generate_script(1337, config);
    require(first.hash == second.hash, "same seed must produce same script");

    const auto first_result = run_simulation(
        first,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow);
    const auto second_result = run_simulation(
        second,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow);
    require(first_result.ticks == config.duration_ms, "tick count mismatch");
    require(first_result.script_hash == second_result.script_hash,
            "run script hash must be deterministic");
    require(first_result.targets_spawned == second_result.targets_spawned,
            "target count must be deterministic");
}

void test_source_cadence_has_unique_fresh_frames() {
    BenchmarkConfig config;
    config.duration_ms = 1'500;
    config.fixed_target_slot_ms = 500;
    config.vision_interval_ms = 6;
    const ScenarioScript script = generate_script(7331, config);

    std::uint64_t previous_frame_id = 0;
    int fresh_count = 0;
    int nonfresh_count = 0;
    const ControllerStep controller =
        [&](const ControllerObservation& input) {
            if (input.fresh_vision) {
                require(input.frame_id > previous_frame_id,
                        "fresh frame ids must increase");
                previous_frame_id = input.frame_id;
                ++fresh_count;
            } else {
                ++nonfresh_count;
            }
            return neutral_bodylock(input);
        };
    run_simulation(
        script,
        ManualProfile::Pure,
        controller,
        BenchmarkCohort::BodyLockFollow);
    require(fresh_count > 0, "scenario must publish fresh frames");
    require(nonfresh_count > fresh_count,
            "1 kHz controller must include non-source ticks");
}

void test_controller_tick_holds_output_without_dropping_ready_vision() {
    BenchmarkConfig config;
    config.duration_ms = 100;
    config.fixed_target_slot_ms = 100;
    config.vision_interval_ms = 3;
    config.tick_ms = 4;
    const ScenarioScript script = generate_script(7332, config);

    int controller_calls = 0;
    int fresh_count = 0;
    const ControllerStep controller = [&](const ControllerObservation& input) {
        ++controller_calls;
        if (input.fresh_vision) ++fresh_count;
        ControllerStepResult result = neutral_bodylock(input);
        result.final_stick.x = 0.25;
        return result;
    };
    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        controller,
        BenchmarkCohort::BodyLockFollow);

    require(result.ticks == 100,
            "controller tick must not change the 1 ms plant/score clock");
    require(result.controller_updates == 25,
            "250 Hz controller must update exactly once per 4 ms");
    require(controller_calls == result.controller_updates,
            "reported controller updates must match actual callbacks");
    require(fresh_count >= 20,
            "Vision publications between controller ticks must be latched");
}

void test_eight_khz_controller_substeps_keep_one_khz_plant_admission() {
    BenchmarkConfig config;
    config.duration_ms = 4;
    config.fixed_target_slot_ms = 4;
    config.inter_target_gap_ms = 0;
    config.vision_interval_ms = 1;
    config.target_motion_enabled = false;
    config.controller_substeps_per_plant_tick = 8;
    const ScenarioScript script = generate_script(8000, config);

    std::vector<double> controller_times;
    int controller_calls = 0;
    int fresh_frames = 0;
    double maximum_plant_delta_px = 0.0;
    const ControllerStep controller = [&](const ControllerObservation& input) {
        controller_times.push_back(input.now_seconds);
        ++controller_calls;
        if (input.fresh_vision) ++fresh_frames;
        ControllerStepResult result = refuse_bodylock(input);
        // If the plant accidentally integrates all eight controller writes,
        // these first seven commands move the camera. The contract under test
        // admits only the final command at each 1 ms plant boundary.
        result.final_stick.x = controller_calls % 8 == 0 ? 0.0 : 1.0;
        return result;
    };

    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) {
            const Vec2d plant_delta{
                frame.true_error_after_px.x - frame.true_error_before_px.x,
                frame.true_error_after_px.y - frame.true_error_before_px.y,
            };
            maximum_plant_delta_px = std::max(
                maximum_plant_delta_px,
                length(plant_delta));
        });

    require(result.ticks == config.duration_ms,
            "8 kHz controller must not change the 1 kHz plant/scorer clock");
    require(result.controller_updates == config.duration_ms * 8,
            "8 kHz controller must execute eight substeps per plant tick");
    require(controller_calls == result.controller_updates,
            "reported 8 kHz updates must match controller callbacks");
    require(fresh_frames <= config.duration_ms,
            "one Vision publication must not be replayed across substeps");
    require(controller_times.size() >= 9 &&
                std::fabs(controller_times[1] - controller_times[0] - 0.000125) <
                    1.0e-12 &&
                std::fabs(controller_times[8] - controller_times[0] - 0.001) <
                    1.0e-12,
            "8 kHz controller timestamps must advance by 125 microseconds");
    require(maximum_plant_delta_px < 1.0e-12,
            "1 kHz plant must admit only the final controller output per tick");
}

void test_sensitivity_multiplier_changes_only_camera_plant_gain() {
    const auto first_step_camera_delta = [](double sensitivity_multiplier) {
        BenchmarkConfig config;
        config.duration_ms = 2;
        config.fixed_target_slot_ms = 2;
        config.target_motion_enabled = false;
        config.camera_response_px_per_stick_second = 400.0;
        config.sensitivity_multiplier = sensitivity_multiplier;
        const ScenarioScript script = generate_script(7333, config);

        double delta_x = 0.0;
        const ControllerStep controller = [](const ControllerObservation& input) {
            ControllerStepResult result = neutral_bodylock(input);
            result.final_stick.x = 0.25;
            return result;
        };
        run_simulation(
            script,
            ManualProfile::Pure,
            controller,
            BenchmarkCohort::BodyLockFollow,
            [&](const SimulationTraceFrame& frame) {
                if (frame.absolute_ms == 0) {
                    delta_x = frame.true_error_before_px.x -
                        frame.true_error_after_px.x;
                }
            });
        return delta_x;
    };

    const double baseline_delta = first_step_camera_delta(1.0);
    const double doubled_delta = first_step_camera_delta(2.0);
    require(std::fabs(doubled_delta - baseline_delta * 2.0) < 1e-9,
            "sensitivity multiplier must scale only the virtual camera plant");
}

void test_held_output_cannot_acquire_a_different_target() {
    BenchmarkConfig config;
    config.duration_ms = 170;
    config.fixed_target_slot_ms = 60;
    config.inter_target_gap_ms = 50;
    config.tick_ms = 200;
    const ScenarioScript script = generate_script(7334, config);

    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow);

    require(result.targets.size() >= 2,
            "slow-tick fixture must reach a second target");
    require(result.targets[0].bodylock_entry_ms == 0,
            "first target must enter BodyLock on the initial controller tick");
    require(result.targets[1].bodylock_entry_ms == -1,
            "held output for target one must not acquire target two");
    require(result.targets[1].bodylock_active_ms == 0,
            "stale target identity must not accrue BodyLock tracking time");
}

void test_player_motion_is_plant_input_not_controller_oracle() {
    BenchmarkConfig config;
    config.duration_ms = 5'000;
    config.fixed_target_slot_ms = 1'200;
    config.vision_interval_ms = 6;
    const ScenarioScript script = generate_script(20260810, config);
    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow,
        {},
        PlayerStrafeMode::FullReversal,
        PlayerVerticalMotionMode::Jump);
    require(result.left_strafe_active_ms > 0,
            "left-strafe plant motion must be exercised");
    require(result.left_strafe_reversals > 0,
            "left-strafe reversal must be exercised");
    require(result.player_jump_events > 0,
            "jump plant motion must be exercised");
    require(result.max_abs_player_speed_px_per_second > 0.0,
            "plant speed must be measured");
}

void test_nonfinite_controller_output_is_rejected() {
    BenchmarkConfig config;
    config.duration_ms = 100;
    const ScenarioScript script = generate_script(42, config);
    bool rejected = false;
    try {
        run_simulation(
            script,
            ManualProfile::Pure,
            [](const ControllerObservation&) {
                ControllerStepResult result;
                result.final_stick.x =
                    std::numeric_limits<double>::quiet_NaN();
                return result;
            });
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "non-finite controller output must fail the run");
}

void test_runtime_manual_segment_is_target_relative_and_not_synthetic() {
    BenchmarkConfig config;
    config.duration_ms = 300;
    config.fixed_target_slot_ms = 300;
    config.runtime_manual_segments = {
        RuntimeManualSegment{{
            RuntimeManualSample{100, 0.30, 0.0},
            RuntimeManualSample{100, -0.20, 0.10},
        }},
    };
    config.runtime_manual_segments.front().aim_mode =
        RuntimeManualAimMode::BodyLock;
    config.runtime_target_samples = {
        RuntimeTargetSample{{40.0, -30.0}, 40.0, 90.0},
    };
    const ScenarioScript script = generate_script(2026082502, config);

    bool ads_saw_manual = false;
    const ControllerStep ads_controller = [&](const ControllerObservation& input) {
        ads_saw_manual = ads_saw_manual || length(input.manual_stick) > 1.0e-9;
        return neutral_bodylock(input);
    };
    run_simulation(
        script,
        ManualProfile::RuntimeProfile,
        ads_controller,
        BenchmarkCohort::AdsAcquire);
    require(!ads_saw_manual,
            "BodyLock user habits must not leak into the ADS cohort");

    bool saw_helpful = false;
    bool saw_opposing_tangent = false;
    const ControllerStep controller = [&](const ControllerObservation& input) {
        if (input.target_present) {
            const Vec2d helpful_error{
                input.observed_error_px.x, -input.observed_error_px.y};
            const double magnitude = length(helpful_error);
            if (magnitude > 1e-9) {
                const Vec2d helpful{
                    helpful_error.x / magnitude, helpful_error.y / magnitude};
                const Vec2d tangent{-helpful.y, helpful.x};
                const double radial = input.manual_stick.x * helpful.x +
                    input.manual_stick.y * helpful.y;
                const double tangential = input.manual_stick.x * tangent.x +
                    input.manual_stick.y * tangent.y;
                saw_helpful = saw_helpful || radial > 0.29;
                saw_opposing_tangent = saw_opposing_tangent ||
                    (radial < -0.19 && tangential > 0.09);
            }
        }
        return neutral_bodylock(input);
    };
    run_simulation(
        script,
        ManualProfile::RuntimeProfile,
        controller,
        BenchmarkCohort::BodyLockFollow);
    require(saw_helpful,
            "runtime manual profile must replay a helpful radial sample");
    require(saw_opposing_tangent,
            "runtime manual profile must replay paired opposing/tangential input");
}

void test_runtime_body_geometry_reconstructs_logged_stable_error() {
    const auto exercise = [](double width, double height, double expected_ratio) {
        BenchmarkConfig config;
        config.duration_ms = 30;
        config.fixed_target_slot_ms = 30;
        config.vision_interval_ms = 5;
        config.body_aim_height_ratio = 0.30;
        config.runtime_target_samples = {
            RuntimeTargetSample{{25.0, -18.0}, width, height},
        };
        const ScenarioScript script = generate_script(2026082503, config);
        bool saw_body = false;
        const ControllerStep controller = [&](const ControllerObservation& input) {
            if (input.fresh_vision && input.has_body_box) {
                const double resolved_x =
                    input.body_box_x + input.body_box_width * 0.5 - 320.0;
                const double resolved_y =
                    input.body_box_y + input.body_box_height * expected_ratio -
                    256.0;
                require(std::fabs(resolved_x - input.observed_error_px.x) < 1e-9,
                        "runtime body box changed the stable x error");
                require(std::fabs(resolved_y - input.observed_error_px.y) < 1e-9,
                        "runtime body box changed the stable y error");
                saw_body = true;
            }
            return neutral_bodylock(input);
        };
        run_simulation(
            script,
            ManualProfile::Pure,
            controller,
            BenchmarkCohort::AdsAcquire);
        require(saw_body, "runtime target geometry was not delivered");
    };

    exercise(40.0, 100.0, 0.30);
    exercise(200.0, 100.0, 0.40);
}

void test_fixed_slots_make_target_count_outcome_independent() {
    BenchmarkConfig config;
    config.duration_ms = 600;
    config.fixed_target_slot_ms = 150;
    config.inter_target_gap_ms = 50;
    config.bodylock_entry_timeout_ms = 20;
    config.tracking_window_ms = 100;
    config.vision_interval_ms = 5;
    const ScenarioScript script = generate_script(2026082701, config);

    const auto successful = run_simulation(
        script,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow);
    const auto failed = run_simulation(
        script,
        ManualProfile::Pure,
        refuse_bodylock,
        BenchmarkCohort::BodyLockFollow);

    require(successful.targets_spawned == 3,
            "fixed schedule must expose exactly three target slots");
    require(failed.targets_spawned == successful.targets_spawned,
            "failed entry must not purchase additional target attempts");
    require(failed.bodylock_entry_failures == failed.targets_spawned,
            "every refused BodyLock slot must be an entry failure");
    require(failed.targets_acquired == 0,
            "BodyLock entry failure must not count as acquisition");
    require(failed.targets_missed == failed.targets_spawned,
            "BodyLock entry failure must count as a missed target");
    require(std::fabs(failed.acquire_points) < 1e-12,
            "BodyLock entry failure must earn no acquisition points");
    require(successful.targets_acquired == successful.targets_spawned,
            "successful BodyLock entry must acquire each scheduled target");
}

void test_fixed_slot_does_not_extend_tracking_score_window() {
    BenchmarkConfig config;
    config.duration_ms = 300;
    config.fixed_target_slot_ms = 300;
    config.inter_target_gap_ms = 0;
    config.tracking_window_ms = 100;
    config.vision_interval_ms = 5;
    const ScenarioScript script = generate_script(2026082702, config);

    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        neutral_bodylock,
        BenchmarkCohort::BodyLockFollow);

    require(result.targets_spawned == 1,
            "one fixed slot must produce one scored target");
    require(result.bodylock_active_ms == config.tracking_window_ms,
            "fixed slot idle must not extend the tracking score window");
}

void test_bodylock_setup_accepts_legal_ads_extension_horizon() {
    BenchmarkConfig config;
    config.duration_ms = config.fixed_target_slot_ms;
    config.inter_target_gap_ms = 0;
    config.vision_interval_ms = 5;
    config.target_motion_enabled = false;
    const ScenarioScript script = generate_script(2026082704, config);

    const ControllerStep delayed_bodylock = [](const ControllerObservation& input) {
        ControllerStepResult result = neutral_bodylock(input);
        result.bodylock_mode = input.target_present && input.now_ms >= 400;
        return result;
    };
    const auto result = run_simulation(
        script,
        ManualProfile::Pure,
        delayed_bodylock,
        BenchmarkCohort::BodyLockFollow);

    require(result.targets_spawned == 1,
            "legal-extension fixture must contain one fixed opportunity");
    require(result.targets_acquired == 1,
            "a 400ms BodyLock setup is inside the product execution horizon");
    require(result.bodylock_entry_failures == 0,
            "legal ADS extension must not be classified as entry failure");
    require(result.bodylock_active_ms == config.tracking_window_ms,
            "legal setup must retain the complete tracking score window");
}

}  // namespace

int main() {
    test_script_and_run_are_deterministic();
    test_source_cadence_has_unique_fresh_frames();
    test_controller_tick_holds_output_without_dropping_ready_vision();
    test_eight_khz_controller_substeps_keep_one_khz_plant_admission();
    test_sensitivity_multiplier_changes_only_camera_plant_gain();
    test_held_output_cannot_acquire_a_different_target();
    test_player_motion_is_plant_input_not_controller_oracle();
    test_nonfinite_controller_output_is_rejected();
    test_runtime_manual_segment_is_target_relative_and_not_synthetic();
    test_runtime_body_geometry_reconstructs_logged_stable_error();
    test_fixed_slots_make_target_count_outcome_independent();
    test_fixed_slot_does_not_extend_tracking_score_window();
    test_bodylock_setup_accepts_legal_ads_extension_horizon();
    return 0;
}
