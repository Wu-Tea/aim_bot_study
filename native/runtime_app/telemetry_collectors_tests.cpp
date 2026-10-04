#include "telemetry_collectors.h"
#include "test_support/native_test_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, int line) {
    if (!value) throw std::runtime_error(
        "telemetry collector assertion failed at line " +
        std::to_string(line));
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::TelemetrySessionContext session_context() {
    runtime_app::TelemetrySessionContext context;
    context.build_commit = "collector-test-revision";
    context.capture_width = 640;
    context.capture_height = 640;
    context.active_capture_fps = 180;
    context.idle_capture_fps = 30;
    context.controller_tick_hz = 1000;
    context.telemetry_hz = 250;
    return context;
}

runtime_app::TelemetryTickInput tick(std::uint64_t tick_id, bool aiming = true) {
    runtime_app::TelemetryTickInput value;
    value.tick_id = tick_id;
    value.physical_read_ns = tick_id * 1'000'000;
    value.controller_consume_ns = value.physical_read_ns + 100'000;
    value.output_sent_ns = value.physical_read_ns + 200'000;
    value.sample_ns = value.output_sent_ns;
    value.aiming = aiming;
    value.controller.physical_connected = true;
    value.controller.current_observed_target_present = true;
    value.controller.output_delivered = true;
    value.controller.output_backend_connected = true;
    value.controller.aim_authority = true;
    value.controller.fire_authority = true;
    value.aim_mode = "ads";
    value.controller.physical_x = 0.20f;
    value.controller.manual_x = 0.18f;
    value.controller.filtered_manual_x = 0.16f;
    value.controller.manual_confidence = 0.90f;
    value.controller.ai_x = 0.05f;
    value.controller.target_final_x = 0.22f;
    value.controller.ai_correction_x = 0.04f;
    value.manual_authority_mode = "micro_manual";
    value.assist_control_phase = "handover_brake";
    value.controller.manual_passthrough_x = false;
    value.controller.manual_correction_y = true;
    value.controller.manual_boundary_y = true;
    value.controller.handover_requested = true;
    value.controller.handover_braking = true;
    value.controller.enemy_mark_request_pending = true;
    value.controller.enemy_mark_synthetic_pressed = true;
    value.controller.enemy_mark_fired = true;
    value.controller.enemy_mark_confirmation_frames = 2;
    value.controller.enemy_mark_target_scope = 7;
    value.controller.enemy_mark_target_generation = 91;
    value.controller.enemy_mark_last_scope = 7;
    value.controller.enemy_mark_last_generation = 91;
    value.enemy_mark_block_reason = "none";
    value.controller.bodylock_position_stick_x = 0.04f;
    value.controller.bodylock_motion_stick_x = 0.02f;
    value.controller.bodylock_effective_motion_stick_x = 0.01f;
    value.controller.bodylock_radial_motion_bound = true;
    value.bodylock_constraint_reason = "current_error_radial_bound";
    value.controller.requested_assist_x = 0.08f;
    value.controller.shaped_assist_x = 0.05f;
    value.controller.final_x = 0.23f;
    value.controller.observed_error_x = 12.0f;
    value.controller.control_error_x = 12.0f;
    value.controller.source_aim_x = 332.0f;
    value.controller.source_aim_y = 304.0f;
    value.controller.desired_aim_x = 332.0f;
    value.controller.desired_aim_y = 332.0f;
    value.controller.desired_point_u = 0.5f;
    value.controller.desired_point_v = 0.75f;
    value.controller.aim_region_x1 = 300.0f;
    value.controller.aim_region_y1 = 280.0f;
    value.controller.aim_region_x2 = 364.0f;
    value.controller.aim_region_y2 = 348.0f;
    value.controller.has_aim_region = true;
    value.controller.visual_authority = 0.83f;
    value.controller.enemy_cue_current = true;
    value.controller.enemy_identity_confirmed = true;
    value.controller.enemy_cue_checked = true;
    value.aim_region_source = "vision_geometry";
    value.desired_point_source = "user_corrected";
    value.controller.selected_track_id = 71;
    value.controller.selected_observation_id = 91;
    value.assist_authority = "aim_and_fire";
    value.assist_authority_reason = "fresh_observed";
    value.bodylock_lifecycle = "active";
    value.assist_limit_reason = "radial_bound";
    return value;
}

runtime_app::TelemetryVisionInput vision(std::uint64_t frame_id) {
    runtime_app::TelemetryVisionInput value;
    value.frame_id = frame_id;
    value.captured_at_ns = frame_id * 1'000'000;
    value.inferred_at_ns = value.captured_at_ns + 300'000;
    value.result_at_ns = value.captured_at_ns + 400'000;
    value.controller_consume_ns = value.captured_at_ns + 600'000;
    value.frame_width = 640;
    value.frame_height = 640;
    value.has_target = true;
    value.live = true;
    value.aiming = true;
    value.x1 = 280.0f;
    value.y1 = 240.0f;
    value.x2 = 360.0f;
    value.y2 = 400.0f;
    value.target_x = 332.0f;
    value.target_y = 316.0f;
    value.screen_center_x = 320.0f;
    value.screen_center_y = 320.0f;
    value.detector_box_count = 1;
    value.target_source = "production_vision";
    value.target_tier = "fresh_observed";
    value.target_confidence = 0.93f;
    return value;
}

