#include "controller_native/output_diagnostics.h"
#include "test_support/native_test_registry.h"
#include "../replay_native/replay_metrics.h"
#include "../replay_native/replay_schema.h"
#include "../runtime_app/perf_logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::ostringstream out;
        out << message << " expected=" << expected << " actual=" << actual;
        throw std::runtime_error(out.str());
    }
}

std::filesystem::path make_temp_test_dir(const std::string& label) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("cod_native_benchmark_metrics_" + label + "_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read test file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void test_replay_schema_captures_controller_components() {
    replay_native::NativeReplayFrame frame;
    frame.frame_id = 42;
    frame.timing.capture_time_seconds = 100.0;
    frame.timing.vision_ready_time_seconds = 100.006;
    frame.selected_target.has_target = true;
    frame.selected_target.aim_error_px = {12.0f, -4.0f};
    frame.selected_target.tier = "strong";
    frame.controller.aiming = true;
    frame.controller.sticks.manual = {0.10f, 0.20f};
    frame.controller.sticks.assist = {0.30f, 0.00f};
    frame.controller.sticks.recoil = {-0.40f, 0.00f};
    frame.controller.sticks.final_output = {0.00f, 0.20f};

    require_true(frame.controller.aiming, "replay schema should capture aiming state");
    require_near(
        frame.controller.sticks.recoil.x,
        -0.40f,
        0.001f,
        "replay schema should expose recoil stick separately");
    require_near(
        frame.controller.sticks.final_output.x,
        0.00f,
        0.001f,
        "replay schema should expose final stick separately");
}

void test_replay_metrics_summarizes_error_and_stale_fire_violations() {
    std::vector<replay_native::NativeReplayFrame> frames(3);
    frames[0].selected_target.has_target = true;
    frames[0].selected_target.aim_error_px = {10.0f, 0.0f};
    frames[1].selected_target.has_target = true;
    frames[1].selected_target.aim_error_px = {20.0f, 0.0f};

    frames[2].selected_target.has_target = true;
    frames[2].selected_target.aim_error_px = {30.0f, 0.0f};
    frames[2].selected_target.age_ms = 60.0;
    frames[2].controller.fire_allowed = true;

    replay_native::ReplayMetricOptions options;
    options.max_fire_source_age_ms = 50.0;
    const replay_native::ReplayMetricSummary summary =
        replay_native::summarize_replay_metrics(frames, options);

    require_near(summary.target_error_p50_px, 20.0f, 0.001f, "replay p50 target error");
    require_near(summary.target_error_p95_px, 30.0f, 0.001f, "replay p95 target error");
    require_true(
        summary.stale_fire_violations == 1,
        "replay metrics should count stale fire violations");
}

void test_replay_metrics_exposes_bodylock_continuity_defect() {
    std::vector<replay_native::NativeReplayFrame> frames(24);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        replay_native::NativeReplayFrame& frame = frames[index];
        frame.timing.controller_tick_ms = 1.0;
        frame.controller.aiming = true;
        frame.controller.sticks.manual = {0.80f, 0.0f};
        frame.controller.sticks.assist = {-0.36f, 0.0f};
        frame.controller.sticks.final_output = {0.44f, 0.0f};
    }

    replay_native::ReplayMetricOptions options;
    options.strong_manual_threshold = 0.45;
    options.manual_gain_plateau_ratio = 0.55;
    options.manual_gain_plateau_tolerance = 0.01;
    options.opposing_assist_defect_ms = 16.0;
    options.manual_gain_plateau_defect_ms = 8.0;
    const replay_native::ReplayMetricSummary summary =
        replay_native::summarize_replay_metrics(frames, options);

    require_true(
        summary.strong_manual_axis_samples == 24,
        "continuity metrics should count strong manual axis samples");
    require_true(
        summary.opposing_assist_axis_samples == 24,
        "continuity metrics should count assist opposing stable manual input");
    require_near(
        static_cast<float>(summary.longest_opposing_assist_run_ms),
        24.0f,
        0.001f,
        "continuity metrics should expose the sustained opposing run");
    require_true(
        summary.manual_gain_plateau_axis_samples == 24,
        "continuity metrics should count the observed 55 percent output plateau");
    require_near(
        static_cast<float>(summary.longest_manual_gain_plateau_run_ms),
        24.0f,
        0.001f,
        "continuity metrics should expose the sustained gain plateau");
    require_true(
        summary.bodylock_continuity_defect,
        "the captured bodylock signature should fail the continuity gate");
}

