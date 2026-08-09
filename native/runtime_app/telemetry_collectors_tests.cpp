#include "telemetry_collectors.h"

#include "../pipeline_contract/target_plan.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

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
    value.detector_box_count = 1;
    value.target_source = "yolo_body";
    value.target_tier = "primary";
    value.target_confidence = 0.91f;
    return value;
}

runtime_app::TelemetryTickInput tick(std::uint64_t seq, bool aiming) {
    runtime_app::TelemetryTickInput value;
    value.tick_id = seq;
    value.sample_ns = seq * 4'000'000;
    value.output_sent_ns = value.sample_ns;
    value.output_delivered = true;
    value.aiming = aiming;
    value.physical_connected = true;
    value.current_observed_target_present = true;
    value.output_delivered = true;
    value.output_backend_connected = true;
    value.input_reconnect_count = 2;
    value.output_reconnect_count = 1;
    value.left_trigger = aiming ? 1.0f : 0.0f;
    value.physical_x = 0.2f;
    value.manual_x = 0.18f;
    value.filtered_manual_x = 0.16f;
    value.filtered_manual_y = -0.015f;
    value.manual_confidence = 0.82f;
    value.ai_x = 0.05f;
    value.fresh_vision_validated_manual_proposal_x = -0.04f;
    value.fresh_vision_validated_manual_proposal_y = 0.01f;
    value.fresh_vision_validated_ai_proposal_x = 0.06f;
    value.fresh_vision_validated_ai_proposal_y = -0.02f;
    value.fresh_vision_manual_radial_scale = 0.35f;
    value.fresh_vision_wrong_way_policy_applied = true;
    value.fresh_vision_ai_radial_bound_applied = false;
    value.fresh_vision_ai_radial_scale = 1.0f;
    value.fresh_vision_predictive_envelope_applied = true;
    value.fresh_vision_escape_latched = true;
    value.fresh_vision_authoritative_error_x = 80.0f;
    value.fresh_vision_authoritative_error_y = -12.0f;
    value.fresh_vision_predicted_error_x = 64.0f;
    value.fresh_vision_predicted_error_y = -8.0f;
    value.fresh_vision_raw_manual_radial = -0.04f;
    value.fresh_vision_raw_ai_radial = 0.31f;
    value.fresh_vision_strongest_valid_radial = 0.31f;
    value.fresh_vision_stopping_radial = 0.27f;
    value.fresh_vision_permitted_radial = 0.27f;
    value.fresh_vision_pre_slew_radial = 0.25f;
    value.fresh_vision_final_radial = 0.21f;
    value.fresh_vision_horizon_seconds = 0.18f;
    value.fresh_vision_horizon_y_seconds = 0.128f;
    value.fresh_vision_max_force_x = 0.30f;
    value.fresh_vision_max_force_y = 0.42f;
    value.fresh_vision_envelope_target_x = 0.21f;
    value.fresh_vision_envelope_target_y = -0.04f;
    value.fresh_vision_envelope_reason = "active_response_envelope";
    value.fresh_vision_envelope_source = "bodylock_response_model";
    value.bodylock_error_rate_x = 83.0f;
    value.bodylock_error_rate_y = -12.0f;
    value.bodylock_position_stick_x = -0.22f;
    value.bodylock_position_stick_y = 0.05f;
    value.bodylock_motion_stick_x = 0.31f;
    value.bodylock_motion_stick_y = -0.08f;
    value.bodylock_effective_motion_stick_x = 0.04f;
    value.bodylock_effective_motion_stick_y = -0.08f;
    value.bodylock_radial_motion_bound = true;
    value.bodylock_constraint_reason = "fresh_position_radial_motion_bound";
    value.requested_assist_x = 0.05f;
    value.shaped_assist_x = 0.04f;
    value.post_ai_x = 0.23f;
    value.dynamic_adjustment_x = 0.01f;
    value.post_dynamic_x = 0.24f;
    value.ads_brake_x = -0.02f;
    value.post_ads_brake_x = 0.22f;
    value.ads_carry_brake_x = -0.01f;
    value.post_ads_carry_brake_x = 0.21f;
    value.ads_completion_active = true;
    value.ads_completion_stable_frames = 2;
    value.ads_completion_radius_px = 8.0f;
    value.ads_completion_required_frames = 3;
    value.ads_completion_max_ms = 220.0f;
    value.ads_completion_reason = "none";
    value.manual_takeover_active = true;
    value.auto_fire_requested = true;
    value.auto_fire_aim_ready = false;
    value.auto_fire_allowed = false;
    value.auto_fire_active = false;
    value.auto_fire_pulse_starts = 7;
    value.auto_fire_pulse_pressed = false;
    value.auto_fire_cadence_wait = true;
    value.final_fire_button = false;
    value.auto_fire_block_reason = "aim_not_ready";
    value.pre_recoil_x = 0.23f;
    value.final_x = 0.23f;
    value.observed_error_x = 12.5f;
    value.observed_error_y = -4.0f;
    value.pending_motion_x = 3.25f;
    value.pending_motion_y = -1.5f;
    value.control_error_x = 9.25f;
    value.control_error_y = -2.5f;
    value.pending_motion_confidence = 0.6f;
    value.pending_motion_valid = true;
    value.memory_applied = true;
    value.memory_status = "applied";
    value.selected_track_id = 41;
    value.selected_observation_id = 73;
    value.backing_frame_id = 19;
    value.track_observation_age_ms = 6.25f;
    value.track_position_sigma = 2.5f;
    value.track_ambiguity = 0.125f;
    value.assist_authority = "observed_strong";
    value.assist_authority_reason = "strong_observed";
    value.bodylock_lifecycle = "tracking";
    value.bodylock_transition_reason = "observed";
    value.assist_limit_reason = "step_cap";
    return value;
}

