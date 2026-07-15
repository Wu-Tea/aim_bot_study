#include "left_stick_motion_defect_benchmark.h"

#include "native_gamepad_controller.h"
#include "runtime_config.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <ostream>
#include <set>
#include <vector>

namespace controller_native::left_stick_defect {
namespace {

constexpr double kDtSeconds = 0.01;
constexpr double kPi = 3.14159265358979323846;
constexpr double kBodyHeightPx = 160.0;
constexpr double kReticleSpeedPxPerSecond = 1500.0;
constexpr int kTicksPerScenario = 360;
constexpr int kVisionIntervalTicks = 2;
constexpr int kVisionDelayTicks = 3;
constexpr int kEvaluationStartTick = 70;

enum class TargetRelation {
    Stationary,
    SameDirection,
    OppositeDirection,
};

struct ClosedLoopFixture {
    const char* name;
    const char* mobility;
    TargetRelation relation;
    double player_top_speed_body_s;
    double player_time_constant_s;
    bool manual_correction;
};

constexpr std::array<ClosedLoopFixture, 5> kClosedLoopFixtures = {{
    {"stationary_target_slow_ads", "slow", TargetRelation::Stationary, 0.78, 0.18, false},
    {"stationary_target_fast_ads", "fast", TargetRelation::Stationary, 1.45, 0.10, false},
    {"same_direction_target_fast_ads", "fast", TargetRelation::SameDirection, 1.45, 0.10, false},
    {"opposite_direction_target_fast_ads", "fast", TargetRelation::OppositeDirection, 1.45, 0.10, false},
    {"stationary_target_fast_ads_manual_correction", "fast", TargetRelation::Stationary, 1.45, 0.10, true},
}};

struct PendingVision {
    int delivery_tick = 0;
    double capture_time_seconds = 0.0;
    double error_x_px = 0.0;
};

float left_profile_for_tick(int tick) {
    if (tick >= 80 && tick < 180) {
        return 0.80f;
    }
    if (tick >= 180 && tick < 280) {
        return -0.80f;
    }
    return 0.0f;
}

bool is_phase_event_sample(int tick) {
    for (const int boundary : {80, 180, 280}) {
        if (std::abs(tick - boundary) <= 2) {
            return true;
        }
    }
    return false;
}

NativeControllerVisionState intent_probe_vision(double now, double error_x) {
    NativeControllerVisionState state;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = false;
    state.target_tier = "observed_strong";
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;
    state.target_x = static_cast<float>(320.0 + error_x);
    state.target_y = 256.0f;
    state.dx = static_cast<float>(error_x);
    state.dy = 0.0f;
    state.has_body_box = true;
    state.body_x1 = state.target_x - 42.0f;
    state.body_x2 = state.target_x + 42.0f;
    state.body_y1 = 176.0f;
    state.body_y2 = 336.0f;
    state.observed_at_seconds = now;
    return state;
}

PhysicalGamepadState intent_probe_input(float left_x, float right_x);

const char* target_relation_name(TargetRelation relation) {
    switch (relation) {
        case TargetRelation::Stationary:
            return "stationary";
        case TargetRelation::SameDirection:
            return "same_direction";
        case TargetRelation::OppositeDirection:
            return "opposite_direction";
    }
    return "unknown";
}

const char* phase_name(int tick) {
    if (tick < 80) {
        return "settle";
    }
    if (tick < 180) {
        return "strafe_right";
    }
    if (tick < 280) {
        return "strafe_left";
    }
    return "release";
}

double clamp_unit(double value) {
    return std::max(-1.0, std::min(1.0, value));
}

double shaped_left(float left_x) {
    constexpr double kDeadzone = 0.08;
    const double magnitude = std::fabs(static_cast<double>(left_x));
    if (magnitude <= kDeadzone) {
        return 0.0;
    }
    const double normalized = std::min(1.0, (magnitude - kDeadzone) / (1.0 - kDeadzone));
    const double shaped = std::pow(normalized, 1.25);
    return left_x < 0.0f ? -shaped : shaped;
}

double percentile95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

double event_peak_error(
    const std::vector<double>& errors,
    int event_tick,
    int window_ticks = 40) {
    double peak = 0.0;
    const int end_tick = std::min(
        static_cast<int>(errors.size()),
        event_tick + window_ticks);
    for (int tick = event_tick; tick < end_tick; ++tick) {
        peak = std::max(peak, errors[static_cast<std::size_t>(tick)]);
    }
    return peak;
}

double event_response_latency_ms(
    const std::vector<double>& outputs,
    const std::vector<double>& oracle_outputs,
    int event_tick) {
    if (event_tick <= 0 || event_tick + 10 >= static_cast<int>(outputs.size())) {
        return -1.0;
    }
    const double baseline = outputs[static_cast<std::size_t>(event_tick - 1)];
    const double desired_change =
        oracle_outputs[static_cast<std::size_t>(event_tick + 10)] - baseline;
    if (std::fabs(desired_change) < 0.02) {
        return -1.0;
    }
    const double direction = desired_change < 0.0 ? -1.0 : 1.0;
    const double threshold = std::min(0.03, std::fabs(desired_change) * 0.50);
    const int end_tick = std::min(
        static_cast<int>(outputs.size()),
        event_tick + 31);
    for (int tick = event_tick; tick < end_tick; ++tick) {
        const double moved =
            (outputs[static_cast<std::size_t>(tick)] - baseline) * direction;
        if (moved >= threshold) {
            return static_cast<double>(tick - event_tick) * kDtSeconds * 1000.0;
        }
    }
    return static_cast<double>(end_tick - event_tick) * kDtSeconds * 1000.0;
}

bool should_record_event(int tick) {
    for (const int boundary : {80, 180, 280}) {
        if (tick >= boundary - 5 && tick <= boundary + 15) {
            return true;
        }
    }
    return tick % 60 == 0 || tick == kTicksPerScenario - 1;
}

ScenarioMetrics run_closed_loop_fixture(const ClosedLoopFixture& fixture) {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;

    double now = 1.0;
    NativeGamepadController controller(config, [&now] { return now; });
    std::deque<PendingVision> pending_vision;

    ScenarioMetrics metrics;
    metrics.name = fixture.name;
    metrics.mobility = fixture.mobility;
    metrics.target_relation = target_relation_name(fixture.relation);
    metrics.body_height_px = kBodyHeightPx;

    double true_error_px = 44.0;
    double last_observed_error_px = true_error_px;
    double last_observed_capture_time_seconds = 1.0;
    double player_velocity_body_s = 0.0;
    double target_velocity_body_s = 0.0;
    double previous_final_output = 0.0;
    bool has_previous_output = false;

    std::vector<double> abs_errors;
    std::vector<double> evaluation_errors;
    std::vector<double> final_outputs;
    std::vector<double> oracle_outputs;
    abs_errors.reserve(kTicksPerScenario);
    evaluation_errors.reserve(kTicksPerScenario - kEvaluationStartTick);
    final_outputs.reserve(kTicksPerScenario);
    oracle_outputs.reserve(kTicksPerScenario);

    for (int tick = 0; tick < kTicksPerScenario; ++tick) {
        now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const float left_x = left_profile_for_tick(tick);
        const double shaped_input = shaped_left(left_x);
        const double desired_player_velocity =
            fixture.player_top_speed_body_s * shaped_input;
        const double player_alpha = 1.0 - std::exp(
            -kDtSeconds / fixture.player_time_constant_s);
        player_velocity_body_s +=
            player_alpha * (desired_player_velocity - player_velocity_body_s);

        double desired_target_velocity = 0.0;
        if (fixture.relation == TargetRelation::SameDirection) {
            desired_target_velocity =
                fixture.player_top_speed_body_s * shaped_input * 0.58;
        } else if (fixture.relation == TargetRelation::OppositeDirection) {
            desired_target_velocity =
                -fixture.player_top_speed_body_s * shaped_input * 0.58;
        }
        constexpr double kTargetTimeConstantSeconds = 0.14;
        const double target_alpha =
            1.0 - std::exp(-kDtSeconds / kTargetTimeConstantSeconds);
        target_velocity_body_s +=
            target_alpha * (desired_target_velocity - target_velocity_body_s);

        const double relative_velocity_px_s =
            (target_velocity_body_s - player_velocity_body_s) * kBodyHeightPx;
        true_error_px += relative_velocity_px_s * kDtSeconds;

        if (tick % kVisionIntervalTicks == 0) {
            pending_vision.push_back(PendingVision{
                tick + kVisionDelayTicks,
                now,
                true_error_px,
            });
        }
        while (!pending_vision.empty() &&
               pending_vision.front().delivery_tick <= tick) {
            const PendingVision vision = pending_vision.front();
            pending_vision.pop_front();
            last_observed_error_px = vision.error_x_px;
            last_observed_capture_time_seconds = vision.capture_time_seconds;
            controller.submit_vision_state(intent_probe_vision(
                vision.capture_time_seconds,
                vision.error_x_px));
        }

        const double oracle_output = clamp_unit(
            (relative_velocity_px_s + (true_error_px * 7.0)) /
            kReticleSpeedPxPerSecond);
        const float manual_right = fixture.manual_correction
            ? static_cast<float>(std::clamp(oracle_output * 0.72, -0.24, 0.24))
            : 0.0f;
        const GamepadOutputState output = controller.build_output(
            intent_probe_input(left_x, manual_right));
        const NativeControllerOutputComponents& components =
            controller.last_output_components();
        const double ai_output = components.ai_aim_stick.x;
        const double final_output = output.right_x;

        const bool evaluation_tick = tick >= kEvaluationStartTick;
        if (evaluation_tick && std::fabs(ai_output) >= 0.28) {
            ++metrics.high_ai_output_frames;
        }
        if (evaluation_tick && std::fabs(manual_right) >= 0.05 &&
            std::fabs(ai_output) >= 0.05 &&
            static_cast<double>(manual_right) * ai_output < 0.0) {
            ++metrics.ai_opposes_manual_frames;
        }
        if (evaluation_tick && std::fabs(oracle_output) >= 0.05 &&
            std::fabs(final_output) >= 0.05 &&
            oracle_output * final_output < 0.0) {
            ++metrics.final_opposes_oracle_frames;
        }
        if (has_previous_output && evaluation_tick) {
            const double output_delta = std::fabs(final_output - previous_final_output);
            metrics.max_final_output_delta = std::max(
                metrics.max_final_output_delta,
                output_delta);
            if (output_delta >= 0.20) {
                ++metrics.output_spike_frames;
            }
        }
        previous_final_output = final_output;
        has_previous_output = true;

        if (evaluation_tick) {
            metrics.max_ai_output = std::max(metrics.max_ai_output, std::fabs(ai_output));
            metrics.max_final_output = std::max(
                metrics.max_final_output,
                std::fabs(final_output));
        }

        true_error_px -=
            final_output * kReticleSpeedPxPerSecond * kDtSeconds;
        const double abs_error = std::fabs(true_error_px);
        abs_errors.push_back(abs_error);
        if (evaluation_tick) {
            evaluation_errors.push_back(abs_error);
        }
        final_outputs.push_back(final_output);
        oracle_outputs.push_back(oracle_output);
        if (evaluation_tick && abs_error <= 8.0) {
            ++metrics.settled_frames;
        }

        if (should_record_event(tick)) {
            PhaseEvent event;
            event.phase = phase_name(tick);
            event.tick = tick;
            event.time_seconds = now;
            event.left_x = left_x;
            event.manual_right_x = manual_right;
            event.ai_right_x = static_cast<float>(ai_output);
            event.final_right_x = static_cast<float>(final_output);
            event.oracle_right_x = static_cast<float>(oracle_output);
            event.observed_error_px = last_observed_error_px;
            event.true_error_px = true_error_px;
            event.vision_age_ms =
                (now - last_observed_capture_time_seconds) * 1000.0;
            event.player_velocity_body_s = player_velocity_body_s;
            event.target_velocity_body_s = target_velocity_body_s;
            event.aim_mode = controller.last_ai_aim_mode();
            metrics.events.push_back(event);
        }
    }

    metrics.behavior_populated = true;
    metrics.mean_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : std::accumulate(evaluation_errors.begin(), evaluation_errors.end(), 0.0) /
            static_cast<double>(evaluation_errors.size());
    metrics.p95_abs_error_px = percentile95(evaluation_errors);
    metrics.max_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : *std::max_element(evaluation_errors.begin(), evaluation_errors.end());
    metrics.final_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : evaluation_errors.back();
    metrics.onset_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, 80);
    metrics.reversal_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, 180);
    metrics.release_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, 280);
    metrics.onset_peak_error_px = event_peak_error(abs_errors, 80);
    metrics.reversal_peak_error_px = event_peak_error(abs_errors, 180);
    metrics.release_peak_error_px = event_peak_error(abs_errors, 280);

    if (metrics.onset_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("onset_response_lag");
    }
    if (metrics.reversal_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("reversal_response_lag");
    }
    if (metrics.release_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("release_response_lag");
    }
    if (metrics.ai_opposes_manual_frames > 0) {
        metrics.defect_reasons.push_back("ai_opposes_manual_correction");
    }
    if (metrics.final_opposes_oracle_frames > 0) {
        metrics.defect_reasons.push_back("final_output_opposes_oracle");
    }
    if (metrics.output_spike_frames > 0) {
        metrics.defect_reasons.push_back("post_acquisition_output_spike");
    }
    if (metrics.max_abs_error_px > 55.0) {
        metrics.defect_reasons.push_back("excessive_tracking_error");
    }
    metrics.desired_gate_pass = metrics.defect_reasons.empty();
    metrics.defect_reproduced = !metrics.desired_gate_pass;
    return metrics;
}