void test_replay_metrics_exposes_stepwise_assist_as_stutter() {
    std::vector<replay_native::NativeReplayFrame> frames(20);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        replay_native::NativeReplayFrame& frame = frames[index];
        const float assist = index % 2 == 0 ? 0.0f : -0.36f;
        frame.timing.controller_tick_ms = 1.0;
        frame.controller.aiming = true;
        frame.controller.sticks.manual = {0.80f, 0.0f};
        frame.controller.sticks.assist = {assist, 0.0f};
        frame.controller.sticks.final_output = {0.80f + assist, 0.0f};
    }

    replay_native::ReplayMetricOptions options;
    options.strong_manual_threshold = 0.45;
    options.assist_step_threshold = 0.10;
    options.final_jerk_threshold = 0.15;
    const replay_native::ReplayMetricSummary summary =
        replay_native::summarize_replay_metrics(frames, options);

    require_true(
        summary.assist_step_events == 19,
        "continuity metrics should count every step in pulsed assist");
    require_true(
        summary.final_jerk_events == 18,
        "continuity metrics should count slope discontinuities in final output");
    require_true(
        summary.assist_delta_p95 >= 0.35,
        "continuity metrics should expose p95 assist steps");
    require_true(
        summary.final_jerk_p95 >= 0.70,
        "continuity metrics should expose p95 final-output jerk");
    require_true(
        summary.bodylock_continuity_defect,
        "stepwise assist over continuous manual input should fail the continuity gate");
}

void test_perf_loop_fps_uses_measured_elapsed_time() {
    require_near(
        static_cast<float>(runtime_app::loop_fps_from_elapsed_ms(2.0)),
        500.0f,
        0.001f,
        "loop fps should be derived from measured loop time");
    require_near(
        static_cast<float>(runtime_app::loop_fps_from_elapsed_ms(0.0)),
        0.0f,
        0.001f,
        "zero elapsed loop time should not synthesize 1000 fps");
}

void test_lightweight_perf_summary_writes_one_compact_window() {
    const std::filesystem::path root = make_temp_test_dir("perf_summary");
    std::filesystem::path log_path;
    {
        runtime_app::PerfSummaryOptions options;
        options.enabled = true;
        options.interval_ms = 1000;
        options.directory = root;
        options.stdout_enabled = false;
        runtime_app::PerfSummaryLogger logger(options);

        runtime_app::PerfControllerWindowSample controller;
        controller.timestamp_ns = 1'000'000'000ull;
        controller.aiming = true;
        controller.output_delivered = true;
        controller.tick_ms = 0.40;
        controller.pipeline_ms = 0.20;
        controller.vigem_ms = 0.05;
        logger.record_controller(controller);

        runtime_app::PerfVisionWindowSample vision;
        vision.aiming = true;
        vision.accumulated_frames = 2;
        vision.capture_to_result_ms = 5.4;
        vision.copy_to_result_ms = 4.8;
        vision.source_present_to_result_ms = 7.9;
        vision.result_to_controller_ms = 0.4;
        vision.source_present_to_vigem_ms = 8.4;
        vision.cuda_map_ms = 0.15;
        vision.preprocess_ms = 0.55;
        vision.infer_ms = 1.40;
        vision.gpu_total_ms = 2.1;
        vision.output_copy_sync_ms = 3.20;
        vision.output_copy_ms = 0.08;
        vision.output_wait_ms = 3.12;
        vision.sync_queue_residual_ms = 1.02;
        vision.color_copy_ms = 0.22;
        vision.cuda_unmap_ms = 0.04;
        logger.record_vision(vision);

        controller.timestamp_ns = 2'100'000'000ull;
        logger.record_controller(controller);
        log_path = logger.log_path();
        logger.stop();
    }

    const std::string log = read_text_file(log_path);
    require_true(
        std::count(log.begin(), log.end(), '\n') == 1,
        "perf summary should write one line per completed window");
    require_true(
        log.find("\"type\":\"runtime_perf_summary\"") != std::string::npos,
        "perf summary should use the compact summary schema");
    require_true(
        log.find("\"histogram_bucket_ms\":0.250") != std::string::npos,
        "perf summary should publish its quantile resolution");
    require_true(
        log.find("\"accumulated_gt1_pct\":100.000") != std::string::npos,
        "perf summary should report accumulated-frame pressure");
    require_true(
        log.find("\"accumulated_active_gt1_pct\":100.000") != std::string::npos,
        "perf summary should separate active accumulation pressure from idle sampling");
    require_true(
        log.find("\"output_wait\":{\"n\":1") != std::string::npos,
        "perf summary should expose the CPU stream wait");
    require_true(
        log.find("\"sync_queue_residual\":{\"n\":1") != std::string::npos,
        "perf summary should label the approximate unexplained sync wait");
    require_true(
        log.find("\"color_copy\":{\"n\":1") != std::string::npos,
        "perf summary should expose conditional color readback cost");
    require_true(
        log.size() < 4096,
        "one performance window should remain a compact record");
    std::filesystem::remove_all(root);
}