template <typename Emitter>
std::uint64_t observe_until_normal_record_is_accepted(
    runtime_app::RuntimeTelemetry& telemetry,
    Emitter&& emitter) {
    constexpr std::uint64_t kMaxAttempts = 64;
    for (std::uint64_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const auto before = telemetry.counters();
        emitter(attempt);
        const auto after = telemetry.counters();
        if (after.accepted_records > before.accepted_records &&
            after.dropped_normal_records == before.dropped_normal_records) {
            return attempt + 1;
        }
        std::this_thread::yield();
    }
    REQUIRE(false);
    return 0;
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

void test_gate25_disabled_path_has_no_observer_state() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    runtime_app::TelemetryCollectors collectors(false, &telemetry, context);
    REQUIRE(!collectors.gate25_observer_enabled());
    collectors.observe_gate25_observation(runtime_app::Gate25ObservationInput{});
    REQUIRE(collectors.counters().gate25_state_constructions == 0);
    REQUIRE(collectors.counters().gate25_observation_invocations == 0);
}

void test_gate25_enabled_path_reports_real_state_and_writer_invocation() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_collectors_gate25";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.directory = directory;
    options.queue_capacity = 64;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);
    REQUIRE(collectors.gate25_observer_enabled());
    REQUIRE(collectors.counters().gate25_state_constructions == 1);
    collectors.observe_gate25_observation(runtime_app::Gate25ObservationInput{});
    collectors.shutdown(1'000'000);
    telemetry.stop();
    const auto counters = collectors.counters();
    REQUIRE(counters.gate25_observation_invocations == 1);
    REQUIRE(counters.gate25_anomaly_records == 1);
    REQUIRE(counters.gate25_writer_invocations == 1);
    std::filesystem::remove_all(directory);
}

runtime_app::Gate25ObservationInput no_target_gate25_input(
    std::uint64_t frame,
    std::uint64_t present_ns,
    std::uint64_t consume_ns) {
    runtime_app::Gate25ObservationInput value;
    value.source_frame_id = frame;
    value.source_observation_id = frame * 10 + 1;
    value.viewport_sequence = 1;
    value.viewport_source_frame_id = frame;
    value.source_present_qpc = present_ns;
    value.source_present_qpc_frequency = 1'000'000'000ull;
    value.source_present_steady_ns = present_ns;
    value.source_present_calibration_id = 1;
    value.source_present_calibration_uncertainty_ns = 10;
    value.source_present_available = true;
    value.source_present_steady_available = true;
    value.controller_consume_ns = consume_ns;
    value.decision_ns = consume_ns;
    value.output_enabled = true;
    return value;
}

runtime_app::Gate25ObservationInput invalid_geometry_gate25_input(
    std::uint64_t frame,
    std::uint64_t present_ns,
    std::uint64_t consume_ns) {
    auto value = no_target_gate25_input(frame, present_ns, consume_ns);
    value.source_observation_id = frame * 10 + 1;
    value.persistent_target_id = 77;
    value.target_acquisition_id = 5;
    value.viewport_source_frame_id = frame;
    value.stable_body_width = 50.0f;
    value.stable_body_height = 100.0f;
    value.reliability = 1.0f;
    value.target_confidence = 1.0f;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = false;
    value.backend_known = true;
    value.backend_epoch = 1;
    value.output_enabled = true;
    return value;
}

void test_gate25_no_target_five_seconds_is_aggregate_only() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    constexpr std::uint64_t kFramePeriodNs = 5'555'556ull;
    for (std::uint64_t frame = 1; frame <= 900; ++frame) {
        const std::uint64_t present = frame * kFramePeriodNs;
        collectors.observe_gate25_observation(
            no_target_gate25_input(frame, present, present));
    }
    const auto before_shutdown = collectors.counters();
    REQUIRE(before_shutdown.gate25_observation_invocations == 900);
    REQUIRE(before_shutdown.gate25_anomaly_records == 0);
    REQUIRE(before_shutdown.gate25_aggregate_records == 0);
    collectors.shutdown(5'100'000'000ull);
    telemetry.stop();
    const auto after_shutdown = collectors.counters();
    REQUIRE(after_shutdown.gate25_anomaly_records == 0);
    REQUIRE(after_shutdown.gate25_aggregate_records == 1);
}

void test_gate25_missing_present_clock_uses_independent_flush_clock() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    for (std::uint64_t frame = 1; frame <= 200; ++frame) {
        auto value = no_target_gate25_input(
            frame, 0, frame * 6'000'000ull);
        value.source_present_available = false;
        value.source_present_steady_available = false;
        value.source_present_qpc = 0;
        value.source_present_qpc_frequency = 0;
        value.source_present_calibration_id = 0;
        value.source_present_steady_ns = 0;
        collectors.observe_gate25_observation(value);
    }
    collectors.shutdown(2'000'000'000ull);
    telemetry.stop();
    const auto counters = collectors.counters();
    // The first anomaly may flush immediately, but subsequent invalid frames
    // are cadence-limited; this is not one writer record per source frame.
    REQUIRE(counters.gate25_anomaly_records < 40);
}

void test_gate25_one_observation_fans_out_without_rescoring() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);
    const auto* history = collectors.control_history();
    REQUIRE(history != nullptr);
    for (std::uint64_t tick_id = 1; tick_id <= 6; ++tick_id) {
        auto delivery = tick(tick_id, false);
        delivery.sample_ns = tick_id * 1'000'000ull;
        delivery.output_sent_ns = delivery.sample_ns;
        collectors.observe_tick(delivery);
    }
    const auto input = no_target_gate25_input(1, 2'000'000ull, 6'000'000ull);
    for (int i = 0; i < 6; ++i) {
        collectors.observe_gate25_observation(input);
    }
    const auto counters = collectors.counters();
    REQUIRE(counters.gate25_observation_invocations == 1);
    REQUIRE(counters.gate25_fanout_reuses == 5);
    REQUIRE(counters.gate25_anomaly_records == 0);
    REQUIRE(history->size() == 6);
    collectors.shutdown(10'000'000ull);
    telemetry.stop();
}