pipeline_contract::CommittedCaptureObservation committed_observation(
    std::uint64_t frame_id) {
    pipeline_contract::CommittedCaptureObservation value;
    value.source_frame_id = frame_id;
    value.source_observation_id = frame_id + 100;
    value.persistent_target_id = 71;
    value.viewport_sequence = 3;
    value.viewport_source_frame_id = frame_id;
    value.captured_at_ns = frame_id * 1'000'000;
    value.result_at_ns = value.captured_at_ns + 400'000;
    value.controller_consume_ns = value.captured_at_ns + 600'000;
    value.stable_error_px = {12.0f, -4.0f};
    value.stable_body_size_px = {80.0f, 160.0f};
    value.raw_body_box_px = {280.0f, 240.0f, 80.0f, 160.0f};
    value.viewport_offset_px = {0.0f, 0.0f};
    value.target_acceleration_px_per_sec2 = {0.0f, 0.0f};
    value.reliability = 0.93f;
    value.normalized_size = 0.20f;
    value.eligible_candidate_count = 1;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = true;
    return value;
}

runtime_app::TelemetryAcquisitionTraceInput acquisition_trace(
    std::uint64_t frame_id) {
    runtime_app::TelemetryAcquisitionTraceInput value;
    value.source_frame_id = frame_id;
    value.source_observation_id = frame_id + 100;
    value.persistent_target_id = 71;
    value.physical_ads_epoch = 2;
    value.target_acquisition_id = 4;
    value.controller_tick_id = 9;
    value.capture_acquire_begin_ns = 1'000'000;
    value.capture_acquire_complete_ns = 1'100'000;
    value.capture_copy_complete_ns = 1'200'000;
    value.result_ready_ns = 1'500'000;
    value.vision_publish_ns = 1'600'000;
    value.controller_consume_ns = 1'700'000;
    value.plan_decision_ns = 1'800'000;
    value.final_output_ready_ns = 1'900'000;
    value.vigem_submit_complete_ns = 2'000'000;
    value.first_effect_observed_ns = 2'100'000;
    value.source_present_calibration_id = 17;
    value.source_present_calibration_uncertainty_ns = 3000;
    value.vision_publish_available = true;
    value.plan_admitted = true;
    value.acquisition_active = true;
    value.acquisition_exists = true;
    value.candidate_count = 1;
    value.selector_target_generation = 6;
    value.effective_activation_radius_px = 48.0f;
    value.raw_error_x = 12.0f;
    value.target_size_x = 80.0f;
    value.target_size_y = 160.0f;
    value.requested_ai_x = 0.08f;
    value.shaped_ai_x = 0.05f;
    value.fused_output_x = 0.23f;
    value.post_output_x = 0.23f;
    return value;
}

std::string read_jsonl(const std::filesystem::path& directory) {
    std::ostringstream text;
    if (!std::filesystem::exists(directory)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        text << input.rdbuf();
    }
    return text.str();
}

void test_disabled_collectors_have_zero_side_effects() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryCollectors collectors(false, &telemetry, session_context());
    REQUIRE(!collectors.enabled());
    collectors.observe_new_vision(vision(1));
    collectors.observe_tick(tick(1));
    collectors.observe_committed_capture(committed_observation(1));
    collectors.observe_acquisition_trace(acquisition_trace(1));
    collectors.shutdown(2'000'000);
    const auto counters = collectors.counters();
    REQUIRE(counters.constructed_records == 0);
    REQUIRE(counters.state_transitions == 0);
    REQUIRE(telemetry.counters().accepted_records == 0);
}