void benchmark_lightweight_perf_summary_hot_path() {
    const std::filesystem::path root = make_temp_test_dir("perf_summary_hot_path");
    runtime_app::PerfSummaryOptions options;
    options.enabled = true;
    options.interval_ms = 60000;
    options.directory = root;
    options.stdout_enabled = false;
    runtime_app::PerfSummaryLogger logger(options);

    runtime_app::PerfControllerWindowSample controller;
    controller.timestamp_ns = 1'000'000'000ull;
    controller.aiming = true;
    controller.output_delivered = true;
    controller.tick_ms = 0.40;
    controller.pipeline_ms = 0.20;
    controller.vigem_ms = 0.05;
    runtime_app::PerfVisionWindowSample vision;
    vision.aiming = true;
    vision.capture_to_result_ms = 5.4;
    vision.copy_to_result_ms = 4.8;
    vision.source_present_to_result_ms = 7.9;
    vision.result_to_controller_ms = 0.4;
    vision.source_present_to_vigem_ms = 8.4;
    vision.cuda_map_ms = 0.15;
    vision.preprocess_ms = 0.55;
    vision.infer_ms = 1.40;
    vision.gpu_total_ms = 2.1;
    vision.output_copy_sync_ms = 3.20;
    vision.output_copy_ms = 0.08;
    vision.output_wait_ms = 3.12;
    vision.sync_queue_residual_ms = 1.02;
    vision.color_copy_ms = 0.22;
    vision.cuda_unmap_ms = 0.04;

    constexpr std::uint64_t kControllerSamples = 1'000'000;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < kControllerSamples; ++index) {
        controller.timestamp_ns += 1000;
        logger.record_controller(controller);
        if (index % 6 == 0) logger.record_vision(vision);
    }
    const auto finished = std::chrono::steady_clock::now();
    logger.stop();
    const double ns_per_tick = std::chrono::duration<double, std::nano>(
        finished - started).count() / static_cast<double>(kControllerSamples);
    std::cout << "[PerfSummaryBenchmark] amortized_hot_path_ns_per_tick="
              << ns_per_tick << '\n';
    std::filesystem::remove_all(root);
}

}  // namespace

void register_benchmark_metrics_tests(native_test::Registry& registry) {
    registry.add_case("FeatureTelemetryAndDiagnostics", "replay_schema_captures_controller_components", test_replay_schema_captures_controller_components);
    registry.add_case("FeatureTelemetryAndDiagnostics", "replay_metrics_summarizes_errors", test_replay_metrics_summarizes_error_and_stale_fire_violations);
    registry.add_case("FeatureTelemetryAndDiagnostics", "replay_metrics_exposes_bodylock_defect", test_replay_metrics_exposes_bodylock_continuity_defect);
    registry.add_case("FeatureTelemetryAndDiagnostics", "replay_metrics_exposes_stepwise_stutter", test_replay_metrics_exposes_stepwise_assist_as_stutter);
    registry.add_case("FeatureTelemetryAndDiagnostics", "perf_loop_fps_uses_elapsed_time", test_perf_loop_fps_uses_measured_elapsed_time);
    registry.add_case("FeatureTelemetryAndDiagnostics", "lightweight_summary_writes_compact_window", test_lightweight_perf_summary_writes_one_compact_window);
    registry.add_case("FeatureTelemetryAndDiagnostics", "lightweight_summary_hot_path_budget", benchmark_lightweight_perf_summary_hot_path);
}
