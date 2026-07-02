#include "controller_native/output_mixer.h"
#include "controller_native/virtual_gamepad.h"
#include "../common_native/authority_types.h"
#include "../replay_native/replay_metrics.h"
#include "../replay_native/replay_schema.h"
#include "../runtime_app/aim_perf_file_logger.h"
#include "vision_native/types.h"

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
    frame.tracker.source = tracking_native::TrackerSnapshotSource::Projected;
    frame.tracker.assist_authority = common_native::AssistAuthority::AimCoast;
    frame.tracker.fire_authority = common_native::FireAuthority::None;
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

void test_replay_metrics_summarizes_error_and_fire_violations() {
    std::vector<replay_native::NativeReplayFrame> frames(3);
    frames[0].selected_target.has_target = true;
    frames[0].selected_target.aim_error_px = {10.0f, 0.0f};
    frames[0].tracker.projection_age_ms = 4.0;

    frames[1].selected_target.has_target = true;
    frames[1].selected_target.aim_error_px = {20.0f, 0.0f};
    frames[1].tracker.source = tracking_native::TrackerSnapshotSource::Projected;
    frames[1].tracker.fire_authority = common_native::FireAuthority::None;
    frames[1].tracker.projection_age_ms = 8.0;
    frames[1].controller.fire_allowed = true;

    frames[2].selected_target.has_target = true;
    frames[2].selected_target.aim_error_px = {30.0f, 0.0f};
    frames[2].selected_target.age_ms = 60.0;
    frames[2].tracker.fire_authority = common_native::FireAuthority::ObservedOnly;
    frames[2].tracker.projection_age_ms = 16.0;
    frames[2].controller.fire_allowed = true;

    replay_native::ReplayMetricOptions options;
    options.max_fire_source_age_ms = 50.0;
    const replay_native::ReplayMetricSummary summary =
        replay_native::summarize_replay_metrics(frames, options);

    require_near(summary.target_error_p50_px, 20.0f, 0.001f, "replay p50 target error");
    require_near(summary.target_error_p95_px, 30.0f, 0.001f, "replay p95 target error");
    require_near(summary.projection_age_p95_ms, 16.0f, 0.001f, "replay p95 projection age");
    require_true(
        summary.predicted_only_fire_violations == 1,
        "replay metrics should count predicted-only fire violations");
    require_true(
        summary.stale_fire_violations == 1,
        "replay metrics should count stale fire violations");
}

void test_aim_perf_file_logger_writes_controller_components() {
    const std::filesystem::path root = make_temp_test_dir("aim_perf_components");
    std::filesystem::path log_path;
    {
        runtime_app::AimPerfFileLogger logger(true, root, 1);
        controller_native::NativeControllerOutputComponents components;
        components.manual_stick = {0.10f, 0.20f};
        components.ai_aim_stick = {0.30f, 0.00f};
        components.dynamic_adjustment_stick = {0.00f, -0.10f};
        components.recoil_stick = {-0.40f, 0.00f};
        components.final_stick = {0.00f, 0.10f};
        components.fire_button = true;
        controller_native::GamepadOutputState tracker_output;
        tracker_output.right_x = 0.10f;
        tracker_output.right_y = 0.20f;
        vision_native::VisionResult vision;
        vision.frame_updated = true;
        vision.frame_id = 7;
        vision.preprocess_mode = vision_native::PreprocessMode::OldBgraCopy;
        runtime_app::PerfSnapshot snapshot;
        logger.record_aim_sample(
            1,
            true,
            snapshot,
            &vision,
            &components,
            &tracker_output);
        log_path = logger.log_path();
    }

    const std::string log = read_text_file(log_path);
    require_true(
        log.find("\"recoil_x\":-0.4") != std::string::npos,
        "aim perf log should include recoil component x");
    require_true(
        log.find("\"final_y\":0.1") != std::string::npos,
        "aim perf log should include final stick y");
    require_true(
        log.find("\"tracker_sample_x\":0.1") != std::string::npos,
        "aim perf log should include tracker sample x");
    require_true(
        log.find("\"fire_button\":true") != std::string::npos,
        "aim perf log should include fire button state");
    require_true(
        log.find("\"preprocess_mode\":\"old_bgra_copy\"") != std::string::npos,
        "aim perf log should include preprocess mode");
}

}  // namespace

int main() {
    try {
        test_replay_schema_captures_controller_components();
        test_replay_metrics_summarizes_error_and_fire_violations();
        test_aim_perf_file_logger_writes_controller_components();
    } catch (const std::exception& exc) {
        std::cerr << "[NativeBenchmarkMetricsTests] FAIL " << exc.what() << "\n";
        return 1;
    }

    std::cout << "[NativeBenchmarkMetricsTests] PASS\n";
    return 0;
}
