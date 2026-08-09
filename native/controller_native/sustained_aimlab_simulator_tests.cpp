#include "sustained_aimlab_simulator.h"
#include "aim_response_curve_plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace controller_native::sustained_aimlab;
using controller_native::AimResponseCurveConfig;
using controller_native::AimResponseCurveAlgorithm;
using controller_native::CausalMotionLedger;
using controller_native::CausalMotionLedgerStatus;
using controller_native::CausalMotionPhaseRequest;
using controller_native::DeliveredFinalCommandSample;
using controller_native::forward_aim_response_curve;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ScenarioScript stationary_script(int duration_ms, Vec2d error,
                                 int deadline_ms = 300) {
    ScenarioScript script;
    script.seed = 7;
    script.hash = 7007;
    script.config.duration_ms = duration_ms;
    TargetScript target;
    target.id = 1;
    target.motion = MotionProfile::ConstantHorizontal;
    target.initial_error_px = error;
    target.acquire_deadline_ms = deadline_ms;
    for (int ms = 0; ms <= 1'400; ++ms) {
        target.observation_at_ms.push_back(ms);
        target.observation_noise_px.push_back({});
    }
    script.targets.push_back(target);
    return script;
}

struct KnownPlantCaseResult {
    CausalMemoryAccuracySummary accuracy;
    std::vector<SimulationTraceFrame> trace;
};

ScenarioScript causal_accuracy_script(
    int duration_ms,
    int plant_delay_ms,
    AimResponseCurveAlgorithm curve,
    BenchmarkCohort cohort,
    bool slowdown_enabled,
    Vec2d initial_error) {
    ScenarioScript script = stationary_script(
        duration_ms, initial_error, duration_ms + 100);
    script.config.control_response_delay_ms = plant_delay_ms;
    script.config.vision_result_delay_ms = 2;
    script.config.slowdown_edge_multiplier = slowdown_enabled ? 0.50 : 1.0;
    script.config.slowdown_center_multiplier = slowdown_enabled ? 0.40 : 1.0;
    script.config.camera_response_curve.algorithm = curve;
    script.config.target_motion_enabled = false;
    script.targets.front().visible_radius_px = cohort ==
        BenchmarkCohort::BodyLockFollow ? 24.0 : 24.0;
    script.targets.front().observation_at_ms.clear();
    script.targets.front().observation_noise_px.clear();
    for (int ms = 0; ms <= duration_ms; ms += 5) {
        script.targets.front().observation_at_ms.push_back(ms);
        script.targets.front().observation_noise_px.push_back({});
    }
    return script;
}

KnownPlantCaseResult run_known_plant_case(
    int plant_delay_ms,
    int ledger_delay_ms,
    AimResponseCurveAlgorithm curve,
    BenchmarkCohort cohort,
    bool slowdown_enabled,
    Vec2d initial_error = {60.0, -36.0}) {
    ScenarioScript script = causal_accuracy_script(
        1'200, plant_delay_ms, curve, cohort, slowdown_enabled,
        initial_error);
    CausalMotionLedger ledger;
    std::vector<SimulationTraceFrame> trace;
    double previous_capture_seconds = 0.0;
    ControllerStep controller = [&](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        output.controller_target_id = input.target_present ? 1 : 0;
        output.controller_ads_epoch = input.target_present ? 1 : 0;
        const double phase = static_cast<double>(input.now_ms) * 0.071;
        output.final_stick = {
            0.16 + 0.05 * std::sin(phase),
            -0.13 + 0.04 * std::cos(phase * 0.73)};
        output.requested_assist_stick = output.final_stick;
        output.shaped_assist_stick = output.final_stick;

        if (input.fresh_vision && input.capture_time_seconds > 0.0) {
            CausalMotionPhaseRequest request;
            request.previous_capture_seconds = previous_capture_seconds;
            request.current_capture_seconds = input.capture_time_seconds;
            request.decision_seconds = input.ready_time_seconds;
            request.response_delay_ms =
                static_cast<float>(ledger_delay_ms);
            request.memory_horizon_ms = 200.0f;
            request.target_id = 1;
            request.ads_epoch = 1;
            const auto estimate = ledger.estimate(request);
            output.causal_memory_status = estimate.status;
            output.causal_memory_valid = estimate.valid;
            output.causal_memory_realized_valid = estimate.realized_valid;
            output.causal_memory_realized_px = {
                estimate.realized_px.x, estimate.realized_px.y};
            output.causal_memory_in_flight_px = {
                estimate.in_flight_px.x, estimate.in_flight_px.y};
            output.causal_memory_scheduled_px = {
                estimate.scheduled_px.x, estimate.scheduled_px.y};
            output.causal_memory_pending_total_px = {
                estimate.pending_total_px.x, estimate.pending_total_px.y};
            previous_capture_seconds = input.capture_time_seconds;
        }

        const auto normalized = forward_aim_response_curve(
            {static_cast<float>(output.final_stick.x),
             static_cast<float>(output.final_stick.y)},
            script.config.camera_response_curve);
        DeliveredFinalCommandSample sample;
        sample.delivered_at_seconds =
            static_cast<double>(input.now_ms) / 1000.0 + 1.0e-6;
        sample.final_stick = {
            static_cast<float>(output.final_stick.x),
            static_cast<float>(output.final_stick.y)};
        sample.camera_velocity_px_per_second = {
            normalized.x * 500.0f,
            -normalized.y * 500.0f};
        sample.target_id = 1;
        sample.ads_epoch = 1;
        sample.delivered = true;
        sample.output_enabled = true;
        ledger.observe(sample);
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, controller, cohort,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    return {
        score_causal_memory_trace(
            trace, ledger_delay_ms, plant_delay_ms),
        std::move(trace)};
}

ControllerStep proportional_controller(int delay_ms = 0, double scale = 0.025) {
    return [=](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        if (input.target_present && input.now_ms >= delay_ms) {
            output.final_stick.x = std::clamp(
                input.observed_error_px.x * scale, -1.0, 1.0);
            output.final_stick.y = std::clamp(
                -input.observed_error_px.y * scale, -1.0, 1.0);
            output.requested_assist_stick = output.final_stick;
            output.shaped_assist_stick = output.final_stick;
        }
        return output;
    };
}

void test_runner_executes_exact_duration_and_preserves_identity() {
    BenchmarkConfig config;
    config.duration_ms = 60'000;
    const ScenarioScript script = generate_script(1337, config);
    int calls = 0;
    ControllerStep zero = [&](const ControllerObservation&) {
        ++calls;
        return ControllerStepResult{};
    };
    const BenchmarkResult result =
        run_simulation(script, ManualProfile::Pure, zero);
    require(calls == 60'000, "runner must execute exactly 60,000 ticks");
    require(result.ticks == 60'000, "result must record exact tick count");
    require(result.seed == script.seed && result.script_hash == script.hash,
            "runner must preserve scenario identity");
}

void test_miss_respects_deadline_and_tracking_is_exactly_1000ms() {
    const ScenarioScript missed_script = stationary_script(400, {100.0, 0.0}, 250);
    const BenchmarkResult missed = run_simulation(
        missed_script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });
    require(missed.targets_spawned == 1 && missed.targets_missed == 1,
            "unacquired target must miss at deadline");
    require(missed.targets.front().first_entry_ms == -1,
            "miss must not create an entry time");

    const ScenarioScript tracked_script = stationary_script(1'300, {20.0, 0.0});
    const BenchmarkResult tracked = run_simulation(
        tracked_script, ManualProfile::Pure, proportional_controller());
    require(tracked.targets_acquired == 1, "controller must acquire target");
    require(tracked.targets.front().tracking_errors_px.size() == 1'000,
            "tracking window must contain exactly 1000 ticks");
}

void test_virtual_camera_x_and_y_signs_close_error() {
    const ScenarioScript script = stationary_script(30, {20.0, 20.0});
    std::vector<Vec2d> observations;
    ControllerStep controller = [&](const ControllerObservation& input) {
        if (input.fresh_vision) observations.push_back(input.observed_error_px);
        ControllerStepResult output;
        output.final_stick = {1.0, -1.0};
        output.target_observed = true;
        output.tracker_reliable = true;
        output.bodylock_mode = true;
        return output;
    };
    (void)run_simulation(script, ManualProfile::Pure, controller);
    require(observations.size() > 2, "fixture must receive observations");
    require(observations.back().x < observations.front().x,
            "positive X stick must close positive screen X error");
    require(observations.back().y < observations.front().y,
            "negative Y stick must close positive screen Y error");
}

void test_virtual_camera_can_delay_delivered_control_without_delaying_controller() {
    ScenarioScript script = stationary_script(30, {20.0, 0.0});
    script.config.control_response_delay_ms = 10;
    std::vector<SimulationTraceFrame> trace;
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.final_stick = input.target_present ? Vec2d{1.0, 0.0} : Vec2d{};
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, controller, BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    require(trace.size() == 30, "delay fixture must retain every tick");
    require(std::fabs(trace[9].true_error_after_px.x - 20.0) < 1e-9,
            "camera must not react before the configured delivery delay");
    require(trace[10].true_error_after_px.x < 20.0,
            "camera must apply the queued stick at the configured delay");
}

void test_full_speed_left_strafe_moves_relative_error_with_inertia() {
    ScenarioScript script = stationary_script(320, {100.0, 0.0}, 320);
    auto& strafe = script.targets.front().player_strafe;
    strafe.initial_direction = 1;
    strafe.onset_ms = 10;
    strafe.reverse_ms = 120;
    strafe.release_ms = 240;
    strafe.top_speed_px_per_second = 200.0;
    strafe.time_constant_ms = 100.0;

    std::vector<SimulationTraceFrame> off_trace;
    const BenchmarkResult off = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; },
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { off_trace.push_back(frame); },
        PlayerStrafeMode::Off);
    std::vector<SimulationTraceFrame> on_trace;
    const BenchmarkResult on = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; },
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { on_trace.push_back(frame); },
        PlayerStrafeMode::FullReversal);

    require(off.script_hash == on.script_hash,
            "paired strafe variants must share target identity");
    require(on_trace[10].input.left_x == 1.0 &&
                on_trace[120].input.left_x == -1.0 &&
                on_trace[240].input.left_x == 0.0,
            "strafe schedule must deliver full-scale onset, reversal, release");
    require(on_trace[60].true_error_after_px.x <
                off_trace[60].true_error_after_px.x,
            "positive player motion must shift target left");
    require(on_trace[10].player_velocity_x_px_per_second > 0.0 &&
                on_trace[10].player_velocity_x_px_per_second < 200.0,
            "player velocity must ramp instead of jumping");
    require(std::fabs(on_trace[241].player_velocity_x_px_per_second) <
                std::fabs(on_trace[239].player_velocity_x_px_per_second),
            "release must decay velocity instead of stopping instantly");
    require(std::fabs(on_trace[241].player_velocity_x_px_per_second) > 0.0,
            "release must retain bounded inertia");
    require(off.left_strafe_active_ms == 0 &&
                off.max_abs_player_speed_px_per_second == 0.0,
            "off mode must leave player plant neutral");
    require(on.left_strafe_active_ms == 230 &&
                on.left_strafe_reversals == 1 &&
                on.max_abs_left_x == 1.0 &&
                on.max_abs_player_speed_px_per_second > 0.0,
            "strafe run must retain audit metrics");
}

