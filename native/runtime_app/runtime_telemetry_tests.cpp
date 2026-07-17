#include "runtime_telemetry.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

namespace {

void require(bool value, int line) {
    if (!value) {
        std::cerr << "require failed at line " << line << '\n';
        std::abort();
    }
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::TelemetryRecord record(std::uint64_t tick, std::uint64_t frame) {
    runtime_app::TelemetryRecord value;
    value.kind = runtime_app::TelemetryRecordKind::ManualControllerTick;
    value.tick_id = tick;
    value.frame_id = frame;
    value.timestamp_ns = tick * 1'000'000;
    value.manual_x = 0.25f;
    value.final_x = 0.50f;
    return value;
}

void test_disabled_mode_has_zero_side_effects() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(!telemetry.enqueue(record(1, 1)));
    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 0);
    REQUIRE(counters.serialized_records == 0);
    REQUIRE(counters.accepted_records == 0);
    REQUIRE(telemetry.log_path().empty());
}

void test_bounded_queue_drops_exact_overflow_without_writer() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.queue_capacity = 2;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(telemetry.enqueue(record(1, 1)));
    REQUIRE(telemetry.enqueue(record(2, 2)));
    REQUIRE(!telemetry.enqueue(record(3, 3)));
    const auto counters = telemetry.counters();
    REQUIRE(counters.accepted_records == 2);
    REQUIRE(counters.dropped_normal_records == 1);
    REQUIRE(counters.queue_high_watermark == 2);
    REQUIRE(counters.serialized_records == 0);
}

void test_writer_serializes_records_and_deduplicates_vision_frames() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_tests";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 32;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    auto vision = record(1, 42);
    vision.kind = runtime_app::TelemetryRecordKind::VisionFrame;
    REQUIRE(telemetry.enqueue(vision));
    REQUIRE(!telemetry.enqueue(vision));
    REQUIRE(telemetry.enqueue(record(2, 42)));
    telemetry.stop();
    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 1);
    REQUIRE(counters.serialized_records == 2);
    REQUIRE(counters.duplicate_vision_frames == 1);
    REQUIRE(std::filesystem::exists(telemetry.log_path()));
    std::filesystem::remove_all(directory);
}

void test_rotation_never_overwrites_active_session_parts() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_rotation";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 64;
    options.rotate_size_bytes = 160;
    options.max_files = 2;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    for (std::uint64_t tick = 1; tick <= 20; ++tick) REQUIRE(telemetry.enqueue(record(tick, tick)));
    telemetry.stop();
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".jsonl") ++files;
    }
    REQUIRE(files > 2);
    REQUIRE(telemetry.counters().serialized_records == 20);
    std::filesystem::remove_all(directory);
}

void test_writer_failure_disables_file_telemetry_without_throwing() {
    const auto parent = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_failure";
    std::filesystem::remove_all(parent);
    { std::ofstream file(parent); file << "not a directory"; }
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = parent / "child";
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    REQUIRE(telemetry.enqueue(record(1, 1)));
    telemetry.stop();
    REQUIRE(telemetry.counters().writer_failures == 1);
    REQUIRE(telemetry.counters().serialized_records == 0);
    std::filesystem::remove(parent);
}

void test_versioned_schema_serializes_readiness_and_completeness() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_schema";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 8;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();

    runtime_app::TelemetryRecord value;
    value.type = runtime_app::TelemetryRecordType::ControllerSample;
    value.schema_version = 2;
    value.sample_seq = 17;
    value.readiness = runtime_app::TelemetryReadiness::ProfileEligible;
    value.controller.manual_x = 0.25f;
    value.completeness = {10, 17, 8, 8, 0, true};
    REQUIRE(telemetry.enqueue(value));
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"schema_version\":2") != std::string::npos);
    REQUIRE(json.find("\"type\":\"controller_sample\"") != std::string::npos);
    REQUIRE(json.find("\"sample_seq\":17") != std::string::npos);
    REQUIRE(json.find("\"readiness\":\"profile_eligible\"") != std::string::npos);
    REQUIRE(json.find("\"first_seq\":10") != std::string::npos);
    REQUIRE(json.find("\"complete\":true") != std::string::npos);
    REQUIRE(json.find("\"manual_x\":0.25") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_every_rotated_file_starts_with_session_metadata() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_metadata_rotation";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 32;
    options.rotate_size_bytes = 1200;
    options.max_files = 3;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryRecord metadata;
    metadata.type = runtime_app::TelemetryRecordType::SessionMetadata;
    std::snprintf(metadata.session_metadata.session_id.data(),
        metadata.session_metadata.session_id.size(), "%s", "rotation-session");
    REQUIRE(telemetry.enqueue(metadata));
    for (std::uint64_t tick = 1; tick <= 8; ++tick) REQUIRE(telemetry.enqueue(record(tick, tick)));
    telemetry.stop();
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".jsonl") continue;
        ++files;
        std::ifstream input(entry.path());
        std::string first_line;
        std::getline(input, first_line);
        REQUIRE(first_line.find("\"type\":\"session_metadata\"") != std::string::npos);
        REQUIRE(first_line.find("rotation-session") != std::string::npos);
    }
    REQUIRE(files > 1);
    std::filesystem::remove_all(directory);
}