void test_gate25_gate_only_keeps_history_and_suppresses_standard_collectors() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    options.gate_only = true;
    options.queue_capacity = 2048;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    context.standard_collectors_enabled = false;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    for (std::uint64_t tick_id = 1; tick_id <= 1000; ++tick_id) {
        auto value = tick(tick_id, (tick_id % 37) == 0);
        value.sample_ns = tick_id * 1'000'000ull;
        value.output_sent_ns = value.sample_ns;
        collectors.observe_tick(value);
    }
    REQUIRE(collectors.control_history() == nullptr);
    REQUIRE(collectors.gate25_delivery_push_count() == 1000);
    std::cout << "gate25_gate_only_state_bytes="
              << collectors.gate25_state_bytes()
              << " delivery_view_size="
              << sizeof(runtime_app::Gate25DeliveryView)
              << " ordinary_queue_size="
              << telemetry.ordinary_queue_size()
              << " ordinary_queue_capacity="
              << telemetry.ordinary_queue_capacity()
              << " ordinary_queue_bytes="
              << telemetry.ordinary_queue_bytes()
              << " gate_transport_bytes="
              << telemetry.gate25_transport_queue_bytes() << '\n';

    constexpr std::uint64_t kFramePeriodNs = 5'555'556ull;
    for (std::uint64_t frame = 1; frame <= 900; ++frame) {
        collectors.observe_gate25_observation(
            no_target_gate25_input(
                frame, frame * kFramePeriodNs, frame * 6'000'000ull));
    }
    collectors.shutdown(6'000'000'000ull);
    const auto collector_counts = collectors.counters();
    const auto telemetry_counts = telemetry.counters();
    REQUIRE(collector_counts.delivered_control_records == 0);
    REQUIRE(collector_counts.controller_sample_records == 0);
    REQUIRE(collector_counts.input_event_records == 0);
    REQUIRE(collector_counts.ads_transition_records == 0);
    REQUIRE(collector_counts.target_event_records == 0);
    REQUIRE(collector_counts.control_response_records == 0);
    REQUIRE(collector_counts.committed_capture_records == 0);
    REQUIRE(collector_counts.acquisition_traces == 0);
    REQUIRE(collector_counts.ego_motion_records == 0);
    REQUIRE(collector_counts.gate25_aggregate_records == 1);
    REQUIRE(collector_counts.gate25_anomaly_records == 0);
    // One session metadata record is allowed; no standard normal record is
    // constructed by the gate-only tick/vision path.
    REQUIRE(collector_counts.constructed_records == 1);
    REQUIRE(telemetry_counts.accepted_records == 1);
    REQUIRE(telemetry_counts.gate25_accepted_records == 1);
    REQUIRE(telemetry_counts.gate25_dropped_records == 0);
    REQUIRE(telemetry.ordinary_queue_capacity() ==
            runtime_app::kGate25GateOnlyOrdinaryQueueCapacity);
    REQUIRE(telemetry.ordinary_queue_bytes() ==
            runtime_app::kGate25GateOnlyOrdinaryQueueCapacity *
                sizeof(runtime_app::TelemetryRecord));
    const std::size_t gate_only_full_bytes =
        collectors.gate25_state_bytes() +
        telemetry.ordinary_queue_bytes() +
        telemetry.gate25_transport_queue_bytes();
    std::cout << "gate25_gate_only_full_bytes="
              << gate_only_full_bytes
              << " budget_bytes=" << 128u * 1024u << '\n';
    // This is the measured bounded Gate-only increment: State plus its
    // observer/delivery view, ordinary queue capacity, and dedicated Gate
    // transport. RuntimeTelemetry object/allocator metadata is reported
    // separately by the memory fixture and is not silently folded into this
    // accessor-based sum.
    REQUIRE(gate_only_full_bytes <= 128u * 1024u);

    runtime_app::TelemetrySessionContext detailed_context;
    detailed_context.gate2_5_live_shadow_enabled = true;
    detailed_context.standard_collectors_enabled = true;
    runtime_app::TelemetryCollectors detailed(true, &telemetry, detailed_context);
    for (std::uint64_t tick_id = 2001; tick_id <= 2008; ++tick_id) {
        detailed.observe_tick(tick(tick_id, false));
    }
    const auto detailed_counts = detailed.counters();
    REQUIRE(detailed_counts.delivered_control_records >= 1);
    REQUIRE(detailed_counts.controller_sample_records >= 1);
}

void test_gate25_gate_only_rejects_missing_output_sent_timestamp() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    options.gate_only = true;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    context.standard_collectors_enabled = false;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    auto missing = tick(1, false);
    missing.sample_ns = 1'000'000;
    missing.output_sent_ns = 0;
    collectors.observe_tick(missing);
    REQUIRE(collectors.gate25_delivery_push_count() == 0);
    REQUIRE(collectors.counters().gate25_delivery_timing_rejects == 1);

    auto delivered = tick(2, false);
    delivered.sample_ns = 2'000'000;
    delivered.output_sent_ns = 2'000'000;
    collectors.observe_tick(delivered);
    REQUIRE(collectors.gate25_delivery_push_count() == 1);
    REQUIRE(collectors.counters().gate25_delivery_timing_rejects == 1);
    collectors.shutdown(5'000'000);
    telemetry.stop();
}

