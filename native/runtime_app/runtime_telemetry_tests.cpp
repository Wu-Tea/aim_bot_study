#include "runtime_telemetry.h"

#include <algorithm>
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
    value.controller.observed_error_x = 7.5f;
    value.controller.observed_error_y = -2.0f;
    value.controller.pending_motion_x = 1.25f;
    value.controller.control_error_x = 6.25f;
    value.controller.pending_motion_confidence = 0.6f;
    value.controller.pending_motion_valid = true;
    value.controller.memory_applied = true;
    std::snprintf(value.controller.memory_status.data(),
                  value.controller.memory_status.size(), "applied");
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
    REQUIRE(json.find("\"observed_error_x\":7.5") != std::string::npos);
    REQUIRE(json.find("\"observed_error_y\":-2") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_x\":1.25") != std::string::npos);
    REQUIRE(json.find("\"control_error_x\":6.25") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_confidence\":0.6") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_valid\":true") != std::string::npos);
    REQUIRE(json.find("\"memory_applied\":true") != std::string::npos);
    REQUIRE(json.find("\"memory_status\":\"applied\"") != std::string::npos);
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

void test_causal_journal_serializes_capture_control_and_provenance() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_causal_journal_schema";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();

    runtime_app::TelemetryRecord metadata;
    metadata.type = runtime_app::TelemetryRecordType::SessionMetadata;
    std::snprintf(metadata.session_metadata.session_id.data(),
        metadata.session_metadata.session_id.size(), "%s", "causal-session");
    std::snprintf(metadata.session_metadata.build_commit.data(),
        metadata.session_metadata.build_commit.size(), "%s", "revision-abc");
    std::snprintf(metadata.session_metadata.config_hash.data(),
        metadata.session_metadata.config_hash.size(), "%064d", 1);
    std::snprintf(metadata.session_metadata.engine_hash.data(),
        metadata.session_metadata.engine_hash.size(), "%064d", 2);
    metadata.session_metadata.capture_width = 480;
    metadata.session_metadata.capture_height = 416;
    REQUIRE(telemetry.enqueue(metadata));

    runtime_app::TelemetryRecord observation;
    observation.type = runtime_app::TelemetryRecordType::CommittedCaptureObservation;
    observation.committed_observation.source_frame_id = 91;
    observation.committed_observation.source_observation_id = 9001;
    observation.committed_observation.persistent_target_id = 7;
    observation.committed_observation.viewport_source_frame_id = 91;
    observation.committed_observation.captured_at_ns = 900;
    observation.committed_observation.result_at_ns = 960;
    observation.committed_observation.stable_error_x = 12.0f;
    observation.committed_observation.stable_error_y = -4.0f;
    observation.committed_observation.stable_coordinates_valid = true;
    observation.committed_observation.fresh_observed = true;
    observation.committed_observation.eligible_candidate_count = 1;
    observation.vision_sample_quality = runtime_app::VisionSampleQuality::Normal;
    observation.identification_update_outcome =
        runtime_app::IdentificationUpdateOutcome::NotEvaluated;
    REQUIRE(telemetry.enqueue(observation));

    runtime_app::TelemetryRecord control;
    control.type = runtime_app::TelemetryRecordType::DeliveredControlSample;
    control.delivered_control.sample_seq = 12;
    control.delivered_control.applied_at_ns = 975;
    control.delivered_control.final_right_x = 0.5f;
    control.delivered_control.final_left_x = 0.25f;
    control.delivered_control.output_delivered = true;
    REQUIRE(telemetry.enqueue(control));
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"schema\":\"causal_response_journal_v1\"") != std::string::npos);
    REQUIRE(json.find("\"captured_at_ns\":900") != std::string::npos);
    REQUIRE(json.find("\"result_at_ns\":960") != std::string::npos);
    REQUIRE(json.find("\"applied_at_ns\":975") != std::string::npos);
    REQUIRE(json.find("\"final_left\":[0.25,0]") != std::string::npos);
    REQUIRE(json.find("\"config_sha256\":\"") != std::string::npos);
    REQUIRE(json.find("\"vision_sample_quality\":\"normal\"") != std::string::npos);
    REQUIRE(json.find("\"identification_update_outcome\":\"not_evaluated\"") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_gate25_records_serialize_separate_keys_and_delivery_join() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_gate25";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();

    runtime_app::Gate25Anomaly anomaly;
    anomaly.source_frame_id = 17;
    anomaly.source_observation_id = 1701;
    anomaly.controller_tick_id = 900;
    anomaly.delivery_first_seq = 41;
    anomaly.delivery_last_seq = 44;
    anomaly.reason =
        runtime_app::Gate25Reason::TargetMotionContamination;
    REQUIRE(telemetry.enqueue_gate25_anomaly(anomaly));

    runtime_app::Gate25AggregateSnapshot aggregate;
    aggregate.mode_boundary = 1;
    aggregate.cancellation_or_reversal = 2;
    aggregate.last_controller_tick_id = 901;
    aggregate.delivery_first_seq = 41;
    aggregate.delivery_last_seq = 46;
    aggregate.observations = 2;
    REQUIRE(telemetry.enqueue_gate25_aggregate(aggregate));
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"schema\":\"w5_gate2_5a_anomaly_v1\"") !=
            std::string::npos);
    REQUIRE(json.find("\"controller_tick_id\":900") != std::string::npos);
    REQUIRE(json.find("\"delivery_first_seq\":41") != std::string::npos);
    REQUIRE(json.find("\"delivery_last_seq\":44") != std::string::npos);
    REQUIRE(json.find("\"schema\":\"w5_gate2_5a_aggregate_v1\"") !=
            std::string::npos);
    REQUIRE(json.find("\"last_controller_tick_id\":901") !=
            std::string::npos);
    REQUIRE(json.find("\"delivery_last_seq\":46") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_gate25_queue_slot_size_is_measured_and_bounded() {
    constexpr std::size_t kDefaultQueueCapacity = 8192;
    const std::size_t ordinary_queue_bytes =
        sizeof(runtime_app::TelemetryRecord) * kDefaultQueueCapacity;
    const std::size_t legacy_gate_queue_bytes =
        (sizeof(runtime_app::TelemetryRecord) +
         sizeof(runtime_app::Gate25AggregateSnapshot)) *
        kDefaultQueueCapacity;
    std::cout << "telemetry_record_size_after="
              << sizeof(runtime_app::TelemetryRecord)
              << " ordinary_queue_bytes=" << ordinary_queue_bytes
              << " legacy_uniform_record_plus_aggregate_bytes="
              << legacy_gate_queue_bytes
              << " gate25_aggregate_size="
              << sizeof(runtime_app::Gate25AggregateSnapshot)
              << " gate25_transport_size="
              << sizeof(runtime_app::Gate25AggregateTransport)
              << " legacy_uniform_transport_bytes="
              << sizeof(runtime_app::Gate25AggregateTransport) *
                  runtime_app::kGate25TransportQueueCapacity
              << " gate25_anomaly_size="
              << sizeof(runtime_app::Gate25Anomaly)
              << " runtime_telemetry_object_size="
              << sizeof(runtime_app::RuntimeTelemetry) << '\n';
    runtime_app::RuntimeTelemetryOptions gate_only_options;
    gate_only_options.enabled = true;
    gate_only_options.gate_enabled = true;
    gate_only_options.start_writer = false;
    gate_only_options.gate_only = true;
    gate_only_options.queue_capacity = kDefaultQueueCapacity;
    runtime_app::RuntimeTelemetry gate_only(gate_only_options);
    std::cout << "gate25_live_shadow_size="
              << sizeof(runtime_app::Gate25LiveShadow)
              << " control_history_1024_size="
              << sizeof(control_learning::ControlHistory<1024>)
              << " gate_only_ordinary_queue_capacity="
              << gate_only.ordinary_queue_capacity()
              << " gate_only_ordinary_queue_size="
              << gate_only.ordinary_queue_size()
              << " gate_only_ordinary_queue_bytes="
              << gate_only.ordinary_queue_bytes()
              << " gate25_transport_queue_bytes="
              << gate_only.gate25_transport_queue_bytes() << '\n';
    REQUIRE(gate_only.ordinary_queue_capacity() ==
            runtime_app::kGate25GateOnlyOrdinaryQueueCapacity);
    REQUIRE(gate_only.ordinary_queue_bytes() ==
            runtime_app::kGate25GateOnlyOrdinaryQueueCapacity *
                sizeof(runtime_app::TelemetryRecord));
    runtime_app::RuntimeTelemetryOptions detailed_options;
    detailed_options.enabled = true;
    detailed_options.start_writer = false;
    detailed_options.gate_only = false;
    detailed_options.queue_capacity = 32;
    runtime_app::RuntimeTelemetry detailed(detailed_options);
    REQUIRE(detailed.ordinary_queue_capacity() == 32);
    REQUIRE(sizeof(runtime_app::TelemetryRecord) <= 4096u);
    REQUIRE(ordinary_queue_bytes < legacy_gate_queue_bytes);
    REQUIRE(detailed.gate25_transport_queue_bytes() == 0);
    REQUIRE(gate_only.gate25_transport_queue_bytes() > 0);

    runtime_app::RuntimeTelemetryOptions standard_gate_options;
    standard_gate_options.enabled = true;
    standard_gate_options.gate_enabled = true;
    standard_gate_options.start_writer = false;
    standard_gate_options.gate_only = false;
    standard_gate_options.queue_capacity = 32;
    runtime_app::RuntimeTelemetry standard_gate(standard_gate_options);
    REQUIRE(standard_gate.ordinary_queue_capacity() == 32);
    REQUIRE(standard_gate.gate25_transport_queue_bytes() > 0);

    runtime_app::RuntimeTelemetryOptions disabled_options;
    disabled_options.enabled = false;
    disabled_options.gate_enabled = true;
    runtime_app::RuntimeTelemetry disabled(disabled_options);
    REQUIRE(disabled.gate25_transport_queue_bytes() == 0);
}

void test_gate25_dense_grid_serializes_active_cells_compactly() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_gate25_grid";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::Gate25AggregateSnapshot aggregate;
    aggregate.mode_boundary = 1;
    aggregate.cancellation_or_reversal = 2;
    for (auto& cell : aggregate.cohort_grid) {
        cell.pair_count = 1;
        cell.valid_count = 1;
        cell.snr_pass_count = 1;
    }
    REQUIRE(telemetry.enqueue_gate25_aggregate(aggregate));
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::string line;
    std::size_t aggregate_line_size = 0;
    while (std::getline(input, line)) {
        if (line.find("w5_gate2_5a_aggregate_v1") != std::string::npos) {
            aggregate_line_size = std::max(aggregate_line_size, line.size());
            REQUIRE(line.find("\"cohort_grid_columns\"") != std::string::npos);
            REQUIRE(line.find("\"cohort_grid_active_count\":72") !=
                    std::string::npos);
            REQUIRE(line.find("\"cohort_grid_overflow\":false") !=
                    std::string::npos);
            REQUIRE(line.find("\"mode_boundary\":1") != std::string::npos);
            REQUIRE(line.find("\"cancellation_or_reversal\":2") !=
                    std::string::npos);
            REQUIRE(line.find("[0,0,0,1") != std::string::npos);
            const std::string expected_columns =
                "\"cohort_grid_columns\":[\"mode\",\"axis_bin\",\"command_bin\","
                "\"pair_count\",\"valid_count\",\"invalid_count\","
                "\"snr_pass_count\",\"below_noise_count\","
                "\"attempted_delivered_x\",\"attempted_delivered_y\","
                "\"attempted_delivered_abs\",\"attempted_delivered_count\","
                "\"delivered_x\",\"delivered_y\",\"delivered_abs\","
                "\"delivered_energy\",\"observed_x\",\"observed_y\","
                "\"observed_abs\",\"observed_energy\","
                "\"delivered_observed_dot\",\"delivered_observed_cross\","
                "\"sign_agree\",\"sign_disagree\","
                "\"residual_sum\",\"residual_max\",\"residual_count\"]";
            REQUIRE(line.find(expected_columns) != std::string::npos);
        }
    }
    std::cout << "gate25_dense_aggregate_serialized_bytes="
              << aggregate_line_size << '\n';
    REQUIRE(aggregate_line_size > 0);
    REQUIRE(aggregate_line_size <= 32u * 1024u);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_gate25_transport_burst_reports_exact_capacity_and_drops() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::Gate25Anomaly anomaly;
    std::size_t accepted = 0;
    std::size_t dropped = 0;
    for (std::size_t index = 0; index <
         runtime_app::kGate25TransportQueueCapacity + 4; ++index) {
        anomaly.source_frame_id = index + 1;
        if (telemetry.enqueue_gate25_anomaly(anomaly)) ++accepted;
        else ++dropped;
    }
    const auto counters = telemetry.counters();
    REQUIRE(accepted == runtime_app::kGate25TransportQueueCapacity);
    REQUIRE(dropped == 4);
    REQUIRE(counters.gate25_accepted_records == accepted);
    REQUIRE(counters.gate25_dropped_records == dropped);
    REQUIRE(counters.gate25_queue_high_watermark ==
            runtime_app::kGate25TransportQueueCapacity);
}

