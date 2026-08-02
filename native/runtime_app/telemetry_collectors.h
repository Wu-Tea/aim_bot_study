#pragma once

#include "pipeline_contract/committed_capture_observation.h"
#include "control_learning/causal_online_response_learner.h"
#include "control_learning/pending_motion_model.h"
#include "control_learning/short_horizon_rollout.h"
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
    // Validated proposals only; these are not final-output shares.
    float fresh_vision_validated_manual_proposal_x = 0.0f;
    float fresh_vision_validated_manual_proposal_y = 0.0f;
    float fresh_vision_validated_ai_proposal_x = 0.0f;
    float fresh_vision_validated_ai_proposal_y = 0.0f;
    float fresh_vision_manual_radial_scale = 1.0f;
    bool fresh_vision_wrong_way_policy_applied = false;
    bool fresh_vision_ai_radial_bound_applied = false;
    float fresh_vision_ai_radial_scale = 1.0f;
    bool fresh_vision_predictive_envelope_applied = false;
    bool fresh_vision_escape_latched = false;
    float fresh_vision_authoritative_error_x = 0.0f;
    float fresh_vision_authoritative_error_y = 0.0f;
    float fresh_vision_predicted_error_x = 0.0f;
    float fresh_vision_predicted_error_y = 0.0f;
    float fresh_vision_raw_manual_radial = 0.0f;
    float fresh_vision_raw_ai_radial = 0.0f;
    float fresh_vision_strongest_valid_radial = 0.0f;
    float fresh_vision_stopping_radial = 0.0f;
    float fresh_vision_permitted_radial = 0.0f;
    float fresh_vision_pre_slew_radial = 0.0f;
    float fresh_vision_final_radial = 0.0f;
    float fresh_vision_horizon_seconds = 0.0f;
    float fresh_vision_horizon_y_seconds = 0.0f;
    float fresh_vision_max_force_x = 0.0f;
    float fresh_vision_max_force_y = 0.0f;
    float fresh_vision_envelope_target_x = 0.0f;
    float fresh_vision_envelope_target_y = 0.0f;
    const char* fresh_vision_envelope_reason = "none";
    const char* fresh_vision_envelope_source = "unavailable";
    float bodylock_error_rate_x = 0.0f, bodylock_error_rate_y = 0.0f;
    float bodylock_position_stick_x = 0.0f, bodylock_position_stick_y = 0.0f;
    float bodylock_motion_stick_x = 0.0f, bodylock_motion_stick_y = 0.0f;
    float bodylock_effective_motion_stick_x = 0.0f;
    float bodylock_effective_motion_stick_y = 0.0f;
    bool bodylock_radial_motion_bound = false;
    const char* bodylock_constraint_reason = "none";
    float requested_assist_x = 0.0f, requested_assist_y = 0.0f;
    float shaped_assist_x = 0.0f, shaped_assist_y = 0.0f;
    float post_ai_x = 0.0f, post_ai_y = 0.0f;
    float dynamic_adjustment_x = 0.0f, dynamic_adjustment_y = 0.0f;
    float post_dynamic_x = 0.0f, post_dynamic_y = 0.0f;
    float ads_brake_x = 0.0f, ads_brake_y = 0.0f;
    float post_ads_brake_x = 0.0f, post_ads_brake_y = 0.0f;
    float ads_carry_brake_x = 0.0f, ads_carry_brake_y = 0.0f;
    float post_ads_carry_brake_x = 0.0f, post_ads_carry_brake_y = 0.0f;
    bool ads_brake_active = false;
    bool ads_carry_brake_active = false;
    bool ads_completion_active = false;
    int ads_completion_stable_frames = 0;
    float ads_completion_radius_px = 0.0f;
    int ads_completion_required_frames = 0;
    float ads_completion_max_ms = 0.0f;
    const char* ads_completion_reason = "none";
    bool manual_takeover_active = false;
    bool auto_fire_requested = false;
    bool auto_fire_aim_ready = false;
    bool auto_fire_allowed = false;
    bool auto_fire_active = false;
    std::uint64_t auto_fire_pulse_starts = 0;
    bool auto_fire_pulse_pressed = false;
    bool auto_fire_cadence_wait = false;
    bool final_fire_button = false;
    const char* auto_fire_block_reason = "none";
    float pre_recoil_x = 0.0f, pre_recoil_y = 0.0f;
    float recoil_x = 0.0f, recoil_y = 0.0f;
    float final_x = 0.0f, final_y = 0.0f;
    float remaining_work_x = 0.0f, remaining_work_y = 0.0f;
    float delivered_camera_work_x = 0.0f, delivered_camera_work_y = 0.0f;
    float remaining_work_confidence = 0.0f;
    bool remaining_work_valid = false;
    float final_left_x = 0.0f, final_left_y = 0.0f;
    bool output_saturated = false;
    std::uint64_t selected_track_id = 0;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t backing_frame_id = 0;
    float track_observation_age_ms = 0.0f;
    float track_position_sigma = 0.0f;
    float track_ambiguity = 0.0f;
    const char* assist_authority = "reject";
    const char* assist_authority_reason = "none";
    const char* bodylock_lifecycle = "inactive";
    const char* bodylock_transition_reason = "none";
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
    bool projected = false;
    bool aiming = false;
    bool explicit_switch = false;
    bool association_ambiguous = false;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    float target_x = 0.0f, target_y = 0.0f;
    float screen_center_x = 0.0f, screen_center_y = 0.0f;
    float predicted_motion_x = 0.0f, predicted_motion_y = 0.0f;
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
    bool ego_motion_available = false;
    bool ego_motion_valid = false;
    std::uint64_t ego_result_sequence = 0;
    std::uint64_t ego_previous_frame_id = 0;
    std::uint64_t ego_current_frame_id = 0;
    std::uint64_t ego_previous_result_ns = 0;
    std::uint64_t ego_current_result_ns = 0;
    float ego_background_dx = 0.0f;
    float ego_background_dy = 0.0f;
    float ego_camera_dx = 0.0f;
    float ego_camera_dy = 0.0f;
    float ego_confidence = 0.0f;
    float ego_valid_background_ratio = 0.0f;
    float ego_residual_px = 0.0f;
    float ego_compute_ms = 0.0f;
    std::uint32_t ego_inlier_count = 0;
    std::uint32_t ego_sample_count = 0;
    std::uint8_t ego_invalid_reason = 0;
};

