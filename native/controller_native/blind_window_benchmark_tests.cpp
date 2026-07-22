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

}  // namespace

int main() {
    try {
        test_harmful_pending_counts_opposing_and_excess_motion();
        test_future_burden_keeps_px_ms_units();
        test_user_fight_ignores_neutral_drift();
        test_user_fight_integrates_opposing_vectors();
        test_metric_contract_rejects_non_finite_values();
        std::cout << "blind_window_benchmark_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "blind_window_benchmark_tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
