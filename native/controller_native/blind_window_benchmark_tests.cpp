#include "blind_window_benchmark.h"
#include "blind_window_metrics.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace controller_native::blind_window;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance = 1.0e-9) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(
            "expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
    }
}

BlindWindowTrace constant_error_trace(double error_x, int duration_ms) {
    BlindWindowTrace trace;
    for (int ms = 0; ms < duration_ms; ++ms) {
        BlindTraceFrame frame;
        frame.now_us = ms * 1'000;
        frame.true_error_px = {error_x, 0.0};
        trace.frames.push_back(frame);
    }
    return trace;
}

void test_harmful_pending_counts_opposing_and_excess_motion() {
    const Vec2d error{8.0, 0.0};
    require_near(harmful_pending_at_reveal(error, {-3.0, 0.0}), 3.0);
    require_near(harmful_pending_at_reveal(error, {12.0, 0.0}), 4.0);
    require_near(harmful_pending_at_reveal(error, {5.0, 0.0}), 0.0);
}

void test_future_burden_keeps_px_ms_units() {
    const BlindWindowTrace trace = constant_error_trace(10.0, 80);
    require_near(future_error_burden(trace, 0, 80), 800.0);
}

void test_user_fight_ignores_neutral_drift() {
    BlindWindowTrace trace;
    BlindTraceFrame frame;
    frame.now_us = 0;
    frame.manual_stick = {0.02, 0.0};
    frame.ai_stick = {-1.0, 0.0};
    trace.frames.push_back(frame);
    require_near(user_fight_area(trace, 0.03), 0.0);
}

void test_user_fight_integrates_opposing_vectors() {
    BlindWindowTrace trace;
    for (int ms = 0; ms < 10; ++ms) {
        BlindTraceFrame frame;
        frame.now_us = ms * 1'000;
        frame.manual_stick = {0.5, 0.0};
        frame.ai_stick = {-0.25, 0.0};
        trace.frames.push_back(frame);
    }
    require_near(user_fight_area(trace, 0.03), 2.5);
}

void test_metric_contract_rejects_non_finite_values() {
    BlindWindowMetrics metrics;
    require(finite(metrics), "zero-initialized metrics must be finite");
    metrics.harmful_ai_motion_px = std::nan("");
    require(!finite(metrics), "NaN metric must fail the report contract");
}

BlindFixture minimal_fixture() {
    BlindFixture fixture;
    fixture.name = "minimal";
    fixture.duration_us = 50'000;
    fixture.initial_error_px = {20.0, 0.0};
    fixture.right_response_px_per_stick_second = {
        1'000.0, 0.0, 0.0, 1'000.0};
    return fixture;
}

BlindControllerStep zero_controller() {
    return [](const BlindControllerObservation&) {
        return BlindControllerOutput{};
    };
}

BlindControllerStep constant_x_controller(double value) {
    return [=](const BlindControllerObservation&) {
        BlindControllerOutput output;
        output.ai_stick = {value, 0.0};
        output.final_stick = output.ai_stick;
        return output;
    };
}

void test_event_occurs_at_requested_capture_phase() {
    BlindTimingProfile timing;
    timing.vision_period_us = 10'000;
    timing.event_phase_per_mille = 250;
    timing.result_latency_us = 16'000;
    timing.response_delay_us = 45'000;
    const BlindSchedule schedule = build_blind_schedule(timing, 0, 50'000);
    require(schedule.event_at_us == 2'500,
            "event must use the requested capture phase");
    require(schedule.next_capture_at_us == 10'000,
            "next capture must retain the Vision period");
    require(schedule.next_result_at_us == 26'000,
            "result availability must be capture plus latency");
}

void test_controller_never_receives_future_capture() {
    const BlindWindowRun run = run_blind_fixture(
        minimal_fixture(), zero_controller());
    for (const BlindTraceFrame& frame : run.trace.frames) {
        require(frame.max_controller_source_time_us <= frame.now_us,
                "controller cannot consume a future capture");
    }
}

void test_delivered_input_changes_plant_only_after_response_delay() {
    BlindFixture fixture = minimal_fixture();
    fixture.timing.response_delay_us = 25'000;
    const BlindWindowRun run = run_blind_fixture(
        fixture, constant_x_controller(1.0));
    require_near(run.frame_at_us(24'000).true_error_px.x, 20.0);
    require(run.frame_at_us(26'000).true_error_px.x < 20.0,
            "delivered input must affect the plant after its response delay");
}

}  // namespace

int main() {
    try {
        test_harmful_pending_counts_opposing_and_excess_motion();
        test_future_burden_keeps_px_ms_units();
        test_user_fight_ignores_neutral_drift();
        test_user_fight_integrates_opposing_vectors();
        test_metric_contract_rejects_non_finite_values();
        test_event_occurs_at_requested_capture_phase();
        test_controller_never_receives_future_capture();
        test_delivered_input_changes_plant_only_after_response_delay();
        std::cout << "blind_window_benchmark_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "blind_window_benchmark_tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
