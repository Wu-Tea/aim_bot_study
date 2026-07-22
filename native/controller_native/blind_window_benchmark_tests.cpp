#include "blind_window_benchmark.h"
#include "blind_window_baseline.h"
#include "blind_window_fixtures.h"
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

void test_fractional_millisecond_vision_period_keeps_requested_rate() {
    BlindFixture fixture;
    fixture.duration_us = 101'000;
    fixture.timing.vision_period_us = 8'333;
    fixture.timing.result_latency_us = 0;
    fixture.timing.response_delay_us = 0;
    int fresh_results = 0;
    run_blind_fixture(
        fixture,
        [&fresh_results](const BlindControllerObservation& observation) {
            if (observation.fresh_vision) ++fresh_results;
            return BlindControllerOutput{};
        });
    require(fresh_results >= 12 && fresh_results <= 13,
            "120 Hz capture must publish about 12 results in 100 ms");
}

void test_k1_fixture_crosses_during_blind_window_with_stale_controller() {
    const BlindWindowRun run = run_blind_fixture(
        bodylock_pending_crossing_fixture(1337, timing_100hz_phase_5()),
        stale_proportional_controller());
    require(run.metrics.harmful_pending_at_reveal_px > 0.5,
            "K1 must expose excess or opposing pending at reveal");
    require(run.metrics.reverse_correction_80_stick_ms > 0.0,
            "K1 must require a post-reveal reverse correction");
    require(run.metrics.future_burden_80_px_ms > 0.0,
            "K1 must retain measurable future error burden");
}

void test_k1_fixture_contains_no_target_surprise() {
    const BlindFixture fixture = bodylock_pending_crossing_fixture(
        1337, timing_100hz_phase_5());
    require(fixture.knowledge_class == BlindKnowledgeClass::SelfPredictable,
            "K1 fixture must be self-predictable");
    require_near(fixture.target_acceleration_px_per_sec2.x, 0.0);
    require_near(fixture.target_acceleration_px_per_sec2.y, 0.0);
    require_near(fixture.target_velocity_px_per_second.x, 0.0);
    require_near(fixture.target_velocity_px_per_second.y, 0.0);
}

void test_k1_phase_changes_reveal_debt() {
    BlindTimingProfile early = timing_100hz_phase_5();
    BlindTimingProfile late = early;
    late.event_phase_per_mille = 950;
    const BlindWindowRun early_run = run_blind_fixture(
        bodylock_pending_crossing_fixture(1337, early),
        stale_proportional_controller());
    const BlindWindowRun late_run = run_blind_fixture(
        bodylock_pending_crossing_fixture(1337, late),
        stale_proportional_controller());
    require(early_run.metrics.harmful_pending_at_reveal_px >
                late_run.metrics.harmful_pending_at_reveal_px + 0.25,
            "capture phase must materially change reveal debt");
}

void test_k1_baseline_matrix_is_complete_and_discriminating() {
    const BlindBaselineSummary summary = run_k1_baseline_matrix();
    require(summary.episodes.size() == 675,
            "K1 baseline must cover 3x3x5x3x5 combinations");
    require(summary.baseline_discriminating,
            "K1 baseline must expose harmful pending in the required matrix");
    for (const BlindEpisodeSummary& episode : summary.episodes) {
        require(finite(episode.metrics),
                "every K1 episode must contain finite metrics");
        require(episode.metrics.future_dependency_violations == 0,
                "K1 matrix must remain causal");
    }
}

void test_k1_baseline_json_retains_provenance_and_metrics() {
    BlindBaselineSummary summary;
    summary.baseline_discriminating = true;
    BlindEpisodeSummary episode;
    episode.seed = 1337;
    episode.vision_hz = 100;
    episode.phase_percent = 5;
    episode.result_latency_ms = 16;
    episode.response_delay_ms = 45;
    episode.metrics.harmful_pending_at_reveal_px = 2.5;
    summary.episodes.push_back(episode);
    const std::string json = serialize_k1_baseline_json(
        summary, "abc123", false);
    require(json.find("\"fixture_semantics_version\":1") != std::string::npos,
            "JSON must retain fixture semantics");
    require(json.find("\"revision\":\"abc123\"") != std::string::npos,
            "JSON must retain revision provenance");
    require(json.find("\"harmful_pending_at_reveal_px\":2.5") !=
                std::string::npos,
            "JSON must retain raw primary metrics");
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
        test_fractional_millisecond_vision_period_keeps_requested_rate();
        test_k1_fixture_crosses_during_blind_window_with_stale_controller();
        test_k1_fixture_contains_no_target_surprise();
        test_k1_phase_changes_reveal_debt();
        test_k1_baseline_matrix_is_complete_and_discriminating();
        test_k1_baseline_json_retains_provenance_and_metrics();
        std::cout << "blind_window_benchmark_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "blind_window_benchmark_tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
