#include "runtime_loop.h"
#include "runtime_timing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace runtime_app {
namespace {

double elapsed_ms_between_ns(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) {
        return 0.0;
    }
    return static_cast<double>(end_ns - start_ns) / 1'000'000.0;
}

double elapsed_ms_or_invalid(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) return -1.0;
    return static_cast<double>(end_ns - start_ns) / 1'000'000.0;
}

const char* safe_c_string(const char* value, const char* fallback) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

float capture_transfer_ms(const vision_native::VisionResult& result) {
    return std::max(0.0f, result.wait_ms - result.capture_acquire_ms);
}

unsigned int environment_uint_or(const char* name, unsigned int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed == 0ul) {
        return fallback;
    }
    return static_cast<unsigned int>(
        std::min<unsigned long>(parsed, std::numeric_limits<unsigned int>::max()));
}

unsigned int vision_log_interval_ticks() {
    return environment_uint_or("VISION_LOG_INTERVAL_TICKS", 1000u);
}

bool should_log_vision_tick(unsigned int tick_count) {
    const unsigned int interval = vision_log_interval_ticks();
    return tick_count == 1u || (interval > 0u && tick_count % interval == 0u);
}

void log_vision_result(
    const vision_native::VisionResult* result,
    bool aiming,
    unsigned int tick_count) {
    std::cout << "[Vision][CPP]"
              << " tick=" << tick_count
              << " aiming=" << (aiming ? 1 : 0);
    if (result == nullptr) {
        std::cout << " updated=0 frame=0 boxes=0 target=0 source=none stage=none"
                  << " conf=0 dx=0 dy=0 aim_auth=0 fire_auth=0"
                  << " service=none service_state=unknown service_seq=0"
                  << " mode=none"
                  << " cap=0ms copy=0ms pre=0ms infer=0ms enqueue=0ms decode=0ms"
                  << " selector=0ms age=0ms\n";
        return;
    }

    std::cout
        << " updated=" << (result->frame_updated ? 1 : 0)
        << " frame=" << result->frame_id
        << " boxes=" << result->boxes_seen
        << " target=" << (result->has_target ? 1 : 0)
        << " tier=" << safe_c_string(result->target_tier, "none")
        << " source=" << safe_c_string(result->target_source, "none")
        << " stage=" << safe_c_string(result->association_stage, "none")
        << " conf=" << result->target_confidence
        << " dx=" << result->dx
        << " dy=" << result->dy
        << " aim_auth=" << (result->aim_authority ? 1 : 0)
        << " fire_auth=" << (result->fire_authority ? 1 : 0)
        << " service=" << safe_c_string(result->service_freshness, "none")
        << " service_state=" << safe_c_string(result->service_source_state, "unknown")
        << " service_seq=" << result->service_sequence
        << " viewport=" << safe_c_string(result->viewport_level, "normal")
        << ':' << result->viewport_width << 'x' << result->viewport_height
        << '@' << result->viewport_left << ',' << result->viewport_top
        << " viewport_seq=" << result->viewport_sequence
        << " mode=" << vision_native::preprocess_mode_name(result->preprocess_mode)
        << " cap=" << result->capture_acquire_ms
        << "ms copy=" << capture_transfer_ms(*result)
        << "ms map=" << result->cuda_map_ms
        << "ms pre=" << result->preprocess_ms
        << "ms infer=" << result->infer_ms
        << "ms enqueue=" << result->enqueue_cpu_ms
        << "ms gpu=" << result->gpu_total_ms
        << "ms wait=" << result->output_wait_ms
        << "ms decode=" << result->decode_ms
        << "ms selector=" << result->selector_ms
        << "ms post=" << result->post_ms
        << "ms age=" << result->age_ms
        << "ms\n";
}

} // namespace

