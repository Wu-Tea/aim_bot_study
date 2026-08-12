#pragma once

#include "pipeline_contract/committed_capture_observation.h"
#include "runtime_telemetry.h"

#include <cstdint>
#include <memory>

namespace runtime_app {

struct TelemetryTickInput {
    std::uint64_t tick_id = 0;
    std::uint64_t physical_read_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    std::uint64_t output_sent_ns = 0;
    std::uint64_t sample_ns = 0;
    bool aiming = false;
    bool physical_connected = false;
    bool current_observed_target_present = false;
    bool output_delivered = false;
    bool output_disabled = false;
    bool output_backend_connected = false;
    std::uint32_t output_error_code = 0;
    unsigned int input_reconnect_count = 0;
    unsigned int output_reconnect_count = 0;
    bool aim_authority = false;
    bool fire_authority = false;
    const char* aim_mode = "none";
    float left_trigger = 0.0f, right_trigger = 0.0f;
    float physical_x = 0.0f, physical_y = 0.0f;
    float physical_left_x = 0.0f, physical_left_y = 0.0f;
    float manual_x = 0.0f, manual_y = 0.0f;
    float filtered_manual_x = 0.0f, filtered_manual_y = 0.0f;
    float manual_confidence = 0.0f;
    float ai_x = 0.0f, ai_y = 0.0f;
    float target_final_x = 0.0f, target_final_y = 0.0f;
    float ai_correction_x = 0.0f, ai_correction_y = 0.0f;
    const char* manual_authority_mode = "no_target_passthrough";
    const char* assist_control_phase = "manual";
    bool manual_passthrough_x = true;
    bool manual_passthrough_y = true;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_boundary_x = false;
    bool manual_boundary_y = false;
    bool manual_exit_requested = false;
    bool handover_requested = false;
    bool handover_braking = false;
    float bodylock_error_rate_x = 0.0f, bodylock_error_rate_y = 0.0f;
    float bodylock_position_stick_x = 0.0f, bodylock_position_stick_y = 0.0f;
    float bodylock_motion_stick_x = 0.0f, bodylock_motion_stick_y = 0.0f;
    float bodylock_effective_motion_stick_x = 0.0f;
    float bodylock_effective_motion_stick_y = 0.0f;
    bool bodylock_radial_motion_bound = false;
    const char* bodylock_constraint_reason = "none";
    float requested_assist_x = 0.0f, requested_assist_y = 0.0f;
    float shaped_assist_x = 0.0f, shaped_assist_y = 0.0f;
    bool auto_fire_requested = false;
    bool auto_fire_aim_ready = false;
    bool auto_fire_allowed = false;
    bool auto_fire_active = false;
    std::uint64_t auto_fire_pulse_starts = 0;
    bool auto_fire_pulse_pressed = false;
    bool auto_fire_cadence_wait = false;
    bool final_fire_button = false;
    const char* auto_fire_block_reason = "none";
    bool enemy_mark_request_pending = false;
    bool enemy_mark_synthetic_pressed = false;
    bool enemy_mark_fired = false;
    bool enemy_mark_canceled = false;
    std::uint32_t enemy_mark_confirmation_frames = 0;
    std::uint64_t enemy_mark_target_scope = 0;
    std::uint64_t enemy_mark_target_generation = 0;
    std::uint64_t enemy_mark_last_scope = 0;
    std::uint64_t enemy_mark_last_generation = 0;
    const char* enemy_mark_block_reason = "disabled";
    float pre_recoil_x = 0.0f, pre_recoil_y = 0.0f;
    float recoil_x = 0.0f, recoil_y = 0.0f;
    float final_x = 0.0f, final_y = 0.0f;
    float observed_error_x = 0.0f, observed_error_y = 0.0f;
    float control_error_x = 0.0f, control_error_y = 0.0f;
    float source_aim_x = 0.0f, source_aim_y = 0.0f;
    float desired_aim_x = 0.0f, desired_aim_y = 0.0f;
    float desired_point_u = 0.0f, desired_point_v = 0.0f;
    float aim_region_x1 = 0.0f, aim_region_y1 = 0.0f;
    float aim_region_x2 = 0.0f, aim_region_y2 = 0.0f;
    bool has_aim_region = false;
    float visual_authority = 0.0f;
    bool enemy_cue_current = false;
    bool enemy_identity_confirmed = false;
    bool enemy_cue_checked = false;
    const char* aim_region_source = "none";
    const char* desired_point_source = "none";
    float final_left_x = 0.0f, final_left_y = 0.0f;
    bool output_saturated = false;
    std::uint64_t selected_track_id = 0;
    std::uint64_t selected_observation_id = 0;
    const char* assist_authority = "reject";
    const char* assist_authority_reason = "none";
    const char* bodylock_lifecycle = "inactive";
    const char* assist_limit_reason = "none";
};

struct TelemetryVisionInput {
    std::uint64_t frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t inferred_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    int frame_width = 0, frame_height = 0;
    bool has_target = false;
    bool live = false;
    bool aiming = false;
    bool explicit_switch = false;
    bool association_ambiguous = false;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    float target_x = 0.0f, target_y = 0.0f;
    float screen_center_x = 0.0f, screen_center_y = 0.0f;
    float motion_residual_px = 0.0f;
    std::uint32_t detector_box_count = 0;
    const char* target_source = "unknown";
    const char* target_tier = "none";
    float target_confidence = 0.0f;
};

struct TelemetryAcquisitionTraceInput {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t target_acquisition_id = 0;
    std::uint64_t controller_tick_id = 0;
    std::uint64_t capture_acquire_begin_ns = 0;
    std::uint64_t capture_acquire_complete_ns = 0;
    std::uint64_t capture_copy_complete_ns = 0;
    std::uint32_t accumulated_frames = 0;
    std::uint64_t ads_acquisition_begin_ns = 0;
    std::uint64_t ads_acquisition_complete_ns = 0;
    std::uint64_t result_ready_ns = 0;
    std::uint64_t vision_publish_ns = 0;
    std::uint64_t controller_submit_complete_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    std::uint64_t plan_decision_ns = 0;
    std::uint64_t final_output_ready_ns = 0;
    std::uint64_t first_requested_ai_ns = 0;
    std::uint64_t first_shaped_ai_ns = 0;
    std::uint64_t first_fused_output_ns = 0;
    std::uint64_t vigem_submit_complete_ns = 0;
    std::uint64_t first_effect_observed_ns = 0;
    std::uint64_t preferred_source_id = 0;
    std::uint64_t selected_source_id = 0;
    std::uint32_t candidate_count = 0;
    std::uint8_t acquisition_state = 0;
    std::uint8_t decision_reason = 0;
    bool source_decision_available = false;
    std::uint8_t source_decision_outcome = 0;
    std::uint8_t source_decision_reason = 0;
    std::uint8_t acquisition_terminal_reason = 0;
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    std::uint64_t source_present_qpc = 0;
    std::uint64_t source_present_qpc_frequency = 0;
    bool source_present_available = false;
    std::uint64_t source_present_steady_ns = 0;
    std::uint64_t source_present_calibration_id = 0;
    std::uint64_t source_present_calibration_uncertainty_ns = 0;
    bool source_present_steady_available = false;
    bool plan_admitted = false;
    bool acquisition_active = false;
    bool acquisition_exists = false;
    bool vision_publish_available = false;
    bool has_first_requested_ai = false;
    bool has_first_shaped_ai = false;
    bool has_first_fused_output = false;
    float effective_activation_radius_px = 0.0f;
    float raw_error_x = 0.0f, raw_error_y = 0.0f;
    float target_size_x = 0.0f, target_size_y = 0.0f;
    float requested_ai_x = 0.0f, requested_ai_y = 0.0f;
    float shaped_ai_x = 0.0f, shaped_ai_y = 0.0f;
    float fused_output_x = 0.0f, fused_output_y = 0.0f;
    float post_output_x = 0.0f, post_output_y = 0.0f;
    float first_requested_ai_x = 0.0f, first_requested_ai_y = 0.0f;
    float first_shaped_ai_x = 0.0f, first_shaped_ai_y = 0.0f;
    float first_fused_output_x = 0.0f, first_fused_output_y = 0.0f;
};

struct TelemetryCollectorsCounters {
    std::uint64_t state_transitions = 0;
    std::uint64_t constructed_records = 0;
    std::uint64_t controller_sample_records = 0;
    std::uint64_t input_event_records = 0;
    std::uint64_t ads_transition_records = 0;
    std::uint64_t target_event_records = 0;
    std::uint64_t committed_capture_records = 0;
    std::uint64_t acquisition_traces = 0;
    std::uint64_t delivered_control_records = 0;
};

struct TelemetrySessionContext {
    const char* build_commit = "unknown";
    const char* config_hash = "unknown";
    const char* engine_hash = "unknown";
    const char* executable_sha256 = "unknown";
    int capture_width = 0;
    int capture_height = 0;
    int active_capture_fps = 0;
    int idle_capture_fps = 0;
    int controller_tick_hz = 0;
    int telemetry_hz = 0;
};

class TelemetryCollectors {
public:
    TelemetryCollectors(
        bool enabled,
        RuntimeTelemetry* sink,
        TelemetrySessionContext context = TelemetrySessionContext{});
    ~TelemetryCollectors();
    TelemetryCollectors(const TelemetryCollectors&) = delete;
    TelemetryCollectors& operator=(const TelemetryCollectors&) = delete;

    bool enabled() const noexcept;
    void observe_tick(const TelemetryTickInput& input) noexcept;
    void observe_new_vision(const TelemetryVisionInput& input) noexcept;
    void observe_acquisition_trace(
        const TelemetryAcquisitionTraceInput& input) noexcept;
    void observe_committed_capture(
        const pipeline_contract::CommittedCaptureObservation& observation) noexcept;
    void shutdown(std::uint64_t now_ns) noexcept;
    TelemetryCollectorsCounters counters() const noexcept;

private:
    struct State;
    void enqueue(TelemetryRecord record) noexcept;
    void flush_ads_event() noexcept;

    RuntimeTelemetry* sink_ = nullptr;
    std::unique_ptr<State> state_;
    TelemetryCollectorsCounters counters_;
};

} // namespace runtime_app