void test_collectors_emit_only_current_contract_records() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_collectors_current";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 128;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryCollectors collectors(true, &telemetry, session_context());
    telemetry.start();

    collectors.observe_new_vision(vision(7));
    collectors.observe_tick(tick(9));
    collectors.observe_committed_capture(committed_observation(7));
    collectors.observe_acquisition_trace(acquisition_trace(7));
    collectors.shutdown(20'000'000);
    telemetry.stop();

    const auto counters = collectors.counters();
    REQUIRE(counters.controller_sample_records >= 1);
    REQUIRE(counters.target_event_records == 1);
    REQUIRE(counters.committed_capture_records == 1);
    REQUIRE(counters.acquisition_traces == 1);
    REQUIRE(counters.delivered_control_records == 1);
    REQUIRE(counters.constructed_records >= 6);

    const std::string text = read_jsonl(directory);
    REQUIRE(text.find("\"type\":\"session_metadata\"") != std::string::npos);
    REQUIRE(text.find("\"build_commit\":\"collector-test-revision\"") != std::string::npos);
    REQUIRE(text.find("\"type\":\"target_event\"") != std::string::npos);
    REQUIRE(text.find("\"assist_control_phase\":\"handover_brake\"") != std::string::npos);
    REQUIRE(text.find("\"handover_requested\":true") != std::string::npos);
    REQUIRE(text.find("\"manual_correction_y\":true") != std::string::npos);
    REQUIRE(text.find("\"desired_aim\":[332,332]") != std::string::npos);
    REQUIRE(text.find("\"aim_region\":[300,280,364,348]") != std::string::npos);
    REQUIRE(text.find("\"aim_region_source\":\"vision_geometry\"") != std::string::npos);
    REQUIRE(text.find("\"desired_point_source\":\"user_corrected\"") != std::string::npos);
    REQUIRE(text.find("\"visual_authority\":0.83") != std::string::npos);
    REQUIRE(text.find("\"enemy_cue_current\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_identity_confirmed\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_cue_checked\":true") != std::string::npos);
    REQUIRE(text.find("\"requested_assist_x\":0.08") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_fired\":true") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_target_scope\":7") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_target_generation\":91") != std::string::npos);
    REQUIRE(text.find("\"enemy_mark_block_reason\":\"none\"") != std::string::npos);
    REQUIRE(text.find("\"production_target_source\":\"production_vision\"") != std::string::npos);
    REQUIRE(text.find("\"schema\":\"committed_capture_observation_v1\"") != std::string::npos);
    REQUIRE(text.find("\"schema\":\"ads_acquisition_trace_v3\"") != std::string::npos);
    REQUIRE(text.find("\"first_effect_observed_ns\":2100000") != std::string::npos);
    REQUIRE(text.find("\"source_present_calibration_id\":17") != std::string::npos);
    REQUIRE(text.find("\"source_present_calibration_uncertainty_ns\":3000") != std::string::npos);
    REQUIRE(text.find("\"schema\":\"delivered_control_sample_v2\"") != std::string::npos);
    REQUIRE(text.find("projected") == std::string::npos);
    REQUIRE(text.find("causal") == std::string::npos);
    REQUIRE(text.find("gate25") == std::string::npos);
    std::filesystem::remove_all(directory);
}

void test_invalid_or_duplicate_committed_capture_is_not_persisted() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.start_writer = false;
    options.queue_capacity = 16;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryCollectors collectors(true, &telemetry, session_context());
    auto invalid = committed_observation(11);
    invalid.controller_consume_ns = 0;
    collectors.observe_committed_capture(invalid);
    REQUIRE(collectors.counters().committed_capture_records == 0);

    const auto valid = committed_observation(11);
    collectors.observe_committed_capture(valid);
    collectors.observe_committed_capture(valid);
    REQUIRE(collectors.counters().committed_capture_records == 2);
    REQUIRE(telemetry.counters().duplicate_source_frames == 1);
    REQUIRE(telemetry.counters().accepted_records == 2);
}

void test_failed_delivery_preserves_receipt_and_controller_units() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_collectors_failed_delivery";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryCollectors collectors(true, &telemetry, session_context());
    telemetry.start();
    auto failed = tick(9);
    failed.controller.output_delivered = false;
    failed.controller.output_backend_connected = false;
    failed.controller.output_error_code = 55;
    failed.controller.final_x = -0.375f;
    failed.controller.bodylock_motion_stick_y = 0.125f;
    collectors.observe_tick(failed);
    collectors.shutdown(20'000'000);
    telemetry.stop();
    const std::string text = read_jsonl(directory);
    REQUIRE(text.find("\"output_delivered\":false") != std::string::npos);
    REQUIRE(text.find("\"output_backend_connected\":false") != std::string::npos);
    REQUIRE(text.find("\"output_error_code\":55") != std::string::npos);
    REQUIRE(text.find("\"bodylock_motion_stick_y\":0.125") != std::string::npos);
    REQUIRE(text.find("\"applied_at_ns\":9200000") != std::string::npos);
    REQUIRE(text.find("\"final_right\":[-0.375,0]") != std::string::npos);
    REQUIRE(collectors.counters().delivered_control_records == 1);
    std::filesystem::remove_all(directory);
}

} // namespace

void register_telemetry_collectors_tests(native_test::Registry& registry) {
    registry.add_case("FeatureTelemetryAndDiagnostics", "disabled_collectors_have_zero_side_effects", test_disabled_collectors_have_zero_side_effects);
    registry.add_case("FeatureTelemetryAndDiagnostics", "collectors_emit_only_current_contract_records", test_collectors_emit_only_current_contract_records);
    registry.add_case("FeatureTelemetryAndDiagnostics", "invalid_or_duplicate_capture_not_persisted", test_invalid_or_duplicate_committed_capture_is_not_persisted);
    registry.add_case("FeatureTelemetryAndDiagnostics", "failed_delivery_preserves_receipt_and_units", test_failed_delivery_preserves_receipt_and_controller_units);
}