void test_slide_and_jump_shift_vertical_error_and_emit_audit_metrics() {
    ScenarioScript script = stationary_script(900, {0.0, 0.0}, 300);
    auto& motion = script.targets.front().player_vertical;
    motion.slide_onset_ms = 100;
    motion.slide_drop_ms = 100;
    motion.slide_hold_ms = 100;
    motion.slide_recover_ms = 100;
    motion.slide_depth_px = 40.0;
    motion.slide_instant_recovery = false;
    motion.jump_onset_ms = 100;
    motion.jump_duration_ms = 600;
    motion.jump_height_px = 50.0;

    auto zero_controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    std::vector<SimulationTraceFrame> slide_trace;
    const BenchmarkResult slide = run_simulation(
        script, ManualProfile::Pure, zero_controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) {
            slide_trace.push_back(frame);
        },
        PlayerStrafeMode::Off, PlayerVerticalMotionMode::Slide);
    std::vector<SimulationTraceFrame> jump_trace;
    const BenchmarkResult jump = run_simulation(
        script, ManualProfile::Pure, zero_controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) {
            jump_trace.push_back(frame);
        },
        PlayerStrafeMode::Off, PlayerVerticalMotionMode::Jump);

    require(slide_trace[200].true_error_after_px.y < -39.0,
            "slide camera drop must move the target upward on screen");
    require(std::fabs(slide_trace[400].true_error_after_px.y) < 1e-9,
            "linear slide recovery must return to standing height");
    require(jump_trace[400].true_error_after_px.y > 49.0,
            "jump apex must move the target downward on screen");
    require(std::fabs(jump_trace[700].true_error_after_px.y) < 1e-9,
            "jump landing must restore standing height");
    require(slide.player_slide_events == 1 &&
                slide.player_jump_events == 0 &&
                slide.player_vertical_active_ms > 0 &&
                slide.max_abs_player_vertical_offset_px >= 40.0 &&
                slide.max_abs_player_vertical_speed_px_per_second > 0.0,
            "slide run must retain vertical-motion audit metrics");
    require(jump.player_slide_events == 0 &&
                jump.player_jump_events == 1 &&
                jump.player_vertical_active_ms > 0 &&
                jump.max_abs_player_vertical_offset_px >= 50.0,
            "jump run must retain vertical-motion audit metrics");
}

void test_default_simulation_mode_is_explicit_off() {
    const ScenarioScript script = stationary_script(80, {20.0, 0.0});
    const BenchmarkResult implicit = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    const BenchmarkResult explicit_off = run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::AdsAcquire, {}, PlayerStrafeMode::Off);
    require(implicit.acquire_points == explicit_off.acquire_points &&
                implicit.tracking_points == explicit_off.tracking_points &&
                implicit.mean_error_px == explicit_off.mean_error_px,
            "default simulation must preserve pre-feature no-strafe result");
}

void test_closed_loop_score_ordering() {
    const ScenarioScript script = stationary_script(1'350, {100.0, 0.0});
    const BenchmarkResult fast = run_simulation(
        script, ManualProfile::Pure, proportional_controller(0, 0.035));
    const BenchmarkResult delayed = run_simulation(
        script, ManualProfile::Pure, proportional_controller(120, 0.035));
    const BenchmarkResult stopped = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });

    require(fast.acquire_points > delayed.acquire_points,
            "fast controller must earn more acquisition points");
    require(delayed.acquire_points > stopped.acquire_points,
            "delayed controller must beat stopped controller");
    require(fast.tracking_points >= delayed.tracking_points,
            "fast controller must not lose tracking score");
}

void test_same_script_is_reused_for_pure_and_mixed_runs() {
    BenchmarkConfig config;
    config.duration_ms = 3'000;
    const ScenarioScript script = generate_script(424242, config);
    const BenchmarkResult pure = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    const BenchmarkResult mixed = run_simulation(
        script, ManualProfile::Mixed, proportional_controller());
    require(pure.script_hash == mixed.script_hash &&
                pure.script_hash == script.hash,
            "manual profiles must consume the identical scenario script");
    require(pure.manual_profile == ManualProfile::Pure &&
                mixed.manual_profile == ManualProfile::Mixed,
            "result must identify manual profile");
}

void test_scripted_manual_input_is_controller_independent() {
    const ScenarioScript script = stationary_script(200, {80.0, 40.0}, 300);
    auto capture = [](std::vector<Vec2d>& values, ControllerStep inner) {
        return [&values, inner = std::move(inner)](
                   const ControllerObservation& input) mutable {
            if (input.target_present) values.push_back(input.manual_stick);
            return inner(input);
        };
    };
    for (const ManualProfile profile : {
             ManualProfile::Scripted,
             ManualProfile::WrongThenCorrect,
             ManualProfile::ArcRecovery}) {
        std::vector<Vec2d> zero_inputs;
        std::vector<Vec2d> active_inputs;
        (void)run_simulation(
            script, profile,
            capture(zero_inputs, [](const ControllerObservation&) {
                return ControllerStepResult{};
            }));
        (void)run_simulation(
            script, profile,
            capture(active_inputs, proportional_controller(0, 0.01)));

        require(zero_inputs.size() == active_inputs.size() &&
                    !zero_inputs.empty(),
                "fixed replay must expose matching manual sample counts");
        for (std::size_t index = 0; index < zero_inputs.size(); ++index) {
            require(zero_inputs[index].x == active_inputs[index].x &&
                        zero_inputs[index].y == active_inputs[index].y,
                    "fixed manual samples must not depend on controller output");
        }
    }
}

void test_manual_recovery_profiles_expose_expected_trajectory() {
    const ScenarioScript script = stationary_script(400, {80.0, 0.0}, 400);
    auto samples_for = [&](ManualProfile profile) {
        std::vector<Vec2d> values;
        (void)run_simulation(
            script, profile,
            [&](const ControllerObservation& input) {
                if (input.target_present) values.push_back(input.manual_stick);
                return ControllerStepResult{};
            });
        return values;
    };

    const auto recover = samples_for(ManualProfile::WrongThenCorrect);
    require(recover.size() == 400,
            "recover fixture must retain the full target lifetime");
    require(recover[20].x < -0.6 && std::fabs(recover[20].y) < 1e-9,
            "recover profile must begin with a strong wrong-way command");
    require(recover[120].x > 0.6 && std::fabs(recover[120].y) < 1e-9,
            "recover profile must reverse into a strong correction");
    require(length(recover[320]) < 1e-9,
            "recover profile must release after its correction");

    const auto arc = samples_for(ManualProfile::ArcRecovery);
    require(arc.size() == 400,
            "arc fixture must retain the full target lifetime");
    require(std::fabs(arc.front().x) < 1e-9 &&
                std::fabs(arc.front().y) > 0.6,
            "arc profile must begin with a strong tangential error");
    require(arc[70].x > 0.3 && std::fabs(arc[70].y) > 0.3,
            "arc profile must pass through a strong tangential command");
    require(arc[180].x > 0.6,
            "arc profile must finish in the corrective direction");
    require(length(arc[320]) < 1e-9,
            "arc profile must release after its corrective hold");
}