PhysicalGamepadState intent_probe_input(float left_x, float right_x) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.left_x = left_x;
    state.right_x = right_x;
    return state;
}

IntentInvarianceMetrics run_intent_invariance_probe() {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    config.ai_aim.ads_snap_window_ms = 0;

    double now = 1.0;
    NativeGamepadController active_left(config, [&now] { return now; });
    NativeGamepadController neutral_left(config, [&now] { return now; });
    IntentInvarianceMetrics metrics;

    for (int tick = 0; tick < 360; ++tick) {
        now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const double seconds = static_cast<double>(tick) * kDtSeconds;
        const double error_x =
            38.0 + (42.0 * std::sin(seconds * kPi * 0.85));
        if (tick % 2 == 0) {
            const NativeControllerVisionState vision = intent_probe_vision(now, error_x);
            active_left.submit_vision_state(vision);
            neutral_left.submit_vision_state(vision);
        }

        const float manual_right = static_cast<float>(
            std::clamp(error_x / 420.0, -0.16, 0.16));
        const float left_x = left_profile_for_tick(tick);
        const GamepadOutputState active_output =
            active_left.build_output(intent_probe_input(left_x, manual_right));
        const GamepadOutputState neutral_output =
            neutral_left.build_output(intent_probe_input(0.0f, manual_right));
        const NativeControllerOutputComponents& active_components =
            active_left.last_output_components();
        const NativeControllerOutputComponents& neutral_components =
            neutral_left.last_output_components();

        metrics.max_ai_trace_delta = std::max(
            metrics.max_ai_trace_delta,
            std::fabs(
                static_cast<double>(active_components.ai_aim_stick.x) -
                static_cast<double>(neutral_components.ai_aim_stick.x)));
        metrics.max_final_right_trace_delta = std::max(
            metrics.max_final_right_trace_delta,
            std::fabs(
                static_cast<double>(active_output.right_x) -
                static_cast<double>(neutral_output.right_x)));
        metrics.max_left_passthrough_error = std::max(
            metrics.max_left_passthrough_error,
            std::fabs(
                static_cast<double>(active_output.left_x) -
                static_cast<double>(left_x)));
        if (is_phase_event_sample(tick)) {
            ++metrics.phase_event_samples;
        }
    }

    constexpr double kIntentSensitivityEpsilon = 1.0e-6;
    metrics.left_intent_ignored =
        metrics.phase_event_samples > 0 &&
        metrics.max_ai_trace_delta <= kIntentSensitivityEpsilon &&
        metrics.max_final_right_trace_delta <= kIntentSensitivityEpsilon;
    metrics.desired_gate_pass = !metrics.left_intent_ignored;
    return metrics;
}