void test_gate25_transport_holds_aggregate_plus_anomaly_batch() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);

    runtime_app::Gate25AggregateSnapshot aggregate;
    REQUIRE(telemetry.enqueue_gate25_aggregate(aggregate));
    runtime_app::Gate25Anomaly anomaly;
    for (std::size_t index = 0; index < 16; ++index) {
        anomaly.source_frame_id = index + 1;
        REQUIRE(telemetry.enqueue_gate25_anomaly(anomaly));
    }
    const auto counters = telemetry.counters();
    REQUIRE(counters.gate25_accepted_records == 17);
    REQUIRE(counters.gate25_dropped_records == 0);
    REQUIRE(counters.gate25_queue_high_watermark == 17);
    std::cout << "gate25_transport_aggregate_plus_anomaly_batch_accepted="
              << counters.gate25_accepted_records
              << " dropped=" << counters.gate25_dropped_records << '\n';
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
    test_causal_journal_serializes_capture_control_and_provenance();
    test_gate25_records_serialize_separate_keys_and_delivery_join();
    test_gate25_queue_slot_size_is_measured_and_bounded();
    test_gate25_dense_grid_serializes_active_cells_compactly();
    test_gate25_transport_burst_reports_exact_capacity_and_drops();
    test_gate25_transport_holds_aggregate_plus_anomaly_batch();
    return 0;
}