void test_virtual_camera_applies_configured_forward_response_curve() {
    ScenarioScript linear = stationary_script(2, {100.0, 0.0}, 2);
    linear.config.slowdown_edge_multiplier = 1.0;
    linear.config.slowdown_center_multiplier = 1.0;
    ScenarioScript dynamic = linear;
    dynamic.config.camera_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;

    auto fixed_stick = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.final_stick = input.target_present ? Vec2d{0.3, 0.0} : Vec2d{};
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    std::vector<SimulationTraceFrame> linear_trace;
    std::vector<SimulationTraceFrame> dynamic_trace;
    (void)run_simulation(
        linear, ManualProfile::Pure, fixed_stick,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { linear_trace.push_back(frame); });
    (void)run_simulation(
        dynamic, ManualProfile::Pure, fixed_stick,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { dynamic_trace.push_back(frame); });

    const double linear_motion =
        linear_trace.front().true_error_before_px.x -
        linear_trace.front().true_error_after_px.x;
    const double dynamic_motion =
        dynamic_trace.front().true_error_before_px.x -
        dynamic_trace.front().true_error_after_px.x;
    const auto expected = controller_native::forward_aim_response_curve(
        {0.3f, 0.0f}, dynamic.config.camera_response_curve);
    require(std::fabs(linear_motion - 0.15) < 1.0e-7,
            "linear camera plant must preserve historical stick response");
    require(std::fabs(dynamic_motion -
                      static_cast<double>(expected.x) * 0.5) < 1.0e-7,
            "dynamic camera plant must use the configured forward curve");
    require(dynamic_motion < linear_motion,
            "legacy COD Dynamic must not be simulated as a linear low stick");
}

void test_manual_input_scale_changes_only_scripted_right_stick() {
    const ScenarioScript reference =
        stationary_script(400, {80.0, 0.0}, 400);
    ScenarioScript scaled = reference;
    scaled.config.manual_input_scale = 0.6875;

    auto samples_for = [](const ScenarioScript& script) {
        std::vector<Vec2d> values;
        (void)run_simulation(
            script, ManualProfile::WrongThenCorrect,
            [&](const ControllerObservation& input) {
                if (input.target_present) values.push_back(input.manual_stick);
                return ControllerStepResult{};
            });
        return values;
    };
    const auto full = samples_for(reference);
    const auto reduced = samples_for(scaled);
    require(full.size() == reduced.size() && full.size() == 400,
            "manual scale must not change target lifetime or tick count");
    for (std::size_t index = 0; index < full.size(); ++index) {
        require(
            std::fabs(reduced[index].x - full[index].x * 0.6875) < 1e-12 &&
                std::fabs(reduced[index].y - full[index].y * 0.6875) < 1e-12,
            "manual scale must affect only the scripted right-stick vector");
    }
}

void test_fixed_target_slot_preserves_wall_clock_spawn_times() {
    BenchmarkConfig config;
    config.duration_ms = 1'000;
    config.fixed_target_slot_ms = 400;
    config.inter_target_gap_ms = 50;
    const ScenarioScript script = generate_script(919191, config);
    auto starts_for = [&](ControllerStep inner) {
        std::vector<std::pair<std::uint64_t, int>> starts;
        std::uint64_t previous = 0;
        (void)run_simulation(
            script, ManualProfile::Pure,
            [&](const ControllerObservation& input) {
                if (input.target_present && input.target_id != previous) {
                    starts.emplace_back(input.target_id, input.now_ms);
                    previous = input.target_id;
                } else if (!input.target_present) {
                    previous = 0;
                }
                return inner(input);
            });
        return starts;
    };
    const auto stopped = starts_for([](const ControllerObservation&) {
        return ControllerStepResult{};
    });
    const auto active = starts_for(proportional_controller(0, 0.04));
    require(stopped == active && stopped.size() >= 2,
            "fixed slots must preserve target spawn times across controllers");
    require(stopped[0].second == 0 && stopped[1].second == 450,
            "fixed slot plus gap must define the wall-clock schedule");
}

void test_despawn_publishes_a_fresh_empty_observation() {
    const ScenarioScript script = stationary_script(400, {100.0, 0.0}, 250);
    bool saw_fresh_miss = false;
    ControllerStep controller = [&](const ControllerObservation& input) {
        if (!input.target_present && input.fresh_vision) saw_fresh_miss = true;
        return ControllerStepResult{};
    };
    (void)run_simulation(script, ManualProfile::Pure, controller);
    require(saw_fresh_miss,
            "despawn must send a fresh empty observation to clear controller hold");
}

void test_run_end_does_not_turn_partial_acquisition_into_a_miss() {
    const ScenarioScript script = stationary_script(100, {100.0, 0.0}, 250);
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });
    require(result.targets_spawned == 1, "partial target must remain recorded");
    require(result.targets_missed == 0,
            "run end before deadline must not count a target miss");
    require(!result.targets.front().acquisition_timed_out,
            "partial target must not be marked timed out");
}

void test_bodylock_cohort_scores_only_after_confirmed_mode_entry() {
    ScenarioScript script = stationary_script(1'200, {10.0, 0.0});
    script.config.bodylock_entry_timeout_ms = 120;
    ControllerStep delayed_bodylock = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 50;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, delayed_bodylock,
        BenchmarkCohort::BodyLockFollow);
    require(result.cohort == BenchmarkCohort::BodyLockFollow,
            "result must identify isolated BodyLock cohort");
    require(result.targets.size() == 1,
            "fixture must score one BodyLock target");
    require(result.targets.front().bodylock_entry_ms == 50,
            "warm-up time must be recorded but not scored");
    require(result.targets.front().tracking_errors_px.size() == 1'000,
            "BodyLock must receive exactly 1000 scored milliseconds");
}

void test_bodylock_cohort_fails_when_mode_never_enters() {
    ScenarioScript script = stationary_script(300, {10.0, 0.0});
    script.config.bodylock_entry_timeout_ms = 100;
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        },
        BenchmarkCohort::BodyLockFollow);
    require(result.bodylock_entry_failures == 1,
            "never entering BodyLock must fail the cohort");
    require(result.targets.front().tracking_errors_px.empty(),
            "ADS warm-up frames must not leak into BodyLock tracking score");
}

void test_bodylock_warmup_is_stationary_and_manual_neutral() {
    ScenarioScript script = stationary_script(1'200, {10.0, 0.0});
    script.targets.front().initial_velocity_px_per_second = {300.0, 0.0};
    std::vector<double> warmup_errors;
    ControllerStep delayed_bodylock = [&](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 50;
        if (input.target_present && input.now_ms < 50) {
            warmup_errors.push_back(input.observed_error_px.x);
            require(std::hypot(input.manual_stick.x, input.manual_stick.y) == 0.0,
                    "BodyLock warm-up must not inject mixed manual errors");
        }
        return output;
    };
    (void)run_simulation(script, ManualProfile::Mixed, delayed_bodylock,
                         BenchmarkCohort::BodyLockFollow);
    require(!warmup_errors.empty(), "fixture must observe BodyLock warm-up");
    require(std::fabs(warmup_errors.back() - warmup_errors.front()) < 1e-9,
            "BodyLock target motion must begin only after confirmed mode entry");
}

void test_compound_bodylock_motion_turns_twice_after_clean_entry() {
    ScenarioScript script = stationary_script(420, {60.0, 0.0});
    script.config.scenario_profile = ScenarioProfile::CompoundDirectional;
    script.targets.front().motion = MotionProfile::CompoundDirectional;
    script.targets.front().initial_velocity_px_per_second = {84.8528, 84.8528};
    script.targets.front().velocity_maneuvers = {
        {80, {-84.8528, 84.8528}},
        {180, {-84.8528, -84.8528}},
    };
    std::vector<SimulationTraceFrame> trace;
    ControllerStep immediate_bodylock = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, immediate_bodylock,
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    require(std::fabs(trace.front().true_error_before_px.x - 8.0) < 1e-9,
            "compound tracking stress must retain the clean 8px entry");
    bool saw_upper_left = false;
    bool saw_lower_left_after_upper_left = false;
    for (const SimulationTraceFrame& frame : trace) {
        if (!frame.target_active) continue;
        if (frame.target_velocity_px_per_second.x < 0.0 &&
            frame.target_velocity_px_per_second.y > 0.0) {
            saw_upper_left = true;
        } else if (saw_upper_left &&
                   frame.target_velocity_px_per_second.x < 0.0 &&
                   frame.target_velocity_px_per_second.y < 0.0) {
            saw_lower_left_after_upper_left = true;
        }
    }
    require(saw_upper_left && saw_lower_left_after_upper_left,
            "compound BodyLock tracking must execute two 2D direction changes");
}

void test_plan_diagnostics_and_ads_handoff_reach_the_scorer() {
    const ScenarioScript script = stationary_script(200, {30.0, 0.0});
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 5;
        output.final_stick = input.target_present
            ? Vec2d{0.50, 0.0}
            : Vec2d{};
        output.predicted_terminal_error_px = {-4.0, 0.0};
        output.radial_closing_velocity_px_per_sec = 75.0;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, controller);
    require(result.handoff_count == 1,
            "ADS-to-BodyLock transition must be counted once");
    require(result.handoff_episodes == 1,
            "ADS-to-BodyLock transition must create one scored episode");
    require(result.post_handoff_local_error_area_px_ms > 0.0,
            "handoff local burden must be populated");
    require(result.max_handoff_residual_px > 0.0,
            "handoff must retain the transition-frame residual");
    require(std::fabs(result.max_abs_handoff_closing_speed_px_per_sec - 75.0) < 1e-9,
            "tracker radial closing speed must reach the scorer unchanged");
}