void RuntimeLoop::record_vision_diagnostics(bool aiming) {
    TelemetryVisionInput vision;
    vision.frame_id = latest_vision_result_.frame_id;
    vision.captured_at_ns = latest_vision_result_.captured_at_ns;
    vision.inferred_at_ns = latest_vision_result_.inferred_at_ns;
    vision.result_at_ns = latest_vision_result_.result_at_ns;
    vision.controller_consume_ns = latest_controller_consume_started_ns_;
    vision.frame_width = config_.vision.capture_width;
    vision.frame_height = config_.vision.capture_height;
    vision.has_target = latest_vision_result_.has_target;
    vision.live = latest_vision_result_.has_target && latest_vision_result_.has_body_box &&
        latest_vision_result_.frame_updated;
    vision.aiming = aiming;
    vision.x1 = latest_vision_result_.body_x1;
    vision.y1 = latest_vision_result_.body_y1;
    vision.x2 = latest_vision_result_.body_x2;
    vision.y2 = latest_vision_result_.body_y2;
    vision.target_x = latest_vision_result_.target_x;
    vision.target_y = latest_vision_result_.target_y;
    vision.screen_center_x = latest_vision_result_.screen_center_x;
    vision.screen_center_y = latest_vision_result_.screen_center_y;
    vision.detector_box_count = static_cast<std::uint32_t>(latest_vision_result_.boxes_seen);
    vision.target_source = latest_vision_result_.target_source;
    vision.target_tier = latest_vision_result_.target_tier;
    vision.target_confidence = latest_vision_result_.target_confidence;
    telemetry_collectors_.observe_new_vision(vision);
}