void test_gate25_shutdown_conserves_bounded_anomalies_without_writer() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.gate_enabled = true;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    context.standard_collectors_enabled = false;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    constexpr std::size_t kAnomalyInputs = 300;
    for (std::size_t index = 0; index < kAnomalyInputs; ++index) {
        auto input = invalid_geometry_gate25_input(
            1 + index, 1'000'000'000ull,
            1'000'000'000ull);
        if (index == 0) {
            // Establish the compatible source endpoint first. Subsequent
            // distinct observation keys at the same present endpoint are
            // genuine integrity anomalies rather than ordinary geometry
            // rejection and therefore exercise the bounded anomaly ring.
            input.stable_coordinates_valid = true;
            input.viewport_width = 640;
            input.viewport_height = 512;
        }
        // Keep the source endpoint physically identical but publish a distinct
        // observation key. The core must classify this as a same-present
        // integrity anomaly; it is not an ordinary no-target rejection.
        input.source_observation_id = 1000 + index;
        collectors.observe_gate25_observation(input);
    }
    collectors.shutdown(2'000'000'000ull);
    const auto counts = collectors.counters();
    const auto telemetry_counts = telemetry.counters();
    std::cout << "gate25_shutdown_anomaly_counts aggregate="
              << counts.gate25_aggregate_records
              << " aggregate_dropped=" << counts.gate25_aggregate_dropped_records
              << " anomaly=" << counts.gate25_anomaly_records
              << " anomaly_dropped=" << counts.gate25_anomaly_dropped_records
              << " unflushed=" << counts.gate25_unflushed_anomalies << '\n';
    REQUIRE(counts.gate25_aggregate_records == 1);
    REQUIRE(counts.gate25_aggregate_dropped_records == 0);
    REQUIRE(counts.gate25_anomaly_records +
                counts.gate25_anomaly_dropped_records +
                counts.gate25_unflushed_anomalies ==
            runtime_app::Gate25LiveShadow::kAnomalyCapacity);
    REQUIRE(counts.gate25_anomaly_records > 0);
    REQUIRE(counts.gate25_anomaly_dropped_records > 0);
    REQUIRE(counts.gate25_unflushed_anomalies == 0);
    REQUIRE(telemetry_counts.gate25_accepted_records ==
            counts.gate25_aggregate_records + counts.gate25_anomaly_records);
    REQUIRE(telemetry_counts.gate25_dropped_records ==
            counts.gate25_aggregate_dropped_records +
                counts.gate25_anomaly_dropped_records);
    std::cout << "gate25_shutdown_anomaly_conservation accepted="
              << telemetry_counts.gate25_accepted_records
              << " dropped=" << telemetry_counts.gate25_dropped_records
              << " unflushed=" << counts.gate25_unflushed_anomalies << '\n';
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
    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t attempt) {
            collectors.observe_tick(tick(2 + attempt, true));
        });
    std::uint64_t tick_id = 2 + accepted_attempts;
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
    REQUIRE(json.find("\"physical_connected\":true") != std::string::npos);
    REQUIRE(json.find("\"current_observed_target_present\":true") != std::string::npos);
    REQUIRE(json.find("\"filtered_manual_x\":0.16") != std::string::npos);
    REQUIRE(json.find("\"manual_confidence\":0.82") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_wrong_way_policy_applied\":true") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_manual_radial_scale\":0.35") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_predictive_envelope_applied\":true") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_validated_ai_proposal_x\":0.06") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_validated_manual_proposal_x\":-0.04") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_effective_manual_x\":-0.04") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_escape_latched\":true") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_raw_ai_radial\":0.31") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_final_radial\":0.21") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_max_force_y\":0.42") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_envelope_source\":\"bodylock_response_model\"") != std::string::npos);
    REQUIRE(json.find("\"bodylock_error_rate_x\":83") != std::string::npos);
    REQUIRE(json.find("\"bodylock_position_stick_x\":-0.22") != std::string::npos);
    REQUIRE(json.find("\"bodylock_motion_stick_x\":0.31") != std::string::npos);
    REQUIRE(json.find("\"bodylock_effective_motion_stick_x\":0.04") != std::string::npos);
    REQUIRE(json.find("\"bodylock_radial_motion_bound\":true") != std::string::npos);
    REQUIRE(json.find("\"bodylock_constraint_reason\":\"fresh_position_radial_motion_bound\"") != std::string::npos);
    REQUIRE(json.find("\"output_delivered\":true") != std::string::npos);
    REQUIRE(json.find("\"observed_error_x\":12.5") != std::string::npos);
    REQUIRE(json.find("\"observed_error_y\":-4") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_x\":3.25") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_y\":-1.5") != std::string::npos);
    REQUIRE(json.find("\"control_error_x\":9.25") != std::string::npos);
    REQUIRE(json.find("\"control_error_y\":-2.5") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_confidence\":0.6") != std::string::npos);
    REQUIRE(json.find("\"pending_motion_valid\":true") != std::string::npos);
    REQUIRE(json.find("\"memory_applied\":true") != std::string::npos);
    REQUIRE(json.find("\"memory_status\":\"applied\"") != std::string::npos);
    REQUIRE(json.find("\"input_reconnect_count\":2") != std::string::npos);
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
    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t attempt) {
            collectors.observe_tick(tick(1 + attempt, false));
        });
    for (std::uint64_t seq = 1 + accepted_attempts; seq <= 8 + accepted_attempts; ++seq) {
        collectors.observe_tick(tick(seq, false));
    }
    collectors.shutdown(40'000'000);
    telemetry.stop();
    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"controller_target_track_id\":41") != std::string::npos);
    REQUIRE(json.find("\"selected_track_id\":41") != std::string::npos);
    REQUIRE(json.find("\"selected_observation_id\":73") != std::string::npos);
    REQUIRE(json.find("\"track_backing_frame_id\":19") != std::string::npos);
    REQUIRE(json.find("\"track_observation_age_ms\":6.25") != std::string::npos);
    REQUIRE(json.find("\"track_position_sigma\":2.5") != std::string::npos);
    REQUIRE(json.find("\"track_ambiguity\":0.125") != std::string::npos);
    REQUIRE(json.find("\"assist_authority\":\"observed_strong\"") != std::string::npos);
    REQUIRE(json.find("\"assist_authority_reason\":\"strong_observed\"") != std::string::npos);
    REQUIRE(json.find("\"bodylock_lifecycle\":\"tracking\"") != std::string::npos);
    REQUIRE(json.find("\"bodylock_transition_reason\":\"observed\"") != std::string::npos);
    REQUIRE(json.find("\"requested_assist_x\":0.05") != std::string::npos);
    REQUIRE(json.find("\"shaped_assist_x\":0.04") != std::string::npos);
    REQUIRE(json.find("\"assist_limit_reason\":\"step_cap\"") != std::string::npos);
    REQUIRE(json.find("\"target_dx\":30") != std::string::npos);
    REQUIRE(json.find("\"aim_mode\":") != std::string::npos);
    REQUIRE(json.find("\"post_ai_x\":0.23") != std::string::npos);
    REQUIRE(json.find("\"post_dynamic_x\":0.24") != std::string::npos);
    REQUIRE(json.find("\"post_ads_brake_x\":0.22") != std::string::npos);
    REQUIRE(json.find("\"post_ads_carry_brake_x\":0.21") != std::string::npos);
    REQUIRE(json.find("\"manual_takeover_active\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_requested\":true") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_aim_ready\":false") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_allowed\":false") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_active\":false") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_pulse_starts\":7") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_pulse_pressed\":false") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_cadence_wait\":true") != std::string::npos);
    REQUIRE(json.find("\"final_fire_button\":false") != std::string::npos);
    REQUIRE(json.find("\"auto_fire_block_reason\":\"aim_not_ready\"") != std::string::npos);
    REQUIRE(json.find("\"ads_completion_active\":true") != std::string::npos);
    REQUIRE(json.find("\"ads_completion_stable_frames\":2") != std::string::npos);
    REQUIRE(json.find("\"ads_completion_required_frames\":3") != std::string::npos);
    REQUIRE(json.find("\"ads_completion_max_ms\":220") != std::string::npos);
    REQUIRE(json.find("\"detector_box_count\":1") != std::string::npos);
    REQUIRE(json.find("\"production_target_source\":\"yolo_body\"") != std::string::npos);
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