struct TelemetryEgoMotionShadowInput {
    bool available = false;
    bool valid = false;
    std::uint8_t invalid_reason = 0;
    std::uint64_t result_sequence = 0;
    std::uint64_t previous_frame_id = 0;
    std::uint64_t current_frame_id = 0;
    std::uint64_t previous_present_qpc = 0;
    std::uint64_t current_present_qpc = 0;
    std::uint64_t present_qpc_frequency = 0;
    std::uint64_t previous_result_ns = 0;
    std::uint64_t current_result_ns = 0;
    float background_dx = 0.0f;
    float background_dy = 0.0f;
    float camera_dx = 0.0f;
    float camera_dy = 0.0f;
    float confidence = 0.0f;
    float valid_background_ratio = 0.0f;
    float residual_px = 0.0f;
    float compute_ms = 0.0f;
    std::uint32_t inlier_count = 0;
    std::uint32_t sample_count = 0;
};

struct TelemetryCollectorsCounters {
    std::uint64_t state_transitions = 0;
    std::uint64_t constructed_records = 0;
    std::uint64_t acquisition_traces = 0;
    std::uint64_t delivered_control_records = 0;
    std::uint64_t ego_motion_records = 0;
};

struct TelemetrySessionContext {
    const char* build_commit = "unknown";
    const char* config_hash = "unknown";
    const char* engine_hash = "unknown";
    const char* executable_sha256 = "unknown";
    const char* tracker_backend = "unknown";
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
    void observe_ego_motion_shadow(
        std::uint64_t source_frame_id,
        std::uint64_t controller_tick_id,
        const TelemetryEgoMotionShadowInput& input) noexcept;
    void observe_committed_capture(
        const pipeline_contract::CommittedCaptureObservation& observation) noexcept;
    const control_learning::ControlHistory<1024>* control_history() const noexcept;
    void observe_causal_shadow(
        const pipeline_contract::CommittedCaptureObservation& observation,
        const control_learning::SampleAssessment& assessment,
        const control_learning::CausalResponseEstimate& estimate,
        const control_learning::PendingMotionEstimate& pending,
        const control_learning::RolloutResult& rollout,
        const control_learning::Vec2d& final_output) noexcept;
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