void RuntimeLoop::record_tick_diagnostics(
    const controller_native::PhysicalGamepadState& physical,
    const controller_native::GamepadOutputState& output,
    const controller_native::VirtualGamepadUpdateResult& output_result,
    const TickTimes& times, bool aiming, bool assist_aiming,
    const pipeline_contract::CommittedCaptureObservation* committed_capture) {
    const auto tick_started = times.tick_started;
    const auto controller_pipeline_started = times.pipeline_started;
    const auto vigem_update_started = times.output_started;
    const auto vigem_update_finished = times.output_finished;
    const auto physical_read_at_ns = times.physical_read_ns;
    const bool fresh_vision = committed_capture != nullptr;
    downward_diagnostics_.record_if_triggered(
        physical,
        output,
        controller_.last_pipeline_traces(),
        has_latest_vision_result_ ? &latest_vision_result_ : nullptr,
        aiming);
    if (perf_summary_logger_.enabled()) {
        const std::uint64_t output_sent_ns = steady_time_point_ns(vigem_update_finished);
        PerfControllerWindowSample controller_sample;
        controller_sample.timestamp_ns = output_sent_ns;
        controller_sample.aiming = aiming;
        controller_sample.output_delivered =
            !config_.output.enabled || output_result.delivered;
        controller_sample.tick_ms = std::chrono::duration<double, std::milli>(
            vigem_update_finished - tick_started).count();
        controller_sample.pipeline_ms = std::chrono::duration<double, std::milli>(
            vigem_update_started - controller_pipeline_started).count();
        controller_sample.vigem_ms = std::chrono::duration<double, std::milli>(
            vigem_update_finished - vigem_update_started).count();
        perf_summary_logger_.record_controller(controller_sample);

        if (fresh_vision && latest_vision_result_.frame_updated) {
            const auto& result = latest_vision_result_;
            const std::uint64_t capture_copy_complete_ns =
                result.capture_copy_complete_ns != 0
                ? result.capture_copy_complete_ns : result.captured_at_ns;
            PerfVisionWindowSample vision_sample;
            vision_sample.aiming = aiming;
            vision_sample.accumulated_frames = result.accumulated_frames;
            vision_sample.capture_to_result_ms = elapsed_ms_or_invalid(
                result.capture_acquire_begin_ns, result.result_at_ns);
            vision_sample.copy_to_result_ms = elapsed_ms_or_invalid(
                capture_copy_complete_ns, result.result_at_ns);
            vision_sample.source_present_to_result_ms =
                result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns, result.result_at_ns)
                : -1.0;
            vision_sample.result_to_controller_ms = elapsed_ms_or_invalid(
                result.result_at_ns, latest_controller_consume_started_ns_);
            vision_sample.result_to_vigem_ms = elapsed_ms_or_invalid(
                result.result_at_ns, output_sent_ns);
            vision_sample.vision_publish_to_vigem_ms =
                (latest_vision_publish_ns_ != 0)
                ? elapsed_ms_or_invalid(latest_vision_publish_ns_, output_sent_ns)
                : -1.0;
            vision_sample.controller_consume_to_vigem_ms = elapsed_ms_or_invalid(
                latest_controller_consume_started_ns_, output_sent_ns);
            const auto& control_trace = controller_.last_acquisition_trace();
            vision_sample.controller_submit_to_final_output_ms = elapsed_ms_or_invalid(
                latest_controller_submit_complete_ns_,
                control_trace.final_output_ready_ns);
            vision_sample.final_output_to_vigem_ms = elapsed_ms_or_invalid(
                control_trace.final_output_ready_ns, output_sent_ns);
            vision_sample.source_present_to_vigem_ms =
                result.source_present_steady_available
                ? elapsed_ms_or_invalid(result.source_present_steady_ns, output_sent_ns)
                : -1.0;
            vision_sample.cuda_map_ms = result.cuda_map_ms;
            vision_sample.preprocess_ms = result.preprocess_ms;
            vision_sample.infer_ms = result.infer_ms;
            vision_sample.gpu_total_ms = result.gpu_total_ms;
            vision_sample.output_copy_sync_ms = result.output_copy_sync_ms;
            vision_sample.output_copy_ms = result.output_copy_ms;
            vision_sample.output_wait_ms = result.output_wait_ms;
            vision_sample.sync_queue_residual_ms = std::max(
                0.0,
                static_cast<double>(result.output_wait_ms) -
                    static_cast<double>(result.gpu_total_ms));
            vision_sample.color_copy_ms = result.color_copy_required
                ? result.color_copy_ms : -1.0;
            vision_sample.cuda_unmap_ms = result.cuda_unmap_ms;
            perf_summary_logger_.record_vision(vision_sample);
        }
    }
    ++tick_count_;
    if (fresh_vision) {
        const auto& trace = controller_.last_acquisition_trace();
        TelemetryAcquisitionTraceInput acquisition_trace;
        acquisition_trace.source_frame_id = trace.source_frame_id;
        acquisition_trace.source_observation_id = trace.source_observation_id;
        acquisition_trace.persistent_target_id = trace.persistent_target_id;
        acquisition_trace.physical_ads_epoch = trace.physical_ads_epoch;
        acquisition_trace.target_acquisition_id = trace.target_acquisition_id;
        acquisition_trace.controller_tick_id = tick_count_;
        acquisition_trace.capture_acquire_begin_ns =
            latest_vision_result_.capture_acquire_begin_ns;
        acquisition_trace.capture_acquire_complete_ns =
            latest_vision_result_.capture_acquire_complete_ns;
        acquisition_trace.capture_copy_complete_ns =
            latest_vision_result_.capture_copy_complete_ns != 0
            ? latest_vision_result_.capture_copy_complete_ns
            : latest_vision_result_.captured_at_ns;
        acquisition_trace.accumulated_frames =
            latest_vision_result_.accumulated_frames;
        acquisition_trace.ads_acquisition_begin_ns =
            trace.ads_acquisition_begin_ns;
        acquisition_trace.ads_acquisition_complete_ns =
            trace.ads_acquisition_complete_ns;
        acquisition_trace.result_ready_ns = latest_vision_result_.result_at_ns;
        acquisition_trace.vision_publish_ns = latest_vision_publish_ns_;
        acquisition_trace.vision_publish_available =
            (latest_vision_publish_ns_ != 0);
        acquisition_trace.controller_submit_complete_ns =
            latest_controller_submit_complete_ns_;
        acquisition_trace.controller_consume_ns =
            latest_controller_consume_started_ns_;
        acquisition_trace.plan_decision_ns = trace.plan_decision_ns;
        acquisition_trace.final_output_ready_ns = trace.final_output_ready_ns;
        acquisition_trace.vigem_submit_complete_ns =
            steady_time_point_ns(vigem_update_finished);
        acquisition_trace.source_present_qpc =
            latest_vision_result_.source_present_qpc;
        acquisition_trace.source_present_qpc_frequency =
            latest_vision_result_.source_present_qpc_frequency;
        acquisition_trace.source_present_available =
            latest_vision_result_.source_present_available;
        acquisition_trace.source_present_steady_ns =
            latest_vision_result_.source_present_steady_ns;
        acquisition_trace.source_present_calibration_id =
            latest_vision_result_.source_present_calibration_id;
        acquisition_trace.source_present_calibration_uncertainty_ns =
            latest_vision_result_.source_present_calibration_uncertainty_ns;
        acquisition_trace.source_present_steady_available =
            latest_vision_result_.source_present_steady_available;
        acquisition_trace.plan_admitted = trace.plan_admitted;
        acquisition_trace.acquisition_active = trace.acquisition_active;
        acquisition_trace.acquisition_exists = trace.acquisition_exists;
        acquisition_trace.preferred_source_id = trace.preferred_source_id;
        acquisition_trace.selected_source_id = trace.selected_source_id;
        acquisition_trace.candidate_count = trace.candidate_count;
        acquisition_trace.acquisition_state =
            static_cast<std::uint8_t>(trace.acquisition_state);
        acquisition_trace.decision_reason =
            static_cast<std::uint8_t>(trace.decision_reason);
        acquisition_trace.source_decision_available =
            trace.source_decision_available;
        acquisition_trace.source_decision_outcome =
            static_cast<std::uint8_t>(trace.source_decision_outcome);
        acquisition_trace.source_decision_reason =
            static_cast<std::uint8_t>(trace.source_decision_reason);
        acquisition_trace.acquisition_terminal_reason =
            static_cast<std::uint8_t>(trace.acquisition_terminal_reason);
        acquisition_trace.selector_target_generation =
            trace.selector_target_generation;
        acquisition_trace.selector_target_changed =
            trace.selector_target_changed;
        acquisition_trace.effective_activation_radius_px =
            trace.effective_activation_radius_px;
        acquisition_trace.raw_error_x = trace.raw_error_px.x;
        acquisition_trace.raw_error_y = trace.raw_error_px.y;
        acquisition_trace.target_size_x = trace.target_size_px.x;
        acquisition_trace.target_size_y = trace.target_size_px.y;
        acquisition_trace.requested_ai_x = trace.requested_ai.x;
        acquisition_trace.requested_ai_y = trace.requested_ai.y;
        acquisition_trace.shaped_ai_x = trace.shaped_ai.x;
        acquisition_trace.shaped_ai_y = trace.shaped_ai.y;
        acquisition_trace.fused_output_x = trace.fused_output.x;
        acquisition_trace.fused_output_y = trace.fused_output.y;
        acquisition_trace.post_output_x = trace.post_output.x;
        acquisition_trace.post_output_y = trace.post_output.y;
        acquisition_trace.has_first_requested_ai =
            trace.has_first_requested_ai;
        acquisition_trace.has_first_shaped_ai = trace.has_first_shaped_ai;
        acquisition_trace.has_first_fused_output =
            trace.has_first_fused_output;
        acquisition_trace.first_requested_ai_ns =
            trace.first_requested_ai_ns;
        acquisition_trace.first_shaped_ai_ns = trace.first_shaped_ai_ns;
        acquisition_trace.first_fused_output_ns =
            trace.first_fused_output_ns;
        acquisition_trace.first_requested_ai_x =
            trace.first_requested_ai.x;
        acquisition_trace.first_requested_ai_y =
            trace.first_requested_ai.y;
        acquisition_trace.first_shaped_ai_x = trace.first_shaped_ai.x;
        acquisition_trace.first_shaped_ai_y = trace.first_shaped_ai.y;
        acquisition_trace.first_fused_output_x = trace.first_fused_output.x;
        acquisition_trace.first_fused_output_y = trace.first_fused_output.y;
        telemetry_collectors_.observe_acquisition_trace(acquisition_trace);
    }
    const auto& telemetry_components = controller_.last_output_components();
    TelemetryTickInput telemetry_tick;
    telemetry_tick.tick_id = tick_count_;
    telemetry_tick.physical_read_ns = physical_read_at_ns;
    telemetry_tick.controller_consume_ns = latest_controller_consume_started_ns_;
    telemetry_tick.output_sent_ns = steady_time_point_ns(vigem_update_finished);
    telemetry_tick.sample_ns = steady_time_point_ns(vigem_update_finished);
    telemetry_tick.aiming = aiming;
    const auto& telemetry_vision_state = controller_.last_frame_vision_state();
    telemetry_tick.controller.physical_connected = physical.connected;
    telemetry_tick.controller.current_observed_target_present =
        telemetry_vision_state.current_observed_target_present;
    telemetry_tick.controller.output_delivered = !config_.output.enabled || output_result.delivered;
    telemetry_tick.output_disabled = !config_.output.enabled;
    telemetry_tick.controller.output_backend_connected =
        !config_.output.enabled || output_result.backend_connected;
    telemetry_tick.controller.output_error_code = output_result.error_code;
    telemetry_tick.controller.input_reconnect_count = sdl_reconnect_count_;
    telemetry_tick.controller.output_reconnect_count = output_result.reconnect_count;
    telemetry_tick.controller.aim_authority = telemetry_vision_state.aim_authority;
    telemetry_tick.controller.fire_authority = telemetry_vision_state.fire_authority;
    telemetry_tick.aim_mode = controller_.last_ai_aim_mode().c_str();
    telemetry_tick.controller.left_trigger = physical.left_trigger;
    telemetry_tick.controller.right_trigger = physical.right_trigger;
    telemetry_tick.controller.physical_x = physical.right_x;
    telemetry_tick.controller.physical_y = physical.right_y;
    telemetry_tick.controller.manual_x = telemetry_components.manual_stick.x;
    telemetry_tick.controller.manual_y = telemetry_components.manual_stick.y;
    telemetry_tick.controller.filtered_manual_x =
        telemetry_components.filtered_manual_stick.x;
    telemetry_tick.controller.filtered_manual_y =
        telemetry_components.filtered_manual_stick.y;
    telemetry_tick.controller.manual_confidence = telemetry_components.manual_confidence;
    telemetry_tick.controller.ai_x = telemetry_components.ai_aim_stick.x;
    telemetry_tick.controller.ai_y = telemetry_components.ai_aim_stick.y;
    telemetry_tick.controller.target_final_x = telemetry_components.target_final_stick.x;
    telemetry_tick.controller.target_final_y = telemetry_components.target_final_stick.y;
    telemetry_tick.controller.ai_correction_x = telemetry_components.ai_correction_stick.x;
    telemetry_tick.controller.ai_correction_y = telemetry_components.ai_correction_stick.y;
    telemetry_tick.manual_authority_mode =
        telemetry_components.manual_authority_mode;
    telemetry_tick.assist_control_phase =
        telemetry_components.assist_control_phase.c_str();
    telemetry_tick.operation_class =
        telemetry_components.operation_class.c_str();
    telemetry_tick.controller.operation_confidence =
        telemetry_components.operation_confidence;
    telemetry_tick.controller.direction_trust = telemetry_components.direction_trust;
    telemetry_tick.controller.recoil_pull_strength =
        telemetry_components.recoil_pull_strength;
    telemetry_tick.controller.manual_passthrough_x =
        telemetry_components.manual_passthrough_x;
    telemetry_tick.controller.manual_passthrough_y =
        telemetry_components.manual_passthrough_y;
    telemetry_tick.controller.manual_correction_x =
        telemetry_components.manual_correction_x;
    telemetry_tick.controller.manual_correction_y =
        telemetry_components.manual_correction_y;
    telemetry_tick.controller.manual_boundary_x = telemetry_components.manual_boundary_x;
    telemetry_tick.controller.manual_boundary_y = telemetry_components.manual_boundary_y;
    telemetry_tick.controller.manual_exit_requested =
        telemetry_components.manual_exit_requested;
    telemetry_tick.controller.handover_requested =
        telemetry_components.handover_requested;
    telemetry_tick.controller.handover_braking = telemetry_components.handover_braking;
    telemetry_tick.controller.bodylock_error_rate_x =
        telemetry_components.bodylock_error_rate_px_per_sec.x;
    telemetry_tick.controller.bodylock_error_rate_y =
        telemetry_components.bodylock_error_rate_px_per_sec.y;
    telemetry_tick.controller.bodylock_position_stick_x =
        telemetry_components.bodylock_position_stick.x;
    telemetry_tick.controller.bodylock_position_stick_y =
        telemetry_components.bodylock_position_stick.y;
    telemetry_tick.controller.bodylock_motion_stick_x =
        telemetry_components.bodylock_motion_stick.x;
    telemetry_tick.controller.bodylock_motion_stick_y =
        telemetry_components.bodylock_motion_stick.y;
    telemetry_tick.controller.bodylock_effective_motion_stick_x =
        telemetry_components.bodylock_effective_motion_stick.x;
    telemetry_tick.controller.bodylock_effective_motion_stick_y =
        telemetry_components.bodylock_effective_motion_stick.y;
    telemetry_tick.controller.bodylock_radial_motion_bound =
        telemetry_components.bodylock_radial_motion_bound;
    telemetry_tick.bodylock_constraint_reason =
        telemetry_components.bodylock_constraint_reason.c_str();
    telemetry_tick.controller.requested_assist_x = telemetry_components.requested_assist_stick.x;
    telemetry_tick.controller.requested_assist_y = telemetry_components.requested_assist_stick.y;
    telemetry_tick.controller.shaped_assist_x = telemetry_components.shaped_assist_stick.x;
    telemetry_tick.controller.shaped_assist_y = telemetry_components.shaped_assist_stick.y;
    telemetry_tick.controller.auto_fire_requested = telemetry_components.auto_fire_requested;
    telemetry_tick.controller.auto_fire_aim_ready = telemetry_components.auto_fire_aim_ready;
    telemetry_tick.controller.auto_fire_allowed = telemetry_components.auto_fire_allowed;
    telemetry_tick.controller.auto_fire_active = telemetry_components.auto_fire_active;
    telemetry_tick.controller.auto_fire_pulse_starts =
        telemetry_components.auto_fire_pulse_starts;
    telemetry_tick.controller.auto_fire_pulse_pressed =
        telemetry_components.auto_fire_pulse_pressed;
    telemetry_tick.controller.auto_fire_cadence_wait =
        telemetry_components.auto_fire_cadence_wait;
    telemetry_tick.controller.final_fire_button = telemetry_components.fire_button;
    telemetry_tick.auto_fire_block_reason =
        telemetry_components.auto_fire_block_reason.c_str();
    const auto enemy_mark_status =
        person_detection_gesture_.status(tick_started);
    telemetry_tick.controller.enemy_mark_request_pending =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.request_pending;
    telemetry_tick.controller.enemy_mark_synthetic_pressed =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.synthetic_pressed;
    telemetry_tick.controller.enemy_mark_fired =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.fired_this_tick;
    telemetry_tick.controller.enemy_mark_canceled =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.canceled_this_tick;
    telemetry_tick.controller.enemy_mark_confirmation_frames =
        enemy_mark_status.confirmation_frames;
    telemetry_tick.controller.enemy_mark_target_scope =
        enemy_mark_status.evaluated_scope;
    telemetry_tick.controller.enemy_mark_target_generation =
        enemy_mark_status.evaluated_generation;
    telemetry_tick.controller.enemy_mark_last_scope =
        enemy_mark_status.last_marked_scope;
    telemetry_tick.controller.enemy_mark_last_generation =
        enemy_mark_status.last_marked_generation;
    telemetry_tick.enemy_mark_block_reason =
        config_.gamepad.enemy_mark.enabled
        ? person_mark_block_reason_name(enemy_mark_status.block_reason)
        : "disabled";
    telemetry_tick.controller.pre_recoil_x = telemetry_components.before_recoil_stick.x;
    telemetry_tick.controller.pre_recoil_y = telemetry_components.before_recoil_stick.y;
    telemetry_tick.controller.recoil_x = telemetry_components.recoil_stick.x;
    telemetry_tick.controller.recoil_y = telemetry_components.recoil_stick.y;
    telemetry_tick.controller.final_x = telemetry_components.final_stick.x;
    telemetry_tick.controller.final_y = telemetry_components.final_stick.y;
    telemetry_tick.controller.observed_error_x = telemetry_components.observed_error_px.x;
    telemetry_tick.controller.observed_error_y = telemetry_components.observed_error_px.y;
    telemetry_tick.controller.control_error_x = telemetry_components.control_error_px.x;
    telemetry_tick.controller.control_error_y = telemetry_components.control_error_px.y;
    telemetry_tick.controller.source_aim_x = telemetry_components.source_aim_px.x;
    telemetry_tick.controller.source_aim_y = telemetry_components.source_aim_px.y;
    telemetry_tick.controller.desired_aim_x = telemetry_components.desired_aim_px.x;
    telemetry_tick.controller.desired_aim_y = telemetry_components.desired_aim_px.y;
    telemetry_tick.controller.desired_point_u =
        telemetry_components.desired_point_normalized.x;
    telemetry_tick.controller.desired_point_v =
        telemetry_components.desired_point_normalized.y;
    telemetry_tick.controller.aim_region_x1 = telemetry_components.aim_region_px.x;
    telemetry_tick.controller.aim_region_y1 = telemetry_components.aim_region_px.y;
    telemetry_tick.controller.aim_region_x2 = telemetry_components.aim_region_px.x +
        telemetry_components.aim_region_px.w;
    telemetry_tick.controller.aim_region_y2 = telemetry_components.aim_region_px.y +
        telemetry_components.aim_region_px.h;
    telemetry_tick.controller.has_aim_region = telemetry_components.has_aim_region;
    telemetry_tick.controller.visual_authority = telemetry_components.visual_authority;
    telemetry_tick.controller.enemy_cue_current = telemetry_components.enemy_cue_current;
    telemetry_tick.controller.enemy_identity_confirmed =
        telemetry_components.enemy_identity_confirmed;
    telemetry_tick.controller.enemy_cue_checked = telemetry_components.enemy_cue_checked;
    telemetry_tick.aim_region_source =
        telemetry_components.aim_region_source.c_str();
    telemetry_tick.desired_point_source =
        telemetry_components.desired_point_source.c_str();
    telemetry_tick.final_left_x = output.left_x;
    telemetry_tick.final_left_y = output.left_y;
    telemetry_tick.output_saturated =
        std::fabs(output.right_x) >= 0.999f ||
        std::fabs(output.right_y) >= 0.999f;
    telemetry_tick.controller.selected_track_id = telemetry_vision_state.selected_track_id;
    telemetry_tick.controller.selected_observation_id =
        telemetry_vision_state.selected_observation_id;
    telemetry_tick.assist_authority = telemetry_components.assist_authority.c_str();
    telemetry_tick.assist_authority_reason =
        telemetry_components.assist_authority_reason.c_str();
    telemetry_tick.bodylock_lifecycle =
        telemetry_components.bodylock_lifecycle.c_str();
    telemetry_tick.assist_limit_reason =
        telemetry_components.assist_limit_reason.c_str();
    telemetry_collectors_.observe_tick(telemetry_tick);
    if (committed_capture != nullptr) {
        telemetry_collectors_.observe_committed_capture(*committed_capture);
    }
    const bool log_vision = perf_log_ && should_log_vision_tick(tick_count_);
    const bool log_gamepad_perf = gamepad_perf_log_ && should_log_vision_tick(tick_count_);
    if (log_vision || log_gamepad_perf) {
        const auto elapsed = std::chrono::steady_clock::now() - tick_started;
        const auto controller_pipeline_elapsed = vigem_update_started - controller_pipeline_started;
        const auto vigem_update_elapsed = vigem_update_finished - vigem_update_started;
        const double ctrl_loop_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        const std::uint64_t output_sent_at_ns = steady_time_point_ns(vigem_update_finished);
        const controller_native::NativeAutoFireCounters fire = controller_.auto_fire_counters();
        const vision_native::VisionResult* result =
            has_latest_vision_result_ ? &latest_vision_result_ : nullptr;
        const std::uint64_t result_timestamp_ns = result != nullptr
            ? (result->captured_at_ns != 0 ? result->captured_at_ns : result->result_at_ns) : 0;
        PerfSnapshot snapshot;
        snapshot.loop_fps = loop_fps_from_elapsed_ms(ctrl_loop_ms);
        snapshot.native_ms = result != nullptr ? result->post_ms : 0.0;
        snapshot.consume_ms = result != nullptr
            ? elapsed_ms_between_ns(result_timestamp_ns, latest_controller_consume_started_ns_)
            : 0.0;
        snapshot.out_age_ms = result != nullptr
            ? elapsed_ms_between_ns(result_timestamp_ns, output_sent_at_ns)
            : 0.0;
        snapshot.gpu_total_ms = result != nullptr ? result->gpu_total_ms : 0.0;
        snapshot.sync_wait_ms = result != nullptr ? result->output_wait_ms : 0.0;
        snapshot.ctrl_loop_ms = ctrl_loop_ms;
        snapshot.ctrl_pipeline_ms =
            std::chrono::duration<double, std::milli>(controller_pipeline_elapsed).count();
        snapshot.vigem_update_ms =
            std::chrono::duration<double, std::milli>(vigem_update_elapsed).count();
        snapshot.target_tier =
            result != nullptr && result->target_tier != nullptr ? result->target_tier : "none";
        snapshot.fire_requested = fire.requested;
        snapshot.fire_allowed = fire.allowed;
        snapshot.fire_blocked = fire.blocked;
        snapshot.box_samples = result != nullptr ? result->boxes_seen : 0.0;
        if (log_gamepad_perf) {
            perf_logger_.record_sample(snapshot);
        }
        if (log_vision) {
            log_vision_result(
                result,
                assist_aiming,
                tick_count_);
        }
    }
}

} // namespace runtime_app