void test_ads_handoff_on_acquisition_boundary_is_not_lost() {
    const ScenarioScript script = stationary_script(40, {10.0, 0.0});
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        output.radial_closing_velocity_px_per_sec = 25.0;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, controller, BenchmarkCohort::AdsAcquire);
    require(result.targets.front().acquired,
            "boundary fixture must complete ADS acquisition");
    require(result.targets.front().bodylock_entry_ms == 0,
            "BodyLock entry on the acquisition tick must retain its ADS time");
    require(result.handoff_count == 1,
            "ADS-to-BodyLock transition on the acquisition tick must be counted");
    require(result.max_handoff_residual_px > 0.0,
            "boundary handoff must retain a measurable residual");
}

Vec2d first_mixed_manual_for_target(std::uint64_t id) {
    ScenarioScript script = stationary_script(20, {30.0, 40.0});
    script.targets.front().id = id;
    Vec2d captured{};
    bool saw_manual = false;
    (void)run_simulation(
        script, ManualProfile::Mixed,
        [&](const ControllerObservation& input) {
            if (!saw_manual && input.target_present) {
                captured = input.manual_stick;
                saw_manual = true;
            }
            return ControllerStepResult{};
        });
    require(saw_manual, "fixture must capture mixed manual input");
    return captured;
}

void test_mixed_profile_contains_polar_component_errors() {
    const Vec2d radial{0.6, -0.8};
    const Vec2d tangent{0.8, 0.6};
    const auto radial_wrong = first_mixed_manual_for_target(7);
    require(radial_wrong.x * radial.x + radial_wrong.y * radial.y < -0.28,
            "target 7 must contain wrong radial manual input");
    require(radial_wrong.x * tangent.x + radial_wrong.y * tangent.y > 0.22,
            "target 7 must preserve helpful tangential manual input");

    const auto tangent_wrong = first_mixed_manual_for_target(8);
    require(tangent_wrong.x * radial.x + tangent_wrong.y * radial.y > 0.22,
            "target 8 must preserve helpful radial manual input");
    require(tangent_wrong.x * tangent.x + tangent_wrong.y * tangent.y < -0.28,
            "target 8 must contain wrong tangential manual input");
}

void test_trace_is_deterministic_and_observational() {
    const ScenarioScript script = stationary_script(40, {20.0, 10.0});
    const BenchmarkResult plain = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    std::vector<SimulationTraceFrame> trace;
    const BenchmarkResult observed = run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });

    require(plain.acquire_points == observed.acquire_points,
            "trace observer must not change acquisition score");
    require(plain.tracking_points == observed.tracking_points,
            "trace observer must not change tracking score");
    require(trace.size() == 40, "trace must contain one frame per tick");
    require(trace.front().true_error_before_px.x == 20.0,
            "trace must expose pre-control ground truth");
    require(trace.front().absolute_ms == 0,
            "trace timestamps must begin at zero");
}

void test_obsolete_manual_profile_persists_after_center_crossing() {
    ScenarioScript script = stationary_script(260, {0.0, -40.0});
    script.targets.front().id = 11;
    std::vector<Vec2d> manual;
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::ObsoleteAfterCrossing,
        [&](const ControllerObservation& input) {
            if (input.target_present) manual.push_back(input.manual_stick);
            ControllerStepResult output;
            output.final_stick = {0.0, 1.0};
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        });
    require(manual.size() == 260, "fixture must expose every active tick");
    require(manual[0].y > 0.60,
            "obsolete profile must begin with realistic upward manual strength");
    require(manual[90].y > 0.60,
            "manual direction must persist after the counterfactual crossing");
    require(std::fabs(manual[250].y) < 1e-9,
            "manual persistence must end within the bounded 80-140ms window");
    require(result.maximum_vertical_overshoot_px > 0.0,
            "V1 must measure crossing without relying on brake arming");
    require(result.post_cross_wrong_way_output_integral > 0.0,
            "V1 must measure obsolete delivered output after crossing");
}

void test_short_occlusion_withholds_publication_without_fresh_miss() {
    ScenarioScript script = stationary_script(90, {8.0, 0.0});
    script.targets.front().vision_occlusion_bursts.push_back({20, 24});
    std::vector<SimulationTraceFrame> trace;
    (void)run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });

    require(trace.size() == 90, "occlusion fixture must retain every tick");
    const std::uint64_t frame_before_burst = trace[19].input.frame_id;
    require(trace[19].fresh_vision,
            "fixture must publish immediately before the burst");
    for (int tick = 20; tick < 44; ++tick) {
        require(trace[tick].vision_occluded,
                "burst ticks must be marked occluded");
        require(!trace[tick].fresh_vision,
                "occlusion must withhold scheduled publication");
        require(trace[tick].input.target_present,
                "occlusion must not become a confirmed target miss");
        require(trace[tick].input.frame_id == frame_before_burst,
                "withheld frames must not advance Vision identity");
    }
    require(!trace[44].vision_occluded && trace[44].fresh_vision,
            "first tick after the burst must reveal a fresh observation");
    require(trace[44].input.frame_id == frame_before_burst + 1,
            "reveal must advance Vision identity exactly once");
}

void test_camera_recoil_moves_true_error_but_visual_kick_does_not() {
    auto run_profile = [](VisionDisturbanceProfile profile) {
        ScenarioScript script = stationary_script(320, {0.0, 0.0});
        script.config.vision_disturbance = profile;
        std::vector<SimulationTraceFrame> trace;
        ControllerStep passive = [](const ControllerObservation& input) {
            ControllerStepResult output;
            output.bodylock_mode = input.target_present;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        };
        (void)run_simulation(
            script, ManualProfile::Pure, passive,
            BenchmarkCohort::BodyLockFollow,
            [&](const SimulationTraceFrame& frame) {
                trace.push_back(frame);
            });
        return trace;
    };
    const auto visual =
        run_profile(VisionDisturbanceProfile::GunKickAdversarial);
    const auto physical =
        run_profile(VisionDisturbanceProfile::CameraRecoil);
    double visual_true_excursion = 0.0;
    double physical_true_excursion = 0.0;
    for (std::size_t index = 0; index < visual.size(); ++index) {
        visual_true_excursion = std::max(
            visual_true_excursion,
            length(visual[index].true_error_after_px));
        physical_true_excursion = std::max(
            physical_true_excursion,
            length(physical[index].true_error_after_px));
    }
    require(visual_true_excursion < 1e-9,
            "observation-only gun kick must not move the true camera");
    require(physical_true_excursion > 3.0,
            "camera recoil fixture must move the true relative error");
}

void test_horizontal_aim_bias_is_observation_only_and_recovers() {
    ScenarioScript script = stationary_script(360, {0.0, 0.0});
    script.config.vision_disturbance =
        VisionDisturbanceProfile::HorizontalAimBiasRecovery;
    std::vector<SimulationTraceFrame> trace;
    ControllerStep passive = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.bodylock_mode = input.target_present;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, passive,
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) {
            trace.push_back(frame);
        });
    double maximum_observed_bias = 0.0;
    double maximum_true_error = 0.0;
    double final_observed_error = 1000.0;
    for (const auto& frame : trace) {
        if (frame.fresh_vision) {
            maximum_observed_bias = std::max(
                maximum_observed_bias,
                std::fabs(frame.input.observed_error_px.x));
            final_observed_error =
                std::fabs(frame.input.observed_error_px.x);
        }
        maximum_true_error = std::max(
            maximum_true_error,
            std::fabs(frame.true_error_after_px.x));
    }
    require(maximum_observed_bias > 20.0,
            "fixture must begin with a materially wrong horizontal aim point");
    require(maximum_true_error < 1e-9,
            "wrong target localization must not move the physical camera");
    require(final_observed_error < 0.5,
            "fresh target localization must recover to the true point");
}