void write_bool(std::ostream& output, bool value) {
    output << (value ? "true" : "false");
}

}  // namespace

BenchmarkReport run_benchmark() {
    BenchmarkReport report;
    report.intent_invariance = run_intent_invariance_probe();
    report.scenarios.reserve(kClosedLoopFixtures.size());
    for (const ClosedLoopFixture& fixture_config : kClosedLoopFixtures) {
        report.scenarios.push_back(run_closed_loop_fixture(fixture_config));
    }
    if (report.intent_invariance.left_intent_ignored) {
        ++report.defect_count;
    }
    report.desired_gate_pass = report.intent_invariance.desired_gate_pass;
    for (const ScenarioMetrics& scenario : report.scenarios) {
        if (scenario.defect_reproduced) {
            ++report.defect_count;
        }
        report.desired_gate_pass =
            report.desired_gate_pass && scenario.desired_gate_pass;
    }
    return report;
}

bool validate_report(const BenchmarkReport& report, std::string* reason) {
    const auto fail = [&](const char* message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };

    if (report.schema_version != 1) {
        return fail("unexpected schema version");
    }
    if (report.controller_hz != 100 || report.vision_hz != 50 ||
        report.vision_delay_ms != 30 || report.ticks_per_scenario != 360 ||
        report.evaluation_start_tick != kEvaluationStartTick) {
        return fail("unexpected deterministic timing contract");
    }
    if (report.scenarios.size() != kClosedLoopFixtures.size()) {
        return fail("expected five closed-loop scenarios");
    }
    if (report.intent_invariance.phase_event_samples != 15) {
        return fail("intent probe did not sample every phase boundary");
    }
    if (report.intent_invariance.max_left_passthrough_error > 1.0e-6) {
        return fail("left output passthrough did not match physical input");
    }

    std::set<std::string> names;
    for (const ScenarioMetrics& scenario : report.scenarios) {
        if (scenario.name.empty() || scenario.body_height_px <= 0.0 ||
            !scenario.behavior_populated || scenario.events.empty()) {
            return fail("invalid scenario fixture");
        }
        for (const double value : {
                 scenario.mean_abs_error_px,
                 scenario.p95_abs_error_px,
                 scenario.max_abs_error_px,
                 scenario.final_abs_error_px,
                 scenario.max_ai_output,
                 scenario.max_final_output,
                 scenario.max_final_output_delta}) {
            if (!std::isfinite(value)) {
                return fail("scenario contains non-finite metric");
            }
        }
        names.insert(scenario.name);
    }
    int expected_defect_count = report.intent_invariance.left_intent_ignored ? 1 : 0;
    bool expected_gate_pass = report.intent_invariance.desired_gate_pass;
    for (const ScenarioMetrics& scenario : report.scenarios) {
        if (scenario.defect_reproduced) {
            ++expected_defect_count;
        }
        expected_gate_pass = expected_gate_pass && scenario.desired_gate_pass;
    }
    if (report.defect_count != expected_defect_count ||
        report.desired_gate_pass != expected_gate_pass) {
        return fail("report summary does not match scenario gates");
    }
    for (const ClosedLoopFixture& expected : kClosedLoopFixtures) {
        if (names.find(expected.name) == names.end()) {
            return fail("missing required scenario fixture");
        }
    }
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

void write_json(std::ostream& output, const BenchmarkReport& report) {
    output << "{\n"
           << "  \"schema_version\": " << report.schema_version << ",\n"
           << "  \"controller_hz\": " << report.controller_hz << ",\n"
           << "  \"vision_hz\": " << report.vision_hz << ",\n"
           << "  \"vision_delay_ms\": " << report.vision_delay_ms << ",\n"
           << "  \"ticks_per_scenario\": " << report.ticks_per_scenario << ",\n"
           << "  \"evaluation_start_tick\": " << report.evaluation_start_tick << ",\n"
           << "  \"intent_invariance\": {\n"
           << "    \"max_ai_trace_delta\": "
           << report.intent_invariance.max_ai_trace_delta << ",\n"
           << "    \"max_final_right_trace_delta\": "
           << report.intent_invariance.max_final_right_trace_delta << ",\n"
           << "    \"max_left_passthrough_error\": "
           << report.intent_invariance.max_left_passthrough_error << ",\n"
           << "    \"phase_event_samples\": "
           << report.intent_invariance.phase_event_samples << ",\n"
           << "    \"left_intent_ignored\": ";
    write_bool(output, report.intent_invariance.left_intent_ignored);
    output << ",\n    \"desired_gate_pass\": ";
    write_bool(output, report.intent_invariance.desired_gate_pass);
    output << "\n  },\n"
           << "  \"closed_loop_behavior_populated\": true,\n"
           << "  \"defect_count\": " << report.defect_count << ",\n"
           << "  \"desired_gate_pass\": ";
    write_bool(output, report.desired_gate_pass);
    output << ",\n  \"scenarios\": [\n";
    for (std::size_t index = 0; index < report.scenarios.size(); ++index) {
        const ScenarioMetrics& scenario = report.scenarios[index];
        output << "    {\n"
               << "      \"name\": \"" << scenario.name << "\",\n"
               << "      \"mobility\": \"" << scenario.mobility << "\",\n"
               << "      \"target_relation\": \"" << scenario.target_relation << "\",\n"
               << "      \"body_height_px\": " << scenario.body_height_px << ",\n"
               << "      \"mean_abs_error_px\": " << scenario.mean_abs_error_px << ",\n"
               << "      \"p95_abs_error_px\": " << scenario.p95_abs_error_px << ",\n"
               << "      \"max_abs_error_px\": " << scenario.max_abs_error_px << ",\n"
               << "      \"final_abs_error_px\": " << scenario.final_abs_error_px << ",\n"
               << "      \"max_ai_output\": " << scenario.max_ai_output << ",\n"
               << "      \"max_final_output\": " << scenario.max_final_output << ",\n"
               << "      \"max_final_output_delta\": " << scenario.max_final_output_delta << ",\n"
               << "      \"onset_response_latency_ms\": " << scenario.onset_response_latency_ms << ",\n"
               << "      \"reversal_response_latency_ms\": " << scenario.reversal_response_latency_ms << ",\n"
               << "      \"release_response_latency_ms\": " << scenario.release_response_latency_ms << ",\n"
               << "      \"onset_peak_error_px\": " << scenario.onset_peak_error_px << ",\n"
               << "      \"reversal_peak_error_px\": " << scenario.reversal_peak_error_px << ",\n"
               << "      \"release_peak_error_px\": " << scenario.release_peak_error_px << ",\n"
               << "      \"ai_opposes_manual_frames\": " << scenario.ai_opposes_manual_frames << ",\n"
               << "      \"final_opposes_oracle_frames\": " << scenario.final_opposes_oracle_frames << ",\n"
               << "      \"high_ai_output_frames\": " << scenario.high_ai_output_frames << ",\n"
               << "      \"output_spike_frames\": " << scenario.output_spike_frames << ",\n"
               << "      \"settled_frames\": " << scenario.settled_frames << ",\n"
               << "      \"defect_reproduced\": ";
        write_bool(output, scenario.defect_reproduced);
        output << ",\n      \"desired_gate_pass\": ";
        write_bool(output, scenario.desired_gate_pass);
        output << ",\n      \"defect_reasons\": [";
        for (std::size_t reason_index = 0;
             reason_index < scenario.defect_reasons.size();
             ++reason_index) {
            output << "\"" << scenario.defect_reasons[reason_index] << "\"";
            if (reason_index + 1 != scenario.defect_reasons.size()) {
                output << ", ";
            }
        }
        output << "],\n      \"events\": [\n";
        for (std::size_t event_index = 0; event_index < scenario.events.size(); ++event_index) {
            const PhaseEvent& event = scenario.events[event_index];
            output << "        {\"phase\": \"" << event.phase
                   << "\", \"tick\": " << event.tick
                   << ", \"time_seconds\": " << event.time_seconds
                   << ", \"left_x\": " << event.left_x
                   << ", \"manual_right_x\": " << event.manual_right_x
                   << ", \"ai_right_x\": " << event.ai_right_x
                   << ", \"final_right_x\": " << event.final_right_x
                   << ", \"oracle_right_x\": " << event.oracle_right_x
                   << ", \"observed_error_px\": " << event.observed_error_px
                   << ", \"true_error_px\": " << event.true_error_px
                   << ", \"vision_age_ms\": " << event.vision_age_ms
                   << ", \"player_velocity_body_s\": " << event.player_velocity_body_s
                   << ", \"target_velocity_body_s\": " << event.target_velocity_body_s
                   << ", \"aim_mode\": \"" << event.aim_mode << "\"}";
            output << (event_index + 1 == scenario.events.size() ? "\n" : ",\n");
        }
        output << "      ]\n    }";
        output << (index + 1 == report.scenarios.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

}  // namespace controller_native::left_stick_defect
