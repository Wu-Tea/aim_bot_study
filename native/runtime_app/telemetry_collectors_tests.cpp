#include "telemetry_collectors.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::TelemetryVisionInput vision(std::uint64_t id, float scale, bool aiming) {
    runtime_app::TelemetryVisionInput value;
    value.frame_id = id;
    value.captured_at_ns = id * 10'000'000;
    value.inferred_at_ns = value.captured_at_ns + 2'000'000;
    value.result_at_ns = value.captured_at_ns + 3'000'000;
    value.frame_width = 640;
    value.frame_height = 512;
    value.has_target = true;
    value.live = true;
    value.aiming = aiming;
    value.x1 = 320 - 50 * scale;
    value.x2 = 320 + 50 * scale;
    value.y1 = 256 - 100 * scale;
    value.y2 = 256 + 100 * scale;
    value.target_x = 350;
    value.target_y = 230;
    value.screen_center_x = 320;
    value.screen_center_y = 256;
    return value;
}

runtime_app::TelemetryTickInput tick(std::uint64_t seq, bool aiming) {
    runtime_app::TelemetryTickInput value;
    value.tick_id = seq;
    value.sample_ns = seq * 4'000'000;
    value.output_sent_ns = value.sample_ns;
    value.aiming = aiming;
    value.left_trigger = aiming ? 1.0f : 0.0f;
    value.physical_x = 0.2f;
    value.manual_x = 0.18f;
    value.ai_x = 0.05f;
    value.pre_recoil_x = 0.23f;
    value.final_x = 0.23f;
    return value;
}

void test_disabled_collectors_have_zero_transitions() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryCollectors collectors(false, &telemetry);
    collectors.observe_tick(tick(1, false));
    collectors.observe_new_vision(vision(1, 1.0f, false));
    REQUIRE(!collectors.enabled());
    REQUIRE(collectors.counters().state_transitions == 0);
    REQUIRE(telemetry.counters().accepted_records == 0);
}

void test_enabled_collectors_write_profile_and_ads_evidence() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_collectors";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 1024;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);

    collectors.observe_new_vision(vision(1, 1.0f, false));
    collectors.observe_tick(tick(1, false));
    collectors.observe_tick(tick(2, true));
    std::uint64_t tick_id = 3;
    std::uint64_t frame_id = 2;
    for (float scale : {1.10f, 1.25f, 1.39f, 1.40f, 1.40f, 1.40f}) {
        collectors.observe_tick(tick(tick_id++, true));
        collectors.observe_new_vision(vision(frame_id++, scale, true));
    }
    collectors.shutdown(tick_id * 4'000'000);
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"type\":\"session_metadata\"") != std::string::npos);
    REQUIRE(json.find("\"session_id\":\"") != std::string::npos);
    REQUIRE(json.find("\"type\":\"controller_sample\"") != std::string::npos);
    REQUIRE(json.find("\"physical_x\":0.2") != std::string::npos);
    REQUIRE(json.find("\"type\":\"target_event\"") != std::string::npos);
    REQUIRE(json.find("\"target_track_id\":1") != std::string::npos);
    REQUIRE(json.find("\"type\":\"ads_transition\"") != std::string::npos);
    REQUIRE(json.find("\"calibration_class\":\"conditional_model\"") != std::string::npos);
    REQUIRE(json.find("\"scale_x\":1.4") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_controller_samples_include_current_target_context() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_target_context";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    collectors.observe_new_vision(vision(1, 1.0f, false));
    for (std::uint64_t seq = 1; seq <= 8; ++seq) collectors.observe_tick(tick(seq, false));
    collectors.shutdown(40'000'000);
    telemetry.stop();
    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"controller_target_track_id\":1") != std::string::npos);
    REQUIRE(json.find("\"target_dx\":30") != std::string::npos);
    REQUIRE(json.find("\"aim_mode\":") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_ads_completeness_uses_observed_sequence_not_capture_frame_id() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_ads_frame_gaps";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    collectors.observe_new_vision(vision(10, 1.0f, false));
    collectors.observe_tick(tick(1, false));
    collectors.observe_tick(tick(2, true));
    std::uint64_t tick_id = 3;
    std::uint64_t frame_id = 20;
    for (float scale : {1.10f, 1.25f, 1.39f, 1.41f, 1.40f, 1.41f, 1.40f}) {
        collectors.observe_tick(tick(tick_id++, true));
        collectors.observe_new_vision(vision(frame_id, scale, true));
        frame_id += 10;
    }
    collectors.shutdown(80'000'000);
    telemetry.stop();
    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"type\":\"ads_transition\"") != std::string::npos);
    REQUIRE(json.find("\"valid\":true") != std::string::npos);
    REQUIRE(json.find("\"complete\":true") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}
}

int main() {
    test_disabled_collectors_have_zero_transitions();
    test_enabled_collectors_write_profile_and_ads_evidence();
    test_controller_samples_include_current_target_context();
    test_ads_completeness_uses_observed_sequence_not_capture_frame_id();
    return 0;
}
