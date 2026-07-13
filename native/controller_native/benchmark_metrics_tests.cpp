#include "controller_native/output_mixer.h"
#include "controller_native/virtual_gamepad.h"
#include "../common_native/authority_types.h"
#include "../replay_native/replay_metrics.h"
#include "../replay_native/replay_schema.h"
#include "../runtime_app/aim_perf_file_logger.h"
#include "../runtime_app/perf_logger.h"
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
        components.physical_stick = {0.70f, -0.80f};
        components.manual_stick = {0.10f, 0.20f};
        components.ai_aim_stick = {-0.40f, -0.20f};
        components.dynamic_adjustment_stick = {0.00f, -0.10f};
        components.post_ai_stick = {0.40f, 0.20f};
        components.post_dynamic_stick = {0.40f, 0.10f};
        components.ads_brake_stick = {-0.05f, 0.02f};
        components.post_ads_brake_stick = {0.35f, 0.12f};
        components.ads_carry_brake_stick = {-0.03f, 0.01f};
        components.post_ads_carry_brake_stick = {0.32f, 0.13f};
        components.ads_carry_brake_active = true;
        components.before_recoil_stick = {0.32f, 0.13f};
        components.recoil_stick = {-0.40f, 0.00f};
        components.final_stick = {0.50f, 0.10f};
        components.ads_brake_active = true;
        components.aim_mode = "ads_snap";
        components.fire_button = true;
        controller_native::GamepadOutputState tracker_output;
        tracker_output.right_x = 0.10f;
        tracker_output.right_y = 0.20f;
        vision_native::VisionResult vision;
        vision.frame_updated = true;
        vision.frame_id = 7;
        vision.age_ms = 96.0f;
        vision.preprocess_mode = vision_native::PreprocessMode::OldBgraCopy;
        controller_native::NativeControllerVisionState controller_vision;
        controller_vision.has_target = true;
        controller_vision.aim_authority = true;
        controller_vision.fire_authority = false;
        controller_vision.target_tier = "projected";
        controller_vision.dx = 12.0f;
        controller_vision.dy = -8.0f;
        controller_vision.has_tracker_projection = true;
        controller_vision.tracker_dx = 10.0f;
        controller_vision.tracker_dy = -6.0f;
        runtime_app::PerfSnapshot snapshot;
        snapshot.out_age_ms = 12.5;
        logger.record_aim_sample(
            1,
            true,
            snapshot,
            &vision,
            &controller_vision,
            &components,
            &tracker_output);
        log_path = logger.log_path();
    }

    const std::string log = read_text_file(log_path);
    require_true(
        log.find("\"recoil_x\":-0.4") != std::string::npos,
        "aim perf log should include recoil component x");
    require_true(
        log.find("\"physical_right_x\":0.7") != std::string::npos,
        "aim perf log should include physical right stick x");
    require_true(
        log.find("\"manual_pre_ai_x\":0.1") != std::string::npos,
        "aim perf log should include manual pre-ai stick x");
    require_true(
        log.find("\"post_ai_x\":0.4") != std::string::npos,
        "aim perf log should include post-ai stick x");
    require_true(
        log.find("\"post_dynamic_y\":0.1") != std::string::npos,
        "aim perf log should include post-dynamic stick y");
    require_true(
        log.find("\"ads_brake_x\":-0.05") != std::string::npos,
        "aim perf log should include ads brake x");
    require_true(
        log.find("\"post_ads_brake_y\":0.12") != std::string::npos,
        "aim perf log should include post ads brake y");
    require_true(
        log.find("\"ads_carry_brake_x\":-0.03") != std::string::npos,
        "aim perf log should include ads carry brake x");
    require_true(
        log.find("\"post_ads_carry_brake_y\":0.13") != std::string::npos,
        "aim perf log should include post ads carry brake y");
    require_true(
        log.find("\"ads_carry_brake_active\":true") != std::string::npos,
        "aim perf log should include ads carry brake active flag");
    require_true(
        log.find("\"before_recoil_x\":0.32") != std::string::npos,
        "aim perf log should include before-recoil stick x");
    require_true(
        log.find("\"ads_brake_active\":true") != std::string::npos,
        "aim perf log should include ads brake active flag");
    require_true(
        log.find("\"aim_mode\":\"ads_snap\"") != std::string::npos,
        "aim perf log should include aim mode");
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
    require_true(
        log.find("\"controller_target\":true") != std::string::npos,
        "aim perf log should include controller target state");
    require_true(
        log.find("\"controller_tier\":\"projected\"") != std::string::npos,
        "aim perf log should include controller target tier");
    require_true(
        log.find("\"controller_authority_state\":\"track_only\"") != std::string::npos,
        "aim perf log should include controller authority state");
    require_true(
        log.find("\"controller_tracker_dx\":10") != std::string::npos,
        "aim perf log should include controller tracker dx");
    require_true(
        log.find("\"vision_age_ms\":") != std::string::npos,
        "aim perf log should include explicit vision age");
    require_true(
        log.find("\"output_age_ms\":12.5") != std::string::npos,
        "aim perf log should include output age comparable to Python out_age");
    require_true(
        log.find("\"diagnostic_manual_magnitude\":") != std::string::npos,
        "aim perf log should include manual magnitude diagnostic");
    require_true(
        log.find("\"diagnostic_ai_magnitude\":") != std::string::npos,
        "aim perf log should include AI magnitude diagnostic");
    require_true(
        log.find("\"diagnostic_final_magnitude\":") != std::string::npos,
        "aim perf log should include final output magnitude diagnostic");
    require_true(
        log.find("\"diagnostic_target_error_px\":") != std::string::npos,
        "aim perf log should include target error diagnostic");
    require_true(
        log.find("\"diagnostic_manual_ai_fight\":true") != std::string::npos,
        "aim perf log should flag manual and AI fighting");
    require_true(
        log.find("\"diagnostic_near_target\":true") != std::string::npos,
        "aim perf log should flag near-target frames");
    require_true(
        log.find("\"diagnostic_near_high_output\":true") != std::string::npos,
        "aim perf log should flag high output near target");
    require_true(
        log.find("\"diagnostic_stale_target\":true") != std::string::npos,
        "aim perf log should flag stale target data");
    require_true(
        log.find("\"diagnostic_tracker_projection\":true") != std::string::npos,
        "aim perf log should mirror tracker projection as a diagnostic flag");
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

}  // namespace

int main() {
    try {
        test_replay_schema_captures_controller_components();
        test_replay_metrics_summarizes_error_and_fire_violations();
        test_replay_metrics_exposes_bodylock_continuity_defect();
        test_replay_metrics_exposes_stepwise_assist_as_stutter();
        test_aim_perf_file_logger_writes_controller_components();
        test_perf_loop_fps_uses_measured_elapsed_time();
    } catch (const std::exception& exc) {
        std::cerr << "[NativeBenchmarkMetricsTests] FAIL " << exc.what() << "\n";
        return 1;
    }

    std::cout << "[NativeBenchmarkMetricsTests] PASS\n";
    return 0;
}