void test_gate25_all_invalid_sources_still_flush_reason_aggregates() {
    const auto geometry_directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_gate25_invalid_geometry";
    std::filesystem::remove_all(geometry_directory);
    runtime_app::RuntimeTelemetryOptions geometry_options;
    geometry_options.enabled = true;
    geometry_options.gate_enabled = true;
    geometry_options.directory = geometry_directory;
    runtime_app::RuntimeTelemetry geometry_telemetry(geometry_options);
    geometry_telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.gate2_5_live_shadow_enabled = true;
    context.standard_collectors_enabled = false;
    runtime_app::TelemetryCollectors geometry(true, &geometry_telemetry, context);
    for (std::uint64_t frame = 1; frame <= 899; ++frame) {
        geometry.observe_gate25_observation(invalid_geometry_gate25_input(
            frame, frame * 5'555'556ull, frame * 6'000'000ull));
    }
    geometry.shutdown(6'000'000'000ull);
    geometry_telemetry.stop();
    REQUIRE(geometry.counters().gate25_aggregate_records == 1);
    REQUIRE(geometry.counters().gate25_anomaly_records == 0);
    std::ifstream geometry_input(geometry_telemetry.log_path());
    std::ostringstream geometry_contents;
    geometry_contents << geometry_input.rdbuf();
    REQUIRE(geometry_contents.str().find("\"invalid_geometry\":899") !=
            std::string::npos);
    REQUIRE(geometry_contents.str().find("\"effect_valid\":0") !=
            std::string::npos);
    REQUIRE(geometry_contents.str().find(
                "\"window_clock_domain\":\"source_present_steady\"") !=
            std::string::npos);
    geometry_input.close();
    std::filesystem::remove_all(geometry_directory);

    const auto missing_directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_gate25_missing_clock";
    std::filesystem::remove_all(missing_directory);
    runtime_app::RuntimeTelemetryOptions missing_options;
    missing_options.enabled = true;
    missing_options.gate_enabled = true;
    missing_options.directory = missing_directory;
    runtime_app::RuntimeTelemetry missing_telemetry(missing_options);
    missing_telemetry.start();
    runtime_app::TelemetryCollectors missing(true, &missing_telemetry, context);
    for (std::uint64_t frame = 1; frame <= 899; ++frame) {
        auto input = no_target_gate25_input(
            frame, 0, frame * 5'555'556ull);
        input.source_present_available = false;
        input.source_present_steady_available = false;
        input.source_present_qpc = 0;
        input.source_present_steady_ns = 0;
        missing.observe_gate25_observation(input);
    }
    missing.shutdown(6'000'000'000ull);
    missing_telemetry.stop();
    REQUIRE(missing.counters().gate25_aggregate_records == 1);
    REQUIRE(missing.counters().gate25_anomaly_records == 1);
    std::ifstream missing_input(missing_telemetry.log_path());
    std::ostringstream missing_contents;
    missing_contents << missing_input.rdbuf();
    REQUIRE(missing_contents.str().find("\"missing_present_clock\":899") !=
            std::string::npos);
    REQUIRE(missing_contents.str().find("\"effect_valid\":0") !=
            std::string::npos);
    REQUIRE(missing_contents.str().find(
                "\"window_clock_domain\":\"collector_monotonic_diagnostic\"") !=
            std::string::npos);
    missing_input.close();
    std::filesystem::remove_all(missing_directory);
}

void test_fresh_envelope_defaults_and_false_values_serialize() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_fresh_defaults";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    auto value = tick(1, true);
    value.fresh_vision_predictive_envelope_applied = false;
    value.fresh_vision_escape_latched = false;
    value.fresh_vision_envelope_reason = "none";
    value.fresh_vision_envelope_source = "unavailable";
    value.fresh_vision_final_radial = 0.0f;
    observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t attempt) {
            auto retry = value;
            retry.tick_id += attempt;
            retry.sample_ns += attempt * 4'000'000;
            retry.output_sent_ns = retry.sample_ns;
            collectors.observe_tick(retry);
        });
    collectors.shutdown(8'000'000);
    telemetry.stop();
    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"fresh_vision_predictive_envelope_applied\":false") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_escape_latched\":false") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_final_radial\":0") != std::string::npos);
    REQUIRE(json.find("\"fresh_vision_envelope_source\":\"unavailable\"") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

runtime_app::TelemetryAcquisitionTraceInput acquisition_trace() {
    runtime_app::TelemetryAcquisitionTraceInput value;
    value.source_frame_id = 77;
    value.source_observation_id = 9001;
    value.persistent_target_id = 12;
    value.physical_ads_epoch = 4;
    value.target_acquisition_id = 8;
    value.controller_tick_id = 31;
    value.capture_acquire_begin_ns = 1'000;
    value.capture_acquire_complete_ns = 1'100;
    value.capture_copy_complete_ns = 1'200;
    value.accumulated_frames = 3;
    value.ads_acquisition_begin_ns = 1'250;
    value.ads_acquisition_complete_ns = 0;
    value.result_ready_ns = 2'000;
    value.vision_publish_ns = 2'100;
    value.vision_publish_available = true;
    value.controller_submit_complete_ns = 2'210;
    value.controller_consume_ns = 2'200;
    value.plan_decision_ns = 2'250;
    value.final_output_ready_ns = 2'300;
    value.first_requested_ai_ns = 2'260;
    value.first_shaped_ai_ns = 2'261;
    value.first_fused_output_ns = 2'262;
    value.vigem_submit_complete_ns = 2'400;
    value.plan_admitted = true;
    value.acquisition_state = static_cast<std::uint8_t>(
        pipeline_contract::AdsAcquisitionState::AcquiringNominal);
    value.decision_reason = static_cast<std::uint8_t>(
        pipeline_contract::AdsDecisionReason::Admitted);
    value.source_decision_available = true;
    value.source_decision_outcome = static_cast<std::uint8_t>(
        pipeline_contract::SourceDecisionOutcome::Admitted);
    value.source_decision_reason = static_cast<std::uint8_t>(
        pipeline_contract::AdsDecisionReason::Admitted);
    value.selector_target_generation = 7;
    value.candidate_count = 2;
    value.preferred_source_id = 9001;
    value.selected_source_id = 9001;
    value.effective_activation_radius_px = 135.0f;
    value.raw_error_x = 18.0f;
    value.raw_error_y = -7.0f;
    value.target_size_x = 40.0f;
    value.target_size_y = 120.0f;
    value.requested_ai_x = 0.25f;
    value.shaped_ai_x = 0.20f;
    value.fused_output_x = 0.18f;
    value.post_output_x = 0.18f;
    value.has_first_requested_ai = true;
    value.has_first_shaped_ai = true;
    value.has_first_fused_output = true;
    value.first_requested_ai_x = 0.25f;
    value.first_shaped_ai_x = 0.20f;
    value.first_fused_output_x = 0.18f;
    // Keep both raw QPC and the calibrated steady projection so this fixture
    // exercises the two distinct clock-domain fields.
    value.source_present_available = true;
    value.source_present_qpc = 900;
    value.source_present_qpc_frequency = 10'000'000;
    value.source_present_steady_ns = 1'500;
    value.source_present_calibration_id = 41;
    value.source_present_calibration_uncertainty_ns = 8;
    value.source_present_steady_available = true;
    return value;
}

void test_acquisition_trace_is_fixed_joinable_and_disabled_is_inert() {
    const auto trace = acquisition_trace();
    REQUIRE(trace.capture_acquire_begin_ns <= trace.capture_acquire_complete_ns);
    REQUIRE(trace.capture_acquire_complete_ns <= trace.capture_copy_complete_ns);
    REQUIRE(trace.capture_copy_complete_ns <= trace.result_ready_ns);
    REQUIRE(trace.result_ready_ns <= trace.vision_publish_ns);
    REQUIRE(trace.vision_publish_ns <= trace.controller_consume_ns);
    REQUIRE(trace.controller_consume_ns <= trace.plan_decision_ns);
    REQUIRE(trace.plan_decision_ns <= trace.final_output_ready_ns);
    REQUIRE(trace.final_output_ready_ns <= trace.vigem_submit_complete_ns);
    runtime_app::RuntimeTelemetryOptions disabled_options;
    disabled_options.enabled = false;
    runtime_app::RuntimeTelemetry disabled_telemetry(disabled_options);
    runtime_app::TelemetryCollectors disabled(false, &disabled_telemetry);
    disabled.observe_acquisition_trace(trace);
    REQUIRE(disabled.counters().acquisition_traces == 0);
    REQUIRE(disabled_telemetry.counters().accepted_records == 0);

    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_acquisition_trace";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 64;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t) {
            collectors.observe_acquisition_trace(trace);
        });
    REQUIRE(collectors.counters().acquisition_traces == accepted_attempts);
    collectors.shutdown(5'000'000);
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"type\":\"ads_acquisition_trace\"") != std::string::npos);
    REQUIRE(json.find("\"schema\":\"ads_acquisition_trace_v3\"") != std::string::npos);
    REQUIRE(json.find("\"source_frame_id\":77") != std::string::npos);
    REQUIRE(json.find("\"source_observation_id\":9001") != std::string::npos);
    REQUIRE(json.find("\"persistent_target_id\":12") != std::string::npos);
    REQUIRE(json.find("\"physical_ads_epoch\":4") != std::string::npos);
    REQUIRE(json.find("\"target_acquisition_id\":8") != std::string::npos);
    REQUIRE(json.find("\"controller_tick_id\":31") != std::string::npos);
    REQUIRE(json.find("\"capture_acquire_begin_ns\":1000") != std::string::npos);
    REQUIRE(json.find("\"capture_acquire_complete_ns\":1100") != std::string::npos);
    REQUIRE(json.find("\"capture_copy_complete_ns\":1200") != std::string::npos);
    REQUIRE(json.find("\"accumulated_frames\":3") != std::string::npos);
    REQUIRE(json.find("\"ads_acquisition_begin_ns\":1250") != std::string::npos);
    REQUIRE(json.find("\"vision_publish_available\":true") != std::string::npos);
    REQUIRE(json.find("\"controller_submit_complete_ns\":2210") != std::string::npos);
    REQUIRE(json.find("\"plan_decision_ns\":2250") != std::string::npos);
    REQUIRE(json.find("\"plan_decision_available\":true") != std::string::npos);
    REQUIRE(json.find("\"final_output_ready_ns\":2300") != std::string::npos);
    REQUIRE(json.find("\"source_decision_outcome\":\"admitted\"") != std::string::npos);
    REQUIRE(json.find("\"selector_target_generation\":7") != std::string::npos);
    REQUIRE(json.find("\"acquisition_state\":\"acquiring_nominal\"") != std::string::npos);
    REQUIRE(json.find("\"decision_reason\":\"admitted\"") != std::string::npos);
    REQUIRE(json.find("\"source_present_available\":true") != std::string::npos);
    REQUIRE(json.find("\"source_present_qpc\":900") != std::string::npos);
    REQUIRE(json.find("\"source_present_qpc_frequency\":10000000") !=
            std::string::npos);
    REQUIRE(json.find("\"source_present_clock_domain\":\"qpc\"") !=
            std::string::npos);
    REQUIRE(json.find(
                "\"source_present_steady_clock_domain\":\"qpc_to_steady_calibrated\"") !=
            std::string::npos);
    REQUIRE(json.find("\"source_present_steady_ns\":1500") !=
            std::string::npos);
    REQUIRE(json.find("\"source_present_calibration_id\":41") !=
            std::string::npos);
    REQUIRE(json.find(
                "\"source_present_calibration_uncertainty_ns\":8") !=
            std::string::npos);
    REQUIRE(json.find("\"source_present_steady_available\":true") !=
            std::string::npos);
    REQUIRE(json.find("\"effective_activation_radius_px\":135") != std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_ego_motion_shadow_is_joinable_and_fixed_rate_independent() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_ego_motion";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    runtime_app::TelemetryEgoMotionShadowInput input;
    input.available = true;
    input.valid = true;
    input.result_sequence = 4;
    input.previous_frame_id = 11;
    input.current_frame_id = 12;
    input.previous_present_qpc = 100;
    input.current_present_qpc = 140;
    input.previous_present_qpc_frequency = 10'000'000;
    input.current_present_qpc_frequency = 10'000'000;
    input.present_qpc_frequency = 10'000'000;
    input.previous_present_steady_ns = 10'000;
    input.current_present_steady_ns = 14'000;
    input.previous_present_calibration_id = 41;
    input.current_present_calibration_id = 42;
    input.previous_present_calibration_uncertainty_ns = 8;
    input.current_present_calibration_uncertainty_ns = 9;
    input.previous_present_steady_available = true;
    input.current_present_steady_available = true;
    input.present_clock_valid = true;
    input.previous_capture_copy_complete_ns = 1'200;
    input.current_capture_copy_complete_ns = 1'600;
    input.previous_result_ns = 1'000;
    input.current_result_ns = 2'000;
    input.observer_completed_at_ns = 2'100;
    input.result_age_at_take_ns = 300;
    input.boundary_consistent_hit_count = 4;
    input.boundary_consistent_hit_rate = 0.05f;
    input.observer_lifecycle_generation = 2;
    input.background_dx = 2.0f;
    input.camera_dx = -2.0f;
    input.confidence = 0.8f;
    input.valid_background_ratio = 0.65f;
    input.inlier_count = 42;
    input.sample_count = 60;
    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t) {
            collectors.observe_ego_motion_shadow(12, 99, input);
        });
    REQUIRE(collectors.counters().ego_motion_records == accepted_attempts);
    collectors.shutdown(3'000);
    telemetry.stop();
    std::ifstream input_file(telemetry.log_path());
    std::ostringstream contents;
    contents << input_file.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"type\":\"ego_motion_shadow\"") != std::string::npos);
    REQUIRE(json.find("\"schema\":\"ego_motion_shadow_v2\"") != std::string::npos);
    REQUIRE(json.find("\"previous_frame_id\":11") != std::string::npos);
    REQUIRE(json.find("\"current_frame_id\":12") != std::string::npos);
    REQUIRE(json.find("\"present_qpc_frequency\":10000000") != std::string::npos);
    REQUIRE(json.find("\"previous_present_qpc\":100") != std::string::npos);
    REQUIRE(json.find("\"current_present_qpc\":140") != std::string::npos);
    REQUIRE(json.find("\"previous_present_qpc_frequency\":10000000") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_qpc_frequency\":10000000") !=
            std::string::npos);
    REQUIRE(json.find("\"previous_present_steady_ns\":10000") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_steady_ns\":14000") !=
            std::string::npos);
    REQUIRE(json.find("\"previous_present_calibration_id\":41") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_calibration_id\":42") !=
            std::string::npos);
    REQUIRE(json.find(
                "\"previous_present_calibration_uncertainty_ns\":8") !=
            std::string::npos);
    REQUIRE(json.find(
                "\"current_present_calibration_uncertainty_ns\":9") !=
            std::string::npos);
    REQUIRE(json.find("\"present_clock_valid\":true") != std::string::npos);
    REQUIRE(json.find("\"present_clock_domain\":\"qpc_to_steady_calibrated\"") !=
            std::string::npos);
    REQUIRE(json.find("\"previous_present_steady_available\":true") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_steady_available\":true") !=
            std::string::npos);
    REQUIRE(json.find("\"observer_completed_at_ns\":2100") != std::string::npos);
    REQUIRE(json.find("\"observer_lifecycle_generation\":2") !=
            std::string::npos);
    input_file.close();
    std::filesystem::remove_all(directory);
}

