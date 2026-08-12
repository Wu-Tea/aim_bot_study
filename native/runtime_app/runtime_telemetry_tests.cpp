#include "runtime_telemetry.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool value, int line) {
    if (!value) {
        std::cerr << "require failed at line " << line << '\n';
        std::abort();
    }
}
#define REQUIRE(value) require((value), __LINE__)

template <std::size_t N>
void copy_text(std::array<char, N>& destination, const char* source) {
    std::snprintf(destination.data(), destination.size(), "%s", source);
}

runtime_app::TelemetryRecord controller_record(std::uint64_t tick) {
    runtime_app::TelemetryRecord value;
    value.type = runtime_app::TelemetryRecordType::ControllerSample;
    value.readiness = runtime_app::TelemetryReadiness::ProfileEligible;
    value.tick_id = tick;
    value.sample_seq = tick;
    value.timestamps.sample_ns = tick * 1'000'000;
    value.controller.physical_connected = true;
    value.controller.current_observed_target_present = true;
    value.controller.output_delivered = true;
    value.controller.output_backend_connected = true;
    value.controller.manual_x = 0.25f;
    value.controller.ai_x = 0.10f;
    value.controller.target_final_x = 0.30f;
    value.controller.ai_correction_x = 0.05f;
    value.controller.requested_assist_x = 0.12f;
    value.controller.shaped_assist_x = 0.10f;
    value.controller.visual_authority = 0.83f;
    value.controller.enemy_cue_current = true;
    value.controller.enemy_identity_confirmed = true;
    value.controller.enemy_cue_checked = true;
    value.controller.final_x = 0.35f;
    value.controller.handover_requested = true;
    value.controller.handover_braking = true;
    value.controller.selected_track_id = 71;
    value.controller.selected_observation_id = 91;
    value.controller.enemy_mark_request_pending = true;
    value.controller.enemy_mark_fired = true;
    value.controller.enemy_mark_confirmation_frames = 2;
    value.controller.enemy_mark_target_scope = 5;
    value.controller.enemy_mark_target_generation = 17;
    value.controller.enemy_mark_last_scope = 5;
    value.controller.enemy_mark_last_generation = 17;
    copy_text(value.controller.manual_authority_mode, "micro_manual");
    copy_text(value.controller.assist_control_phase, "handover_brake");
    copy_text(value.controller.assist_authority, "aim_and_fire");
    copy_text(value.controller.assist_authority_reason, "fresh_observed");
    copy_text(value.controller.enemy_mark_block_reason, "none");
    return value;
}

runtime_app::TelemetryRecord committed_record(std::uint64_t frame_id) {
    runtime_app::TelemetryRecord value;
    value.type = runtime_app::TelemetryRecordType::CommittedCaptureObservation;
    value.frame_id = frame_id;
    value.target_track_id = 71;
    value.vision_sample_quality = runtime_app::VisionSampleQuality::Normal;
    value.committed_observation.source_frame_id = frame_id;
    value.committed_observation.source_observation_id = frame_id + 100;
    value.committed_observation.persistent_target_id = 71;
    value.committed_observation.captured_at_ns = frame_id * 1'000'000;
    value.committed_observation.result_at_ns = frame_id * 1'000'000 + 400'000;
    value.committed_observation.controller_consume_ns = frame_id * 1'000'000 + 600'000;
    value.committed_observation.stable_error_x = 12.5f;
    value.committed_observation.stable_error_y = -3.0f;
    value.committed_observation.reliability = 0.92f;
    value.committed_observation.fresh_observed = true;
    value.committed_observation.strong_observation = true;
    value.committed_observation.stable_coordinates_valid = true;
    return value;
}

runtime_app::TelemetryRecord metadata_record() {
    runtime_app::TelemetryRecord value;
    value.type = runtime_app::TelemetryRecordType::SessionMetadata;
    value.critical = true;
    copy_text(value.session_metadata.session_id, "cleanup-test-session");
    copy_text(value.session_metadata.build_commit, "cleanup-test-revision");
    copy_text(value.session_metadata.config_hash, "config-sha");
    copy_text(value.session_metadata.engine_hash, "engine-sha");
    copy_text(value.session_metadata.executable_sha256, "exe-sha");
    value.session_metadata.capture_width = 640;
    value.session_metadata.capture_height = 640;
    value.session_metadata.active_capture_fps = 180;
    value.session_metadata.controller_tick_hz = 1000;
    value.session_metadata.telemetry_hz = 250;
    return value;
}

std::vector<std::filesystem::path> jsonl_files(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(directory)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".jsonl") result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string read_files(const std::filesystem::path& directory) {
    std::ostringstream text;
    for (const auto& path : jsonl_files(directory)) {
        std::ifstream input(path);
        text << input.rdbuf();
    }
    return text.str();
}

void test_disabled_mode_has_zero_side_effects() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(!telemetry.enqueue(controller_record(1)));
    telemetry.start();
    telemetry.stop();
    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 0);
    REQUIRE(counters.serialized_records == 0);
    REQUIRE(counters.accepted_records == 0);
    REQUIRE(telemetry.log_path().empty());
    REQUIRE(telemetry.ordinary_queue_size() == 0);
}