void test_micro_input_dropout_decoy_keeps_ads_held_and_publishes_decoy() {
    ScenarioScript script = stationary_script(700, {4.0, 0.0});
    script.config.vision_disturbance =
        VisionDisturbanceProfile::TargetDropoutDecoy;
    script.targets[0].vision_occlusion_bursts = {{110, 70}, {360, 50}};
    int decoy_frames = 0;
    int micro_ticks = 0;
    bool primary_leaked_into_dropout = false;
    ControllerStep passive = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.bodylock_mode = input.target_present;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::MicroCorrection, passive,
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) {
            if (std::fabs(frame.input.manual_stick.x) == 0.03) ++micro_ticks;
            if (frame.input.decoy_candidate_present) {
                ++decoy_frames;
                primary_leaked_into_dropout = primary_leaked_into_dropout ||
                    frame.input.primary_candidate_visible ||
                    !frame.input.target_present;
            }
        });
    require(decoy_frames == 120,
            "dropout-decoy fixture must publish every scheduled hidden frame");
    require(!primary_leaked_into_dropout,
            "dropout must hide only the primary candidate while LT stays held");
    require(micro_ticks >= 639 && micro_ticks <= 640,
            "micro profile must emit one bounded 3% nudge and correction");
}

void test_known_plant_gate_matrix_and_state_dependent_response() {
    const std::array<AimResponseCurveAlgorithm, 2> curves{
        AimResponseCurveAlgorithm::Linear,
        AimResponseCurveAlgorithm::CodDynamicLegacyLut};
    const std::array<int, 3> delays{5, 20, 45};
    const std::array<BenchmarkCohort, 2> cohorts{
        BenchmarkCohort::AdsAcquire,
        BenchmarkCohort::BodyLockFollow};
    for (const auto curve : curves) {
        for (const int delay : delays) {
            for (const auto cohort : cohorts) {
                const auto result = run_known_plant_case(
                    delay, delay, curve, cohort, false);
                require(result.accuracy.fresh_capture_count > 10,
                        "known-plant fixture must publish fresh captures");
                require(result.accuracy.paired_capture_count > 10,
                        "known-plant fixture must form capture pairs");
                require(result.accuracy.realized.scored_count > 0 &&
                            result.accuracy.in_flight.scored_count > 0 &&
                            result.accuracy.pending_total.scored_count > 0,
                        "known-plant fixture must score all non-empty phases");
                require(result.accuracy.gate_pass,
                        "exact known plant must pass Gate 1 matrix control");
                std::cout << "[W5 Gate1 control] curve="
                          << (curve == AimResponseCurveAlgorithm::Linear
                                  ? "linear" : "cod_dynamic")
                          << " delay_ms=" << delay
                          << " cohort="
                          << (cohort == BenchmarkCohort::AdsAcquire
                                  ? "ads" : "bodylock")
                          << " pending_mean_px="
                          << result.accuracy.pending_total.residual_mean_px
                          << " sign="
                          << result.accuracy.pending_total.sign_agreement
                          << " cosine="
                          << result.accuracy.pending_total.cosine_mean
                          << " PASS\n";
            }
        }
    }

    const auto slowdown = run_known_plant_case(
        20, 20, AimResponseCurveAlgorithm::Linear,
        BenchmarkCohort::AdsAcquire, true, {10.0, -8.0});
    const auto no_slowdown_near = run_known_plant_case(
        20, 20, AimResponseCurveAlgorithm::Linear,
        BenchmarkCohort::AdsAcquire, false, {10.0, -8.0});
    require(slowdown.accuracy.paired_capture_count > 10,
            "slowdown negative control must still have capture pairs");
    require(no_slowdown_near.accuracy.gate_pass,
            "no-slowdown near control must pass Gate 1");
    require(slowdown.accuracy.pending_total.residual_mean_px >
                no_slowdown_near.accuracy.pending_total.residual_mean_px + 0.25 &&
                slowdown.accuracy.pending_total.residual_p95_px >
                no_slowdown_near.accuracy.pending_total.residual_p95_px + 0.50,
            "state-dependent slowdown must visibly degrade fixed-response accuracy");
    std::cout << "[W5 Gate1 negative] default aim slowdown response_model_RED"
              << " standard_gate_pass=" << slowdown.accuracy.gate_pass
              << " pending_mean_px="
              << slowdown.accuracy.pending_total.residual_mean_px
              << " pending_p95_px="
              << slowdown.accuracy.pending_total.residual_p95_px
              << " no_slowdown_mean_px="
              << no_slowdown_near.accuracy.pending_total.residual_mean_px
              << " RED\n";

    const auto mismatched_delay = run_known_plant_case(
        24, 20, AimResponseCurveAlgorithm::Linear,
        BenchmarkCohort::AdsAcquire, false);
    const auto matched_delay = run_known_plant_case(
        20, 20, AimResponseCurveAlgorithm::Linear,
        BenchmarkCohort::AdsAcquire, false);
    require(mismatched_delay.accuracy.paired_capture_count > 10,
            "delay mismatch control must still have pairs");
    require(mismatched_delay.accuracy.pending_total.residual_mean_px >
                matched_delay.accuracy.pending_total.residual_mean_px + 0.01 &&
                mismatched_delay.accuracy.pending_total.normalized_residual_mean >
                matched_delay.accuracy.pending_total.normalized_residual_mean +
                    0.01,
            "mismatched plant delay must visibly degrade accuracy");
    std::cout << "[W5 Gate1 sensitivity RED] plant_delay=24 ledger_delay=20"
              << " declared_gate_pass=" << mismatched_delay.accuracy.gate_pass
              << " pending_mean_px="
              << mismatched_delay.accuracy.pending_total.residual_mean_px
              << " matched_mean_px="
              << matched_delay.accuracy.pending_total.residual_mean_px
              << " normalized_mean="
              << mismatched_delay.accuracy.pending_total.normalized_residual_mean
              << " RED\n";
}

DeliveredFinalCommandSample causal_sample(
    double seconds,
    std::uint64_t target_id,
    std::uint64_t ads_epoch,
    float velocity_x,
    bool delivered = true,
    bool output_enabled = true) {
    DeliveredFinalCommandSample sample;
    sample.delivered_at_seconds = seconds;
    sample.final_stick = {velocity_x / 500.0f, 0.0f};
    sample.camera_velocity_px_per_second = {velocity_x, 0.0f};
    sample.target_id = target_id;
    sample.ads_epoch = ads_epoch;
    sample.delivered = delivered;
    sample.output_enabled = output_enabled;
    return sample;
}

DeliveredFinalCommandSample causal_sample_xy(
    double seconds,
    std::uint64_t target_id,
    std::uint64_t ads_epoch,
    Vec2d velocity,
    bool delivered = true,
    bool output_enabled = true) {
    DeliveredFinalCommandSample sample;
    sample.delivered_at_seconds = seconds;
    sample.final_stick = {
        static_cast<float>(velocity.x / 500.0),
        static_cast<float>(velocity.y / 500.0)};
    sample.camera_velocity_px_per_second = {
        static_cast<float>(velocity.x),
        static_cast<float>(velocity.y)};
    sample.target_id = target_id;
    sample.ads_epoch = ads_epoch;
    sample.delivered = delivered;
    sample.output_enabled = output_enabled;
    return sample;
}