void test_ego_motion_shadow_invalid_clock_stays_unjoinable() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_ego_motion_invalid_clock";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);
    runtime_app::TelemetryEgoMotionShadowInput input;
    input.available = true;
    input.valid = true;
    input.result_sequence = 5;
    input.previous_frame_id = 21;
    input.current_frame_id = 22;
    input.previous_present_qpc = 2'100;
    input.current_present_qpc = 2'140;
    input.previous_present_qpc_frequency = 10'000'000;
    input.current_present_qpc_frequency = 10'000'000;
    input.present_qpc_frequency = 10'000'000;
    input.previous_present_steady_ns = 21'000;
    input.current_present_steady_ns = 21'400;
    // Numeric endpoint values are retained for diagnostics, but without
    // endpoint availability and calibration ids they are not join evidence.
    input.previous_present_calibration_id = 0;
    input.current_present_calibration_id = 0;
    input.previous_present_steady_available = false;
    input.current_present_steady_available = false;
    input.present_clock_valid = false;
    input.observer_completed_at_ns = 22'100;
    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t) {
            collectors.observe_ego_motion_shadow(22, 100, input);
        });
    REQUIRE(accepted_attempts >= 1);
    collectors.shutdown(3'000);
    telemetry.stop();

    std::ifstream input_file(telemetry.log_path());
    std::ostringstream contents;
    contents << input_file.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"present_clock_valid\":false") !=
            std::string::npos);
    REQUIRE(json.find("\"present_clock_domain\":\"unavailable\"") !=
            std::string::npos);
    REQUIRE(json.find("\"previous_present_steady_available\":false") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_steady_available\":false") !=
            std::string::npos);
    REQUIRE(json.find("\"previous_present_calibration_id\":0") !=
            std::string::npos);
    REQUIRE(json.find("\"current_present_calibration_id\":0") !=
            std::string::npos);
    input_file.close();
    std::filesystem::remove_all(directory);
}

