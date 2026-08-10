#include "sustained_aimlab_simulator.h"

#include <cmath>
#include <limits>
#include <stdexcept>

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

}  // namespace

int main() {
    test_script_and_run_are_deterministic();
    test_source_cadence_has_unique_fresh_frames();
    test_player_motion_is_plant_input_not_controller_oracle();
    test_nonfinite_controller_output_is_rejected();
    return 0;
}