void test_w5_adversarial_lifecycle_and_delivery_measurements() {
    auto sustained_target_samples = [](CausalMotionLedger& ledger,
                                       std::uint64_t target_id,
                                       std::uint64_t ads_epoch) {
        for (int index = 1; index <= 10; ++index) {
            require(ledger.observe(causal_sample(
                        static_cast<double>(index) / 1000.0,
                        target_id, ads_epoch, 100.0f)),
                    "adversarial sample must be accepted");
        }
    };

    CausalMotionLedger target_switch_ledger;
    sustained_target_samples(target_switch_ledger, 1, 1);
    require(target_switch_ledger.observe(
                causal_sample(0.011, 2, 2, 0.0f)),
            "replacement sample must be accepted");
    const auto target_switch_estimate = target_switch_ledger.estimate({
        0.010, 0.021, 0.021, 20.0f, 200.0f, 2, 2, false, 1});
    require(target_switch_estimate.valid &&
                target_switch_estimate.pending_valid &&
                target_switch_estimate.pending_total_px.x > 0.5,
            "target switch must retain old global pending work");
    std::cout << "[W5 PASS P0] target_switch_inside_delay global_pending="
              << " ledger_B_pending="
              << target_switch_estimate.pending_total_px.x
              << " realized_valid="
              << target_switch_estimate.realized_valid << "\n";

    CausalMotionLedger ads_epoch_ledger;
    sustained_target_samples(ads_epoch_ledger, 3, 1);
    require(ads_epoch_ledger.observe(
                causal_sample(0.011, 3, 2, 0.0f)),
            "new ADS epoch sample must be accepted");
    const auto ads_estimate = ads_epoch_ledger.estimate({
        0.010, 0.021, 0.021, 20.0f, 200.0f, 3, 2, false, 1});
    require(ads_estimate.pending_valid &&
                ads_estimate.pending_total_px.x > 0.5,
            "ADS release/re-press must retain old global pending work");
    std::cout << "[W5 PASS P0] ads_epoch_repress global_pending="
              << " ledger_new_epoch_pending="
              << ads_estimate.pending_total_px.x << "\n";

    CausalMotionLedger loss_ledger;
    sustained_target_samples(loss_ledger, 4, 1);
    require(loss_ledger.observe(causal_sample(
                0.011, 0, 0, 0.0f)),
            "target loss must retain a global targetless actuator sample");
    require(loss_ledger.observe(causal_sample(0.021, 4, 2, 0.0f)),
            "same-target reacquire sample must be accepted");
    const auto reacquire_estimate = loss_ledger.estimate({
        0.010, 0.030, 0.030, 20.0f, 200.0f, 4, 2, false, 1});
    require(reacquire_estimate.pending_valid &&
                reacquire_estimate.pending_total_px.x > 0.05,
            "loss/reacquire must retain old global in-flight work");
    std::cout << "[W5 PASS P0] loss_reacquire global_pending="
              << " ledger_reacquire_pending="
              << reacquire_estimate.pending_total_px.x << "\n";

    CausalMotionLedger failed_delivery_ledger;
    sustained_target_samples(failed_delivery_ledger, 5, 1);
    require(!failed_delivery_ledger.observe(causal_sample(
                0.011, 5, 1, 100.0f, false, true)),
            "failed delivery must invalidate the ledger sample");
    std::cout << "[W5 measurement] failed_delivery backend_state=unknown"
              << " (runtime disconnect semantics; no held-report claim)\n";

    CausalMotionLedger disabled_output_ledger;
    sustained_target_samples(disabled_output_ledger, 6, 1);
    require(!disabled_output_ledger.observe(causal_sample(
                0.011, 6, 1, 0.0f, true, false)),
            "disabled output must invalidate the ledger sample");
    std::cout << "[W5 measurement] output_disabled physical_state=unknown"
              << " unless successful neutral is separately observed\n";

    CausalMotionLedger boundary_ledger;
    // Half-open ownership:
    //   realized   [previous-delay, current-delay) = [.080,.100)
    //   in_flight  [current-delay, current)         = [.100,.120)
    //   scheduled  [current, decision)              = [.120,.140)
    // The .140 endpoint is deliberately a visibly different command and must
    // contribute zero duration to this estimate.
    require(boundary_ledger.observe(causal_sample_xy(
                0.080, 7, 1, {100.0, -50.0})),
            "boundary previous-delay sample must be accepted");
    require(boundary_ledger.observe(causal_sample_xy(
                0.100, 7, 1, {200.0, -75.0})),
            "boundary current-delay sample must be accepted");
    require(boundary_ledger.observe(causal_sample_xy(
                0.120, 7, 1, {300.0, -100.0})),
            "boundary current-capture sample must be accepted");
    require(boundary_ledger.observe(causal_sample_xy(
                0.140, 7, 1, {900.0, -900.0})),
            "boundary decision endpoint sample must be accepted");
    const auto boundary = boundary_ledger.estimate({
        0.100, 0.120, 0.140, 20.0f, 200.0f, 7, 1});
    require(boundary.status == CausalMotionLedgerStatus::Valid &&
                boundary.realized_valid && boundary.pending_valid,
            "half-open boundary estimate must be valid");
    require(std::fabs(boundary.realized_px.x - 2.0) < 1.0e-5 &&
                std::fabs(boundary.realized_px.y + 1.0) < 1.0e-5 &&
                std::fabs(boundary.in_flight_px.x - 4.0) < 1.0e-5 &&
                std::fabs(boundary.in_flight_px.y + 1.5) < 1.0e-5 &&
                std::fabs(boundary.scheduled_px.x - 6.0) < 1.0e-5 &&
                std::fabs(boundary.scheduled_px.y + 2.0) < 1.0e-5 &&
                std::fabs(boundary.pending_total_px.x - 10.0) < 1.0e-5 &&
                std::fabs(boundary.pending_total_px.y + 3.5) < 1.0e-5,
            "half-open boundary phase values must be exact and endpoint-safe");
    std::cout << "[W5 PASS] timestamp_boundaries interval=[begin,end)"
              << " realized=(" << boundary.realized_px.x << ','
              << boundary.realized_px.y << ") in_flight=("
              << boundary.in_flight_px.x << ',' << boundary.in_flight_px.y
              << ") scheduled=(" << boundary.scheduled_px.x << ','
              << boundary.scheduled_px.y << ") pending=("
              << boundary.pending_total_px.x << ','
              << boundary.pending_total_px.y << ")\n";

    CausalMotionLedger repeated_query_ledger;
    for (int index = 1; index <= 110; ++index) {
        require(repeated_query_ledger.observe(causal_sample(
                    static_cast<double>(index) / 1000.0,
                    8, 1, 100.0f)),
                "repeated-query sample must be accepted");
    }
    const CausalMotionPhaseRequest first_request{
        0.080, 0.100, 0.100, 20.0f, 200.0f, 8, 1};
    const auto first_estimate = repeated_query_ledger.estimate(first_request);
    for (int index = 111; index <= 116; ++index) {
        require(repeated_query_ledger.observe(causal_sample(
                    static_cast<double>(index) / 1000.0,
                    8, 1, 100.0f)),
                "post-capture sample must be accepted");
    }
    CausalMotionPhaseRequest recomputed_request = first_request;
    recomputed_request.decision_seconds = 0.116;
    const auto recomputed = repeated_query_ledger.estimate(recomputed_request);
    require(recomputed.scheduled_px.x > first_estimate.scheduled_px.x,
            "same capture with later decisions must grow scheduled work");
    require(first_estimate.scheduled_px.x == 0.0f,
            "first decision must start with empty scheduled window");
    std::cout << "[W5 Gate1 historical RED / PhaseB plant truth]"
              << " stale_between_captures held_scheduled="
              << first_estimate.scheduled_px.x
              << " recomputed_scheduled="
              << recomputed.scheduled_px.x
              << " (direct ledger control; controller-level PhaseB refresh is"
              << " covered by NativeControllerIntegrationTests)\n";

    const double continuous_latest_frame_work = 0.9 * 500.0 * 0.005;
    const double latest_only_zero_poll_work = 0.0;
    const double continuous_reversal_work =
        0.9 * 500.0 * 0.004 - 0.6 * 500.0 * 0.001;
    const double latest_only_reversal_work = -0.6 * 500.0 * 0.005;
    require(continuous_latest_frame_work > latest_only_zero_poll_work,
            "latest-only negative control must differ from continuous hold");
    require(continuous_reversal_work != latest_only_reversal_work,
            "latest-only reversal negative control must differ");
    std::cout << "[W5 UNRESOLVED NEGATIVE CONTROL] latest_only_game_admission"
              << " continuous_zero_poll="
              << continuous_latest_frame_work
              << " latest_only=" << latest_only_zero_poll_work
              << " continuous_reversal=" << continuous_reversal_work
              << " latest_only_reversal=" << latest_only_reversal_work << "\n";

    CausalMotionLedger pre_acquisition_ledger;
    require(pre_acquisition_ledger.observe(causal_sample(
                0.001, 0, 0, 450.0f)),
            "pre-acquisition targetless delivery must enter global history");
    require(pre_acquisition_ledger.observe(causal_sample(
                0.021, 9, 1, 0.0f)),
            "first target-owned sample must preserve targetless history");
    const auto pre_acquisition_estimate = pre_acquisition_ledger.estimate({
        0.020, 0.030, 0.030, 20.0f, 200.0f, 9, 1});
    require(pre_acquisition_estimate.pending_valid &&
                pre_acquisition_estimate.pending_total_px.x > 4.9,
            "global history must retain targetless pre-acquisition work");
    std::cout << "[W5 PASS P0] pre_acquisition_manual_carry_in ledger_pending="
              << pre_acquisition_estimate.pending_total_px.x
              << " (global actuator history)\n";
}

std::vector<SimulationTraceFrame> run_actual_pre_acquisition_trace() {
    ScenarioScript script = stationary_script(140, {4.0, 0.0}, 200);
    script.config.control_response_delay_ms = 20;
    script.config.initial_idle_ms = 40;
    script.config.fixed_target_slot_ms = 60;
    script.config.target_motion_enabled = false;
    script.targets.front().observation_at_ms.clear();
    script.targets.front().observation_noise_px.clear();
    for (int ms = 0; ms <= 100; ms += 5) {
        script.targets.front().observation_at_ms.push_back(ms);
        script.targets.front().observation_noise_px.push_back({});
    }

    std::vector<SimulationTraceFrame> trace;
    const ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        if (input.target_present) {
            output.controller_target_id = input.target_id;
            output.controller_ads_epoch = 1;
            output.final_stick = {};
        } else {
            // This is a successful manual/final actuator command before
            // acquisition. The simulator's delayed plant, rather than an
            // arithmetic constant, determines when it is applied.
            output.controller_target_id = 0;
            output.controller_ads_epoch = 0;
            output.final_stick = {0.90, 0.0};
        }
        output.requested_assist_stick = output.final_stick;
        output.shaped_assist_stick = output.final_stick;
        return output;
    };
    run_simulation(
        script, ManualProfile::Pure, controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    return trace;
}