void test_causal_shadow_serializes_motion_buckets_and_final_output() {
    REQUIRE(runtime_app::kTelemetrySchemaVersion == 13);
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_causal_shadow_v2";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetryCollectors collectors(true, &telemetry);

    pipeline_contract::CommittedCaptureObservation observation;
    observation.source_frame_id = 88;
    observation.source_observation_id = 8801;
    observation.persistent_target_id = 12;
    observation.viewport_source_frame_id = 88;
    observation.captured_at_ns = 1'000'000;
    observation.result_at_ns = 2'000'000;
    observation.controller_consume_ns = 3'000'000;
    observation.stable_body_size_px = {40.0f, 120.0f};
    observation.stable_coordinates_valid = true;
    observation.reliability = 0.9f;
    observation.normalized_size = 0.2f;
    observation.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    observation.mode = pipeline_contract::ControlMode::BodyLockFollow;
    observation.ads_epoch = 4;
    observation.fresh_observed = true;
    observation.strong_observation = true;
    observation.eligible_candidate_count = 1;

    control_learning::SampleAssessment assessment;
    assessment.vision_quality = control_learning::VisionSampleQuality::Normal;
    assessment.update_outcome =
        control_learning::IdentificationUpdateOutcome::Accepted;
    assessment.accepted_by_any_delay = true;
    assessment.accepted_delay_count = 3;

    control_learning::CausalResponseEstimate estimate;
    estimate.selected_delay_ms = 40.0f;
    estimate.selected_delay_confidence = 0.8f;
    estimate.right_confidence = 0.75f;
    estimate.left_confidence = 0.7f;

    control_learning::PendingMotionEstimate pending;
    pending.realized_px = {1.25, -2.5};
    pending.in_flight_px = {3.5, 4.5};
    pending.scheduled_px = {5.5, 6.5};
    pending.pending_total_px = {9.0, 11.0};
    pending.confidence = 0.7f;
    pending.history_complete = true;
    pending.valid = true;

    control_learning::RolloutResult rollout;
    rollout.candidate_count = 1;
    rollout.candidates[0] = {1.0f, 2.0};
    rollout.best_scale = 1.0f;
    rollout.confidence = 0.7f;
    rollout.valid = true;
    const control_learning::Vec2d final_output{0.42, -0.17};

    const auto accepted_attempts = observe_until_normal_record_is_accepted(
        telemetry,
        [&](std::uint64_t attempt) {
            auto retry = observation;
            retry.source_frame_id += attempt;
            retry.viewport_source_frame_id = retry.source_frame_id;
            collectors.observe_causal_shadow(
                retry, assessment, estimate, pending, rollout, final_output);
        });
    REQUIRE(accepted_attempts >= 1);
    collectors.shutdown(5'000'000);
    telemetry.stop();

    std::ifstream input(telemetry.log_path());
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string json = contents.str();
    REQUIRE(json.find("\"schema\":\"causal_response_shadow_v3\"") !=
            std::string::npos);
    REQUIRE(json.find("\"pending_realized\":[1.25,-2.5]") !=
            std::string::npos);
    REQUIRE(json.find("\"pending_in_flight\":[3.5,4.5]") !=
            std::string::npos);
    REQUIRE(json.find("\"pending_scheduled\":[5.5,6.5]") !=
            std::string::npos);
    REQUIRE(json.find("\"pending_total\":[9,11]") !=
            std::string::npos);
    REQUIRE(json.find("\"rollout_uses_final_output\":true") !=
            std::string::npos);
    REQUIRE(json.find("\"rollout_final_output\":[0.42,-0.17]") !=
            std::string::npos);
    input.close();
    std::filesystem::remove_all(directory);
}