void test_type_specific_fields_and_timestamps_are_serialized() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_specific_fields";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();

    runtime_app::TelemetryRecord metadata;
    metadata.type = runtime_app::TelemetryRecordType::SessionMetadata;
    std::snprintf(metadata.session_metadata.session_id.data(),
        metadata.session_metadata.session_id.size(), "%s", "specific-session");
    REQUIRE(telemetry.enqueue(metadata));

    runtime_app::TelemetryRecord input_event;
    input_event.type = runtime_app::TelemetryRecordType::InputEvent;
    input_event.timestamps.sample_ns = 123456;
    input_event.input_event.kind = runtime_app::InputEventKind::DirectionReversed;
    input_event.input_event.input_episode_id = 9;
    input_event.input_event.magnitude = 0.42f;
    REQUIRE(telemetry.enqueue(input_event));

    runtime_app::TelemetryRecord ads;
    ads.type = runtime_app::TelemetryRecordType::AdsTransition;
    ads.ads_transition.invalid_reason = runtime_app::AdsInvalidReason::VisualSettleUnproven;
    REQUIRE(telemetry.enqueue(ads));

    runtime_app::TelemetryRecord response;
    response.type = runtime_app::TelemetryRecordType::ControlResponseWindow;
    response.control_response.reason = runtime_app::ResponseWindowReason::SampleGap;
    REQUIRE(telemetry.enqueue(response));
    telemetry.stop();

    REQUIRE(telemetry.log_path().filename().string().find("specific-session") != std::string::npos);
    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"input_event_kind\":\"direction_reversed\"") != std::string::npos);
    REQUIRE(json.find("\"input_episode_id\":9") != std::string::npos);
    REQUIRE(json.find("\"sample_ns\":123456") != std::string::npos);
    REQUIRE(json.find("\"invalid_reason\":\"visual_settle_unproven\"") != std::string::npos);
    REQUIRE(json.find("\"response_reason\":\"sample_gap\"") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_controller_pipeline_and_target_provenance_are_serialized() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_pipeline";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();

    runtime_app::TelemetryRecord value;
    value.type = runtime_app::TelemetryRecordType::ControllerSample;
    value.target_track_id = 42;
    value.controller.post_ai_x = 0.11f;
    value.controller.dynamic_adjustment_x = 0.02f;
    value.controller.post_dynamic_x = 0.13f;
    value.controller.ads_brake_x = -0.01f;
    value.controller.post_ads_brake_x = 0.12f;
    value.controller.ads_carry_brake_x = -0.02f;
    value.controller.post_ads_carry_brake_x = 0.10f;
    value.controller.manual_takeover_active = true;
    value.controller.auto_fire_requested = true;
    value.controller.auto_fire_aim_ready = true;
    value.controller.auto_fire_allowed = true;
    value.controller.auto_fire_active = true;
    value.controller.auto_fire_pulse_starts = 7;
    value.controller.auto_fire_pulse_pressed = true;
    value.controller.auto_fire_cadence_wait = false;
    value.controller.final_fire_button = true;
    std::snprintf(value.controller.auto_fire_block_reason.data(),
        value.controller.auto_fire_block_reason.size(), "%s", "none");
    value.controller.detector_box_count = 1;
    value.controller.production_target_confidence = 0.91f;
    value.controller.selected_track_id = 73;
    value.controller.selected_observation_id = 901;
    value.controller.backing_frame_id = 55;
    value.controller.track_observation_age_ms = 8.5f;
    value.controller.track_position_sigma = 0.025f;
    value.controller.track_ambiguity = 0.12f;
    value.controller.requested_assist_x = 0.34f;
    value.controller.shaped_assist_x = 0.10f;
    std::snprintf(value.controller.production_target_source.data(),
        value.controller.production_target_source.size(), "%s", "yolo_body");
    std::snprintf(value.controller.production_target_tier.data(),
        value.controller.production_target_tier.size(), "%s", "primary");
    std::snprintf(value.controller.assist_authority.data(),
        value.controller.assist_authority.size(), "%s", "continuity");
    std::snprintf(value.controller.assist_authority_reason.data(),
        value.controller.assist_authority_reason.size(), "%s", "short_evidence_gap");
    std::snprintf(value.controller.bodylock_lifecycle.data(),
        value.controller.bodylock_lifecycle.size(), "%s", "coast");
    std::snprintf(value.controller.bodylock_transition_reason.data(),
        value.controller.bodylock_transition_reason.size(), "%s", "continuity");
    std::snprintf(value.controller.assist_limit_reason.data(),
        value.controller.assist_limit_reason.size(), "%s", "assist_envelope");
    REQUIRE(telemetry.enqueue(value));
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"telemetry_identity_track_id\":42") != std::string::npos);
    REQUIRE(json.find("\"post_ai_x\":0.11") != std::string::npos);
    REQUIRE(json.find("\"dynamic_adjustment_x\":0.02") != std::string::npos);
    REQUIRE(json.find("\"post_dynamic_x\":0.13") != std::string::npos);
    REQUIRE(json.find("\"ads_brake_x\":-0.01") != std::string::npos);
    REQUIRE(json.find("\"post_ads_brake_x\":0.12") != std::string::npos);
    REQUIRE(json.find("\"ads_carry_brake_x\":-0.02") != std::string::npos);
    REQUIRE(json.find("\"post_ads_carry_brake_x\":0.1") != std::string::npos);
    REQUIRE(json.find("\"manual_takeover_active\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_requested\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_aim_ready\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_allowed\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_active\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_pulse_starts\":7") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_pulse_pressed\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_cadence_wait\":false") != std::string::npos);
    REQUIRE(json.find("\"final_fire_button\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_block_reason\":\"none\"") != std::string::npos);
    REQUIRE(json.find("\"detector_box_count\":1") != std::string::npos);
    REQUIRE(json.find("\"production_target_source\":\"yolo_body\"") != std::string::npos);
    REQUIRE(json.find("\"production_target_tier\":\"primary\"") != std::string::npos);
    REQUIRE(json.find("\"production_target_confidence\":0.91") != std::string::npos);
    REQUIRE(json.find("\"selected_track_id\":73") != std::string::npos);
    REQUIRE(json.find("\"selected_observation_id\":901") != std::string::npos);
    REQUIRE(json.find("\"track_backing_frame_id\":55") != std::string::npos);
    REQUIRE(json.find("\"track_observation_age_ms\":8.5") != std::string::npos);
    REQUIRE(json.find("\"track_position_sigma\":0.025") != std::string::npos);
    REQUIRE(json.find("\"track_ambiguity\":0.12") != std::string::npos);
    REQUIRE(json.find("\"assist_authority\":\"continuity\"") != std::string::npos);
    REQUIRE(json.find("\"assist_authority_reason\":\"short_evidence_gap\"") != std::string::npos);
    REQUIRE(json.find("\"bodylock_lifecycle\":\"coast\"") != std::string::npos);
    REQUIRE(json.find("\"bodylock_transition_reason\":\"continuity\"") != std::string::npos);
    REQUIRE(json.find("\"requested_assist_x\":0.34") != std::string::npos);
    REQUIRE(json.find("\"shaped_assist_x\":0.1") != std::string::npos);
    REQUIRE(json.find("\"assist_limit_reason\":\"assist_envelope\"") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    test_disabled_mode_has_zero_side_effects();
    test_bounded_queue_drops_exact_overflow_without_writer();
    test_writer_serializes_records_and_deduplicates_vision_frames();
    test_rotation_never_overwrites_active_session_parts();
    test_writer_failure_disables_file_telemetry_without_throwing();
    test_versioned_schema_serializes_readiness_and_completeness();
    test_every_rotated_file_starts_with_session_metadata();
    test_type_specific_fields_and_timestamps_are_serialized();
    test_controller_pipeline_and_target_provenance_are_serialized();
    return 0;
}