std::vector<SimulationTraceFrame> run_actual_lifecycle_trace() {
    ScenarioScript script = stationary_script(100, {4.0, 0.0}, 200);
    TargetScript replacement = script.targets.front();
    replacement.id = 2;
    replacement.initial_error_px = {-4.0, 0.0};
    script.targets.push_back(replacement);
    script.config.control_response_delay_ms = 20;
    script.config.fixed_target_slot_ms = 20;
    script.config.inter_target_gap_ms = 0;
    script.config.target_motion_enabled = false;
    for (auto& target : script.targets) {
        target.observation_at_ms.clear();
        target.observation_noise_px.clear();
        for (int ms = 0; ms <= 50; ms += 5) {
            target.observation_at_ms.push_back(ms);
            target.observation_noise_px.push_back({});
        }
    }

    std::vector<SimulationTraceFrame> trace;
    const ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        if (input.target_present) {
            output.controller_target_id = input.target_id;
            output.controller_ads_epoch = input.target_id;
            output.final_stick = input.target_id == 1
                ? Vec2d{0.50, 0.0} : Vec2d{-0.30, 0.0};
        } else {
            output.controller_target_id = 0;
            output.controller_ads_epoch = 0;
            output.final_stick = {0.20, 0.0};
        }
        output.requested_assist_stick = output.final_stick;
        output.shaped_assist_stick = output.final_stick;
        return output;
    };
    run_simulation(
        script, ManualProfile::Pure, controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    return trace;
}

void test_actual_delayed_plant_truth_contracts() {
    const auto pre_acquisition_trace = run_actual_pre_acquisition_trace();
    const auto pre_acquisition_truth = summarize_causal_actuator_truth(
        pre_acquisition_trace);
    bool saw_post_acquisition_targetless_apply = false;
    for (const auto& frame : pre_acquisition_trace) {
        if (frame.plant_applied_valid &&
            frame.plant_applied_target_id == 0 &&
            frame.output.controller_target_id != 0) {
            saw_post_acquisition_targetless_apply = true;
            break;
        }
    }
    require(saw_post_acquisition_targetless_apply,
            "actual pre-acquisition trace must apply manual carry after target admission");
    require(pre_acquisition_truth.pre_acquisition_carry_sample_count > 0 &&
                pre_acquisition_truth.pre_acquisition_carry_displacement_px.x > 0.0,
            "actual delayed plant must retain targetless work applied before admission");
    require(pre_acquisition_truth.targetless_after_acquisition_carry_sample_count > 0 &&
                pre_acquisition_truth.targetless_after_acquisition_carry_displacement_px.x > 0.0,
            "actual delayed plant must retain targetless carry applied after admission");
    std::cout << "[W5 Gate1 historical RED / PhaseB plant truth]"
              << " actual_pre_acquisition_carry pre_apply_px="
              << pre_acquisition_truth.pre_acquisition_carry_displacement_px.x
              << " post_apply_px="
              << pre_acquisition_truth.targetless_after_acquisition_carry_displacement_px.x
              << " (independent delayed-plant attribution; controller-level"
              << " PhaseB global history is covered by NativeControllerIntegrationTests)\n";

    const auto lifecycle_trace = run_actual_lifecycle_trace();
    const auto lifecycle_truth = summarize_causal_actuator_truth(lifecycle_trace);
    bool saw_old_owner_after_replacement = false;
    for (const auto& frame : lifecycle_trace) {
        if (frame.plant_applied_valid &&
            frame.output.controller_target_id == 2 &&
            frame.plant_applied_target_id == 1) {
            saw_old_owner_after_replacement = true;
            break;
        }
    }
    require(saw_old_owner_after_replacement,
            "actual delayed plant must apply target A work after target B becomes current");
    require(lifecycle_truth.cross_boundary_old_owner_sample_count > 0 &&
                lifecycle_truth.cross_boundary_old_owner_abs_displacement_px >
                    std::fabs(lifecycle_truth.cross_boundary_old_owner_displacement_px.x),
            "cross-boundary truth must report sample magnitude separately from net vector");
    std::cout << "[W5 Gate1 historical RED / PhaseB plant truth]"
              << " actual_target_switch old_owner_net_px="
              << lifecycle_truth.cross_boundary_old_owner_displacement_px.x
              << " old_owner_abs_px="
              << lifecycle_truth.cross_boundary_old_owner_abs_displacement_px
              << " samples="
              << lifecycle_truth.cross_boundary_old_owner_sample_count << "\n";
}

void test_w5_delivery_partition_and_negative_controls() {
    CausalMotionLedger neutral_ledger;
    require(neutral_ledger.observe(causal_sample_xy(
                0.001, 30, 1, {100.0, 50.0})),
            "explicit-neutral control must accept the nonzero delivery");
    require(neutral_ledger.observe(causal_sample_xy(
                0.011, 30, 1, {0.0, 0.0})),
            "explicit-neutral control must accept the successful neutral");
    require(neutral_ledger.observe(causal_sample_xy(
                0.031, 30, 1, {0.0, 0.0})),
            "explicit-neutral control must retain the neutral state");
    const auto nonzero_before_neutral = neutral_ledger.estimate({
        0.001, 0.011, 0.011, 0.0f, 200.0f, 30, 1});
    const auto neutral_after = neutral_ledger.estimate({
        0.011, 0.031, 0.031, 0.0f, 200.0f, 30, 1});
    require(std::fabs(nonzero_before_neutral.realized_px.x - 1.0) < 1.0e-5 &&
                std::fabs(nonzero_before_neutral.realized_px.y - 0.5) < 1.0e-5 &&
                std::fabs(neutral_after.realized_px.x) < 1.0e-6 &&
                std::fabs(neutral_after.realized_px.y) < 1.0e-6,
            "successful explicit neutral must stop subsequent physical work");

    CausalMotionLedger vector_ledger;
    require(vector_ledger.observe(causal_sample_xy(
                0.001, 31, 1, {10.0, -10.0})),
            "diagonal deadzone sample must be accepted");
    require(vector_ledger.observe(causal_sample_xy(
                0.011, 31, 1, {500.0, -500.0})),
            "diagonal saturated sample must be accepted");
    require(vector_ledger.observe(causal_sample_xy(
                0.021, 31, 1, {-500.0, 500.0})),
            "diagonal reversal sample must be accepted");
    require(vector_ledger.observe(causal_sample_xy(
                0.031, 31, 1, {-500.0, 500.0})),
            "diagonal reversal hold sample must be accepted");
    const auto low_vector = vector_ledger.estimate({
        0.001, 0.011, 0.011, 0.0f, 200.0f, 31, 1});
    const auto reversed_vector = vector_ledger.estimate({
        0.021, 0.031, 0.031, 0.0f, 200.0f, 31, 1});
    require(low_vector.realized_px.x > 0.09 &&
                low_vector.realized_px.y < -0.09 &&
                reversed_vector.realized_px.x < -4.9 &&
                reversed_vector.realized_px.y > 4.9,
            "diagonal deadzone/saturation/reversal signs must be preserved");

    CausalMotionLedger horizon_ledger;
    require(horizon_ledger.observe(causal_sample(
                0.001, 32, 1, 100.0f)),
            "horizon fixture first sample must be accepted");
    require(horizon_ledger.observe(causal_sample(
                0.101, 32, 1, 100.0f)),
            "horizon fixture middle sample must be accepted");
    require(horizon_ledger.observe(causal_sample(
                0.201, 32, 1, 100.0f)),
            "horizon fixture last sample must be accepted");
    const auto horizon = horizon_ledger.estimate({
        0.100, 0.200, 0.250, 20.0f, 50.0f, 32, 1});
    require(horizon.status == CausalMotionLedgerStatus::HorizonExceeded &&
                !horizon.valid,
            "long dropout/horizon overflow must be explicit invalid");

    const std::array<int, 5> jitter_gaps_ms{4, 6, 11, 25, 60};
    for (const int gap_ms : jitter_gaps_ms) {
        CausalMotionLedger jitter_ledger;
        const double first_seconds = 0.001;
        const double second_seconds =
            first_seconds + static_cast<double>(gap_ms) / 1000.0;
        require(jitter_ledger.observe(causal_sample(
                    first_seconds, 33, 1, 100.0f)) &&
                    jitter_ledger.observe(causal_sample(
                        second_seconds, 33, 1, 100.0f)),
                "cadence jitter sample pair must be accepted");
        const auto jitter = jitter_ledger.estimate({
            first_seconds, second_seconds, second_seconds,
            0.0f, 200.0f, 33, 1});
        require(jitter.valid,
                "cadence jitter within the memory horizon must remain measurable");
        std::cout << "[W5 PASS control] cadence_gap_ms=" << gap_ms
                  << " zoh_realized_px=" << jitter.realized_px.x << "\n";
    }

    const auto exact = run_known_plant_case(
        20, 20, AimResponseCurveAlgorithm::Linear,
        BenchmarkCohort::AdsAcquire, false);
    auto dropout_trace = exact.trace;
    for (auto& frame : dropout_trace) {
        if (frame.absolute_ms >= 350 && frame.absolute_ms < 410) {
            frame.plant_applied_valid = false;
        }
    }
    const auto dropout = score_causal_memory_trace(
        dropout_trace, 20.0, 20.0);
    require(dropout.truth_incomplete_window_count > 0,
            "60ms plant/capture dropout must be visible as incomplete truth");
    std::cout << "[W5 RED P1] truth_instrumentation_availability_dropout_60ms"
              << " truth_incomplete="
              << dropout.truth_incomplete_window_count << "\n";

    auto distributed_trace = exact.trace;
    for (auto& frame : distributed_trace) {
        if (!frame.plant_applied_valid) continue;
        const double phase = static_cast<double>(frame.absolute_ms % 20) / 19.0;
        const double response_factor = 0.45 + 0.55 * std::clamp(phase, 0.0, 1.0);
        frame.plant_applied_camera_velocity_px_per_second.x *= response_factor;
        frame.plant_applied_camera_velocity_px_per_second.y *= response_factor;
        frame.plant_applied_displacement_px.x *= response_factor;
        frame.plant_applied_displacement_px.y *= response_factor;
    }
    const auto distributed = score_causal_memory_trace(
        distributed_trace, 20.0, 20.0);
    require(!distributed.gate_pass &&
                distributed.pending_total.normalized_residual_mean >
                    exact.accuracy.pending_total.normalized_residual_mean + 0.10,
            "distributed/ramped plant negative control must visibly fail");
    std::cout << "[W5 RED P1] distributed_response gate_pass="
              << distributed.gate_pass
              << " normalized_mean="
              << distributed.pending_total.normalized_residual_mean << "\n";

    const std::array<AimResponseCurveAlgorithm, 2> curves{
        AimResponseCurveAlgorithm::Linear,
        AimResponseCurveAlgorithm::CodDynamicLegacyLut};
    const std::array<Vec2d, 4> final_sticks{
        Vec2d{0.02, -0.02},   // deadzone-edge diagonal
        Vec2d{1.0, 0.0},      // positive saturation edge
        Vec2d{0.80, -0.70},   // diagonal high magnitude
        Vec2d{-0.80, 0.70},   // rapid sign reversal
    };
    for (const auto curve : curves) {
        AimResponseCurveConfig curve_config;
        curve_config.algorithm = curve;
        for (const Vec2d final_stick : final_sticks) {
            const auto normalized = forward_aim_response_curve(
                {static_cast<float>(final_stick.x),
                 static_cast<float>(final_stick.y)},
                curve_config);
            CausalMotionLedger curve_ledger;
            require(curve_ledger.observe(causal_sample_xy(
                        0.001, 34, 1,
                        {normalized.x * 500.0,
                         -normalized.y * 500.0})),
                    "curve conversion fixture first sample must be accepted");
            require(curve_ledger.observe(causal_sample_xy(
                        0.011, 34, 1,
                        {normalized.x * 500.0,
                         -normalized.y * 500.0})),
                    "curve conversion fixture hold sample must be accepted");
            const auto estimate = curve_ledger.estimate({
                0.001, 0.011, 0.011, 0.0f, 200.0f, 34, 1});
            require(estimate.realized_valid &&
                        std::fabs(estimate.realized_px.x -
                                  normalized.x * 5.0) < 1.0e-5 &&
                        std::fabs(estimate.realized_px.y +
                                  normalized.y * 5.0) < 1.0e-5,
                    "final-stick forward curve must feed causal XY truth");
        }
        std::cout << "[W5 PASS control] final_stick_forward_curve="
                  << (curve == AimResponseCurveAlgorithm::Linear
                          ? "linear" : "cod_dynamic")
                  << " deadzone/saturation/diagonal/reversal\n";
    }
}