void test_delivered_control_persistence_is_sampled_below_controller_rate() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_telemetry_delivered_sampling";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 2048;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    runtime_app::TelemetrySessionContext context;
    context.telemetry_hz = 250;
    runtime_app::TelemetryCollectors collectors(true, &telemetry, context);

    for (std::uint64_t index = 1; index <= 1000; ++index) {
        auto value = tick(index, false);
        value.sample_ns = index * 1'000'000;
        value.output_sent_ns = value.sample_ns;
        collectors.observe_tick(value);
    }
    const auto counters = collectors.counters();
    REQUIRE(counters.delivered_control_records >= 240);
    REQUIRE(counters.delivered_control_records <= 260);
    REQUIRE(collectors.control_history() != nullptr);
    REQUIRE(collectors.control_history()->size() >= 990);
    collectors.shutdown(1'100'000'000);
    telemetry.stop();
    std::filesystem::remove_all(directory);
}
}

int main() {
    test_disabled_collectors_have_zero_transitions();
    test_gate25_disabled_path_has_no_observer_state();
    test_gate25_enabled_path_reports_real_state_and_writer_invocation();
    test_gate25_no_target_five_seconds_is_aggregate_only();
    test_gate25_missing_present_clock_uses_independent_flush_clock();
    test_gate25_all_invalid_sources_still_flush_reason_aggregates();
    test_gate25_one_observation_fans_out_without_rescoring();
    test_gate25_gate_only_keeps_history_and_suppresses_standard_collectors();
    test_gate25_gate_only_rejects_missing_output_sent_timestamp();
    test_gate25_shutdown_conserves_bounded_anomalies_without_writer();
    test_enabled_collectors_write_profile_and_ads_evidence();
    test_fresh_envelope_defaults_and_false_values_serialize();
    test_controller_samples_include_current_target_context();
    test_ads_completeness_uses_observed_sequence_not_capture_frame_id();
    test_acquisition_trace_is_fixed_joinable_and_disabled_is_inert();
    test_delivered_control_persistence_is_sampled_below_controller_rate();
    test_ego_motion_shadow_is_joinable_and_fixed_rate_independent();
    test_ego_motion_shadow_invalid_clock_stays_unjoinable();
    test_causal_shadow_serializes_motion_buckets_and_final_output();
    return 0;
}
