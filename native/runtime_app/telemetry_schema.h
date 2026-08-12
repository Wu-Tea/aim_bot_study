#pragma once

#include <cstdint>
#include <array>
#include <type_traits>

namespace runtime_app {

inline constexpr std::uint16_t kTelemetrySchemaVersion = 17;

enum class TelemetryRecordType : std::uint8_t {
    SessionMetadata,
    ControllerSample,
    InputEvent,
    TargetEvent,
    AdsTransitionSample,
    AdsTransition,
    CommittedCaptureObservation,
    DeliveredControlSample,
    AdsAcquisitionTrace,
};

enum class VisionSampleQuality : std::uint8_t {
    Normal,
    SoftWeight,
    HardReject,
};

enum class TelemetryReadiness : std::uint8_t {
    Diagnostic,
    ProfileEligible,
    ModelEligible,
};

enum class TargetIdentityQuality : std::uint8_t {
    None,
    ProductionAssociated,
    StrongGeometricMatch,
    WeakGeometricMatch,
    Ambiguous,
};

enum class TargetEventKind : std::uint8_t {
    None,
    Created,
    Switched,
    Lost,
    Reacquired,
    Released,
};

enum class InputEventKind : std::uint8_t {
    None,
    AdsPressed,
    AdsReleased,
    TargetCreated,
    TargetSwitched,
    TargetLost,
    TargetReacquired,
    ManualAiConflict,
    TargetCrossed,
    BodylockEntered,
    BodylockExited,
    AuthorityChanged,
    InputStarted,
    InputPeak,
    DirectionReversed,
    InputSettled,
    InputEnded,
};

enum class AdsCalibrationClass : std::uint8_t {
    DiagnosticOnly,
    CalibrationClean,
    ConditionalModel,
};

enum class AdsInvalidReason : std::uint8_t {
    None,
    NoHipfireTarget,
    TargetSwitched,
    TargetLost,
    AdsNotSettled,
    LargeManualTurn,
    GeometryChanged,
    InsufficientFrames,
    IdentityAmbiguous,
    VisualSettleUnproven,
    MotionResidualHigh,
    SampleGap,
    RuntimeShutdown,
    QueueOverflow,
};

struct TelemetryCompleteness {
    std::uint64_t first_seq = 0;
    std::uint64_t last_seq = 0;
    std::uint32_t expected = 0;
    std::uint32_t written = 0;
    std::uint32_t dropped = 0;
    bool complete = false;
};

struct TelemetryTimestamps {
    std::uint64_t physical_read_ns = 0;
    std::uint64_t vision_capture_ns = 0;
    std::uint64_t inference_ready_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    std::uint64_t output_sent_ns = 0;
    std::uint64_t sample_ns = 0;
};

struct ControllerSamplePayload {
    bool physical_connected = false;
    bool current_observed_target_present = false;
    bool output_delivered = false;
    bool output_backend_connected = false;
    std::uint32_t output_error_code = 0;
    unsigned int input_reconnect_count = 0;
    unsigned int output_reconnect_count = 0;
    float physical_x = 0.0f;
    float physical_y = 0.0f;
    float manual_x = 0.0f;
    float manual_y = 0.0f;
    float filtered_manual_x = 0.0f;
    float filtered_manual_y = 0.0f;
    float manual_confidence = 0.0f;
    float ai_x = 0.0f;
    float ai_y = 0.0f;
    float target_final_x = 0.0f;
    float target_final_y = 0.0f;
    float ai_correction_x = 0.0f;
    float ai_correction_y = 0.0f;
    std::array<char, 32> manual_authority_mode{};
    std::array<char, 24> assist_control_phase{};
    bool manual_passthrough_x = true;
    bool manual_passthrough_y = true;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_boundary_x = false;
    bool manual_boundary_y = false;
    bool manual_exit_requested = false;
    bool handover_requested = false;
    bool handover_braking = false;
    float bodylock_error_rate_x = 0.0f;
    float bodylock_error_rate_y = 0.0f;
    float bodylock_position_stick_x = 0.0f;
    float bodylock_position_stick_y = 0.0f;
    float bodylock_motion_stick_x = 0.0f;
    float bodylock_motion_stick_y = 0.0f;
    float bodylock_effective_motion_stick_x = 0.0f;
    float bodylock_effective_motion_stick_y = 0.0f;
    bool bodylock_radial_motion_bound = false;
    std::array<char, 48> bodylock_constraint_reason{};
    float pre_recoil_x = 0.0f;
    float pre_recoil_y = 0.0f;
    float recoil_x = 0.0f;
    float recoil_y = 0.0f;
    float final_x = 0.0f;
    float final_y = 0.0f;
    float observed_error_x = 0.0f;
    float observed_error_y = 0.0f;
    float control_error_x = 0.0f;
    float control_error_y = 0.0f;
    float source_aim_x = 0.0f;
    float source_aim_y = 0.0f;
    float desired_aim_x = 0.0f;
    float desired_aim_y = 0.0f;
    float desired_point_u = 0.0f;
    float desired_point_v = 0.0f;
    float aim_region_x1 = 0.0f;
    float aim_region_y1 = 0.0f;
    float aim_region_x2 = 0.0f;
    float aim_region_y2 = 0.0f;
    bool has_aim_region = false;
    float visual_authority = 0.0f;
    bool enemy_cue_current = false;
    bool enemy_identity_confirmed = false;
    bool enemy_cue_checked = false;
    std::array<char, 24> aim_region_source{};
    std::array<char, 24> desired_point_source{};
    float requested_assist_x = 0.0f;
    float requested_assist_y = 0.0f;
    float shaped_assist_x = 0.0f;
    float shaped_assist_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool has_target = false;
    bool aim_authority = false;
    bool fire_authority = false;
    bool auto_fire_requested = false;
    bool auto_fire_aim_ready = false;
    bool auto_fire_allowed = false;
    bool auto_fire_active = false;
    std::uint64_t auto_fire_pulse_starts = 0;
    bool auto_fire_pulse_pressed = false;
    bool auto_fire_cadence_wait = false;
    bool final_fire_button = false;
    std::array<char, 28> auto_fire_block_reason{};
    bool enemy_mark_request_pending = false;
    bool enemy_mark_synthetic_pressed = false;
    bool enemy_mark_fired = false;
    bool enemy_mark_canceled = false;
    std::uint32_t enemy_mark_confirmation_frames = 0;
    std::uint64_t enemy_mark_target_scope = 0;
    std::uint64_t enemy_mark_target_generation = 0;
    std::uint64_t enemy_mark_last_scope = 0;
    std::uint64_t enemy_mark_last_generation = 0;
    std::array<char, 32> enemy_mark_block_reason{};
    std::uint32_t detector_box_count = 0;
    float production_target_confidence = 0.0f;
    float target_dx = 0.0f;
    float target_dy = 0.0f;
    float target_error_px = 0.0f;
    std::uint64_t selected_track_id = 0;
    std::uint64_t selected_observation_id = 0;
    TargetIdentityQuality target_identity_quality = TargetIdentityQuality::None;
    std::array<char, 24> aim_mode{};
    std::array<char, 32> production_target_source{};
    std::array<char, 24> production_target_tier{};
    std::array<char, 20> assist_authority{};
    std::array<char, 24> assist_authority_reason{};
    std::array<char, 16> bodylock_lifecycle{};
    std::array<char, 24> assist_limit_reason{};
};

struct SessionMetadataPayload {
    std::array<char, 33> session_id{};
    std::array<char, 41> build_commit{};
    std::array<char, 65> config_hash{};
    std::array<char, 65> engine_hash{};
    std::array<char, 65> executable_sha256{};
    int capture_width = 0;
    int capture_height = 0;
    int active_capture_fps = 0;
    int idle_capture_fps = 0;
    int controller_tick_hz = 0;
    int telemetry_hz = 0;
};

struct InputEventPayload {
    InputEventKind kind = InputEventKind::None;
    std::uint64_t input_episode_id = 0;
    float magnitude = 0.0f;
};

struct TargetEventPayload {
    TargetEventKind event = TargetEventKind::None;
    TargetIdentityQuality quality = TargetIdentityQuality::None;
    std::uint64_t previous_track_id = 0;
};

struct AdsTransitionPayload {
    AdsCalibrationClass calibration_class = AdsCalibrationClass::DiagnosticOnly;
    AdsInvalidReason invalid_reason = AdsInvalidReason::None;
    bool valid = false;
    std::uint64_t hipfire_frame_id = 0;
    std::uint64_t settled_frame_id = 0;
    float hipfire_dx = 0.0f, hipfire_dy = 0.0f;
    float ads_dx = 0.0f, ads_dy = 0.0f;
    float delta_dx = 0.0f, delta_dy = 0.0f;
    float scale_x = 1.0f, scale_y = 1.0f;
    float offset_x = 0.0f, offset_y = 0.0f;
    float settle_confidence = 0.0f;
    float cumulative_manual = 0.0f;
    float cumulative_ai = 0.0f;
    float cumulative_recoil = 0.0f;
};

struct CommittedObservationPayload {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t viewport_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    float stable_error_x = 0.0f, stable_error_y = 0.0f;
    float stable_body_width = 0.0f, stable_body_height = 0.0f;
    float raw_body_x = 0.0f, raw_body_y = 0.0f;
    float raw_body_width = 0.0f, raw_body_height = 0.0f;
    float motion_anchor_x = 0.0f, motion_anchor_y = 0.0f;
    float motion_anchor_score = 0.0f;
    float viewport_offset_x = 0.0f, viewport_offset_y = 0.0f;
    float target_acceleration_x = 0.0f, target_acceleration_y = 0.0f;
    float reliability = 0.0f;
    float normalized_size = 0.0f;
    std::uint64_t ads_epoch = 0;
    std::uint16_t eligible_candidate_count = 0;
    std::uint8_t lifecycle = 0;
    std::uint8_t motion = 0;
    std::uint8_t mode = 0;
    bool fresh_observed = false;
    bool strong_observation = false;
    bool stable_coordinates_valid = false;
    bool has_motion_anchor = false;
};

struct AdsAcquisitionTracePayload {
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
    float raw_error_x = 0.0f;
    float raw_error_y = 0.0f;
    float target_size_x = 0.0f;
    float target_size_y = 0.0f;
    float requested_ai_x = 0.0f;
    float requested_ai_y = 0.0f;
    float shaped_ai_x = 0.0f;
    float shaped_ai_y = 0.0f;
    float fused_output_x = 0.0f;
    float fused_output_y = 0.0f;
    float post_output_x = 0.0f;
    float post_output_y = 0.0f;
    float first_requested_ai_x = 0.0f;
    float first_requested_ai_y = 0.0f;
    float first_shaped_ai_x = 0.0f;
    float first_shaped_ai_y = 0.0f;
    float first_fused_output_x = 0.0f;
    float first_fused_output_y = 0.0f;
};

struct DeliveredControlPayload {
    std::uint64_t sample_seq = 0;
    std::uint64_t applied_at_ns = 0;
    float final_right_x = 0.0f, final_right_y = 0.0f;
    float final_left_x = 0.0f, final_left_y = 0.0f;
    std::uint64_t ads_epoch = 0;
    bool output_delivered = false;
    bool output_disabled = false;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
};

struct TelemetryRecord {
    std::uint16_t schema_version = kTelemetrySchemaVersion;
    TelemetryRecordType type = TelemetryRecordType::ControllerSample;
    TelemetryReadiness readiness = TelemetryReadiness::Diagnostic;
    bool critical = false;
    std::uint64_t event_id = 0;
    std::uint64_t tick_id = 0;
    std::uint64_t frame_id = 0;
    std::uint64_t intent_id = 0;
    std::uint64_t sample_seq = 0;
    std::uint64_t target_track_id = 0;
    TelemetryTimestamps timestamps;
    TelemetryCompleteness completeness;
    SessionMetadataPayload session_metadata;
    ControllerSamplePayload controller;
    InputEventPayload input_event;
    TargetEventPayload target_event;
    AdsTransitionPayload ads_transition;
    CommittedObservationPayload committed_observation;
    DeliveredControlPayload delivered_control;
    AdsAcquisitionTracePayload ads_acquisition_trace;
    VisionSampleQuality vision_sample_quality = VisionSampleQuality::Normal;
};

static_assert(std::is_trivially_copyable_v<TelemetryRecord>);
static_assert(sizeof(TelemetryRecord) <= 32u * 1024u,
              "telemetry queue records must remain fixed and <=32 KiB");

} // namespace runtime_app