SimulationTraceFrame actuator_truth_frame(
    int absolute_ms,
    std::uint64_t current_target_id,
    std::uint64_t current_ads_epoch,
    std::uint64_t origin_target_id,
    std::uint64_t origin_ads_epoch,
    Vec2d displacement) {
    SimulationTraceFrame frame;
    frame.absolute_ms = absolute_ms;
    frame.output.controller_target_id = current_target_id;
    frame.output.controller_ads_epoch = current_ads_epoch;
    frame.plant_applied_valid = true;
    frame.plant_applied_target_id = origin_target_id;
    frame.plant_applied_ads_epoch = origin_ads_epoch;
    frame.plant_applied_command_delivery_seconds =
        static_cast<double>(absolute_ms) / 1000.0;
    frame.plant_applied_displacement_px = displacement;
    return frame;
}

void test_global_actuator_truth_preserves_cross_boundary_carry() {
    const std::vector<SimulationTraceFrame> trace{
        // Unit-level summary controls: first two samples are genuinely
        // pre-acquisition; the third is dangerous post-acquisition
        // targetless carry; later samples exercise two boundaries.
        actuator_truth_frame(0, 0, 0, 0, 0, {0.25, 0.0}),
        actuator_truth_frame(1, 0, 0, 0, 0, {0.25, 0.0}),
        actuator_truth_frame(2, 11, 1, 0, 0, {0.10, 0.0}),
        actuator_truth_frame(3, 11, 1, 11, 1, {0.10, 0.0}),
        actuator_truth_frame(4, 22, 2, 11, 1, {0.30, 0.0}),
        actuator_truth_frame(5, 22, 2, 22, 2, {0.10, 0.0}),
        actuator_truth_frame(6, 0, 0, 22, 2, {0.15, 0.0}),
        actuator_truth_frame(7, 22, 3, 22, 2, {0.10, 0.0}),
        actuator_truth_frame(8, 22, 3, 22, 3, {0.10, 0.0}),
    };
    const CausalActuatorTruthSummary truth =
        summarize_causal_actuator_truth(trace);
    require(truth.pre_acquisition_carry_displacement_px.x > 0.4,
            "global truth must retain pre-acquisition manual carry-in");
    require(truth.targetless_after_acquisition_carry_displacement_px.x > 0.0 &&
                truth.targetless_after_acquisition_carry_sample_count == 1,
            "global truth must retain targetless carry after acquisition");
    require(truth.targetless_applied_displacement_px.x > 0.5,
            "global truth must retain targetless physical work");
    require(truth.cross_boundary_old_owner_displacement_px.x > 0.25 &&
                truth.cross_boundary_old_owner_sample_count >= 2 &&
                truth.cross_boundary_old_owner_abs_displacement_px >= 0.30,
            "global truth must retain old-owner work after lifecycle switch");
    require(truth.lifecycle_boundary_count >= 2,
            "global truth must count target/epoch/loss boundaries");
    std::cout << "[W5 PASS] global_actuator_truth total="
              << truth.total_applied_displacement_px.x
              << " pre_acquisition="
              << truth.pre_acquisition_carry_displacement_px.x
              << " targetless="
              << truth.targetless_applied_displacement_px.x
              << " targetless_after_acquisition="
              << truth.targetless_after_acquisition_carry_displacement_px.x
              << " cross_boundary_old_owner="
              << truth.cross_boundary_old_owner_displacement_px.x
              << " cross_boundary_old_owner_abs="
              << truth.cross_boundary_old_owner_abs_displacement_px
              << " boundaries=" << truth.lifecycle_boundary_count << "\n";
}

}  // namespace

int main() {
    try {
        test_runner_executes_exact_duration_and_preserves_identity();
        test_miss_respects_deadline_and_tracking_is_exactly_1000ms();
        test_virtual_camera_x_and_y_signs_close_error();
        test_virtual_camera_can_delay_delivered_control_without_delaying_controller();
        test_virtual_camera_applies_configured_forward_response_curve();
        test_full_speed_left_strafe_moves_relative_error_with_inertia();
        test_slide_and_jump_shift_vertical_error_and_emit_audit_metrics();
        test_default_simulation_mode_is_explicit_off();
        test_closed_loop_score_ordering();
        test_same_script_is_reused_for_pure_and_mixed_runs();
        test_scripted_manual_input_is_controller_independent();
        test_manual_recovery_profiles_expose_expected_trajectory();
        test_manual_input_scale_changes_only_scripted_right_stick();
        test_fixed_target_slot_preserves_wall_clock_spawn_times();
        test_mixed_profile_contains_polar_component_errors();
        test_despawn_publishes_a_fresh_empty_observation();
        test_run_end_does_not_turn_partial_acquisition_into_a_miss();
        test_bodylock_cohort_scores_only_after_confirmed_mode_entry();
        test_bodylock_cohort_fails_when_mode_never_enters();
        test_bodylock_warmup_is_stationary_and_manual_neutral();
        test_compound_bodylock_motion_turns_twice_after_clean_entry();
        test_plan_diagnostics_and_ads_handoff_reach_the_scorer();
        test_ads_handoff_on_acquisition_boundary_is_not_lost();
        test_trace_is_deterministic_and_observational();
        test_obsolete_manual_profile_persists_after_center_crossing();
        test_short_occlusion_withholds_publication_without_fresh_miss();
        test_camera_recoil_moves_true_error_but_visual_kick_does_not();
        test_horizontal_aim_bias_is_observation_only_and_recovers();
        test_micro_input_dropout_decoy_keeps_ads_held_and_publishes_decoy();
        test_known_plant_gate_matrix_and_state_dependent_response();
        test_w5_adversarial_lifecycle_and_delivery_measurements();
        test_actual_delayed_plant_truth_contracts();
        test_w5_delivery_partition_and_negative_controls();
        test_global_actuator_truth_preserves_cross_boundary_carry();
        std::cout << "cod_native_sustained_aimlab_simulator_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_simulator_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