void test_bounded_queue_counts_exact_overflow_without_writer() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.start_writer = false;
    options.queue_capacity = 2;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(telemetry.enqueue(controller_record(1)));
    REQUIRE(telemetry.enqueue(controller_record(2)));
    REQUIRE(!telemetry.enqueue(controller_record(3)));
    auto critical = controller_record(4);
    critical.critical = true;
    REQUIRE(!telemetry.enqueue(critical));
    const auto counters = telemetry.counters();
    REQUIRE(counters.accepted_records == 2);
    REQUIRE(counters.dropped_normal_records == 1);
    REQUIRE(counters.dropped_critical_records == 1);
    REQUIRE(counters.queue_high_watermark == 2);
    REQUIRE(counters.serialized_records == 0);
    REQUIRE(telemetry.ordinary_queue_capacity() == 2);
}

void test_writer_serializes_current_controller_and_deduplicates_source_frames() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_current";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 32;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    REQUIRE(telemetry.enqueue(controller_record(1)));
    const auto observation = committed_record(42);
    REQUIRE(telemetry.enqueue(observation));
    REQUIRE(!telemetry.enqueue(observation));
    telemetry.stop();

    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 1);
    REQUIRE(counters.serialized_records == 2);
    REQUIRE(counters.duplicate_source_frames == 1);
    const std::string text = read_files(directory);
    REQUIRE(text.find("\"assist_control_phase\":\"handover_brake\"") != std::string::npos);
    REQUIRE(text.find("\"requested_assist_x\":0.12") != std::string::npos);
    REQUIRE(text.find("\"shaped_assist_x\":0.1") != std::string::npos);
    REQUIRE(text.find("\"visual_authority\":0.83") != std::string::npos);
    REQUIRE(text.find("\"enemy_cue_current\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_identity_confirmed\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_cue_checked\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_fired\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_target_scope\":5") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_target_generation\":17") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_block_reason\":\"none\"") != std::string::npos);
    REQUIRE(text.find("\"schema\":\"committed_capture_observation_v1\"") != std::string::npos);
    REQUIRE(text.find("\"fresh_observed\":true") != std::string::npos);
    REQUIRE(text.find("legacy_kind") == std::string::npos);
    REQUIRE(text.find("reused_or_projected") == std::string::npos);
    std::filesystem::remove_all(directory);
}

void test_current_observation_and_delivery_include_session_provenance() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_provenance";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    REQUIRE(telemetry.enqueue(metadata_record()));
    REQUIRE(telemetry.enqueue(committed_record(7)));
    runtime_app::TelemetryRecord delivered;
    delivered.type = runtime_app::TelemetryRecordType::DeliveredControlSample;
    delivered.delivered_control.sample_seq = 9;
    delivered.delivered_control.applied_at_ns = 9'500'000;
    delivered.delivered_control.final_right_x = 0.3f;
    delivered.delivered_control.output_delivered = true;
    REQUIRE(telemetry.enqueue(delivered));
    telemetry.stop();

    const std::string text = read_files(directory);
    REQUIRE(text.find("\"type\":\"session_metadata\"") != std::string::npos);
    REQUIRE(text.find("\"schema\":\"delivered_control_sample_v2\"") != std::string::npos);
    REQUIRE(text.find("\"build_revision\":\"cleanup-test-revision\"") != std::string::npos);
    REQUIRE(text.find("\"final_right\":[0.3,0]") != std::string::npos);
    REQUIRE(text.find("\"physical_right\"") == std::string::npos);
    REQUIRE(text.find("identification_update_outcome") == std::string::npos);
    std::filesystem::remove_all(directory);
}

void test_rotated_files_begin_with_session_metadata() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_rotation";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 128;
    options.rotate_size_bytes = 700;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    REQUIRE(telemetry.enqueue(metadata_record()));
    for (std::uint64_t tick = 1; tick <= 30; ++tick) {
        REQUIRE(telemetry.enqueue(controller_record(tick)));
    }
    telemetry.stop();
    const auto files = jsonl_files(directory);
    REQUIRE(files.size() >= 2);
    for (const auto& path : files) {
        std::ifstream input(path);
        std::string first_line;
        std::getline(input, first_line);
        REQUIRE(first_line.find("\"type\":\"session_metadata\"") != std::string::npos);
        REQUIRE(first_line.find("cleanup-test-session") != std::string::npos);
    }
    std::filesystem::remove_all(directory);
}

void test_writer_failure_is_nonthrowing_and_counted_once() {
    const auto parent = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_failure";
    std::filesystem::remove_all(parent);
    { std::ofstream file(parent); file << "not a directory"; }
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = parent / "child";
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    REQUIRE(telemetry.enqueue(controller_record(1)));
    telemetry.stop();
    REQUIRE(telemetry.counters().writer_failures == 1);
    REQUIRE(telemetry.counters().serialized_records == 0);
    std::filesystem::remove(parent);
}

} // namespace

int main() {
    test_disabled_mode_has_zero_side_effects();
    test_bounded_queue_counts_exact_overflow_without_writer();
    test_writer_serializes_current_controller_and_deduplicates_source_frames();
    test_current_observation_and_delivery_include_session_provenance();
    test_rotated_files_begin_with_session_metadata();
    test_writer_failure_is_nonthrowing_and_counted_once();
    std::cout << "runtime_telemetry_tests: PASS\n";
    return 0;
}
