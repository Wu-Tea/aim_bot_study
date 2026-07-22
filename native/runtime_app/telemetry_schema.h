#pragma once

#include <cstdint>
#include <array>
#include <type_traits>

namespace runtime_app {

inline constexpr std::uint16_t kTelemetrySchemaVersion = 6;

enum class TelemetryRecordKind : std::uint8_t {
    ManualControllerTick,
    VisionFrame,
    RuntimeEvent,
};

enum class TelemetryRecordType : std::uint8_t {
    SessionMetadata,
    ControllerSample,
    InputEvent,
    TargetEvent,
    AdsTransitionSample,
    AdsTransition,
    ControlResponseWindow,
    CommittedCaptureObservation,
    DeliveredControlSample,
    CausalResponseShadow,
};

enum class VisionSampleQuality : std::uint8_t {
    Normal,
    SoftWeight,
    HardReject,
};

enum class IdentificationUpdateOutcome : std::uint8_t {
    NotEvaluated,
    AcceptedByAtLeastOneDelay,
    InsufficientExcitation,
    DeliveryGap,
    FiringOrRecoil,
    Saturated,
    TimingInvalid,
    CoordinateInvalid,
    IdentityBoundary,
    NoUsableDelay,
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
    ProjectedContinuity,
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
    ProjectedOnlyAnchor,
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

enum class ResponseWindowReason : std::uint8_t {
    None,
    TargetChanged,
    IdentityWeak,
    GeometryChanged,
    SampleGap,
    TimingInvalid,
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
    float ai_x = 0.0f;
    float ai_y = 0.0f;
    float post_ai_x = 0.0f;
    float post_ai_y = 0.0f;
    float dynamic_adjustment_x = 0.0f;
    float dynamic_adjustment_y = 0.0f;
    float post_dynamic_x = 0.0f;
    float post_dynamic_y = 0.0f;
    float ads_brake_x = 0.0f;
    float ads_brake_y = 0.0f;
    float post_ads_brake_x = 0.0f;
    float post_ads_brake_y = 0.0f;
    float ads_carry_brake_x = 0.0f;
    float ads_carry_brake_y = 0.0f;
    float post_ads_carry_brake_x = 0.0f;
    float post_ads_carry_brake_y = 0.0f;
    float pre_recoil_x = 0.0f;
    float pre_recoil_y = 0.0f;
    float recoil_x = 0.0f;
    float recoil_y = 0.0f;
    float final_x = 0.0f;
    float final_y = 0.0f;
    float requested_assist_x = 0.0f;
    float requested_assist_y = 0.0f;
    float shaped_assist_x = 0.0f;
    float shaped_assist_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool has_target = false;
    bool aim_authority = false;
    bool fire_authority = false;
    bool ads_brake_active = false;
    bool ads_carry_brake_active = false;
    bool ads_completion_active = false;
    int ads_completion_stable_frames = 0;
    float ads_completion_radius_px = 0.0f;
    int ads_completion_required_frames = 0;
    float ads_completion_max_ms = 0.0f;
    std::array<char, 20> ads_completion_reason{};
    bool manual_takeover_active = false;
    bool auto_fire_requested = false;
    bool auto_fire_aim_ready = false;
    bool auto_fire_allowed = false;
    bool auto_fire_active = false;
    std::uint64_t auto_fire_pulse_starts = 0;
    bool auto_fire_pulse_pressed = false;
    bool auto_fire_cadence_wait = false;
    bool final_fire_button = false;
    std::array<char, 28> auto_fire_block_reason{};
    std::uint32_t detector_box_count = 0;
    float production_target_confidence = 0.0f;
    float target_dx = 0.0f;
    float target_dy = 0.0f;
    float target_error_px = 0.0f;
    std::uint64_t selected_track_id = 0;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t backing_frame_id = 0;
    float track_observation_age_ms = 0.0f;
    float track_position_sigma = 0.0f;
    float track_ambiguity = 0.0f;
    TargetIdentityQuality target_identity_quality = TargetIdentityQuality::None;
    std::array<char, 24> aim_mode{};
    std::array<char, 32> production_target_source{};
    std::array<char, 24> production_target_tier{};
    std::array<char, 20> assist_authority{};
    std::array<char, 24> assist_authority_reason{};
    std::array<char, 16> bodylock_lifecycle{};
    std::array<char, 24> bodylock_transition_reason{};
    std::array<char, 24> assist_limit_reason{};
};

struct SessionMetadataPayload {
    std::array<char, 33> session_id{};
    std::array<char, 41> build_commit{};
    std::array<char, 65> config_hash{};
    std::array<char, 65> engine_hash{};
    std::array<char, 32> tracker_backend{};
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

struct ControlResponsePayload {
    ResponseWindowReason reason = ResponseWindowReason::None;
    std::uint64_t frame_id_before = 0;
    std::uint64_t frame_id_after = 0;
    float delta_error_x = 0.0f, delta_error_y = 0.0f;
    float residual_x = 0.0f, residual_y = 0.0f;
    float manual_x_integral = 0.0f, manual_y_integral = 0.0f;
    float ai_x_integral = 0.0f, ai_y_integral = 0.0f;
    float pre_recoil_x_integral = 0.0f, pre_recoil_y_integral = 0.0f;
    float recoil_x_integral = 0.0f, recoil_y_integral = 0.0f;
    float final_x_integral = 0.0f, final_y_integral = 0.0f;
};

struct CommittedObservationPayload {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t viewport_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    float stable_error_x = 0.0f, stable_error_y = 0.0f;
    float stable_body_width = 0.0f, stable_body_height = 0.0f;
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
    bool reused_or_projected = false;
};

struct DeliveredControlPayload {
    std::uint64_t sample_seq = 0;
    std::uint64_t applied_at_ns = 0;
    float physical_right_x = 0.0f, physical_right_y = 0.0f;
    float physical_left_x = 0.0f, physical_left_y = 0.0f;
    float manual_x = 0.0f, manual_y = 0.0f;
    float ai_x = 0.0f, ai_y = 0.0f;
    float pre_recoil_x = 0.0f, pre_recoil_y = 0.0f;
    float recoil_x = 0.0f, recoil_y = 0.0f;
    float final_right_x = 0.0f, final_right_y = 0.0f;
    float final_left_x = 0.0f, final_left_y = 0.0f;
    std::uint64_t ads_epoch = 0;
    bool output_delivered = false;
    bool output_disabled = false;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
};

struct CausalResponseShadowPayload {
    float best_delay_ms = 0.0f;
    float selected_delay_ms = 0.0f;
    float selected_delay_confidence = 0.0f;
    float right_confidence = 0.0f;
    float left_confidence = 0.0f;
    float joint_confidence = 0.0f;
    float excitation = 0.0f;
    float residual = 0.0f;
    float pending_realized_x = 0.0f;
    float pending_realized_y = 0.0f;
    float pending_scheduled_x = 0.0f;
    float pending_scheduled_y = 0.0f;
    float pending_confidence = 0.0f;
    std::uint32_t reason_bits = 0;
    std::uint8_t accepted_delay_count = 0;
    bool accepted_by_any_delay = false;
    bool delay_switch_pending = false;
    bool pending_valid = false;
    std::array<float, 5> rollout_scales{};
    std::array<float, 5> rollout_costs{};
    float rollout_best_scale = 1.0f;
    float rollout_confidence = 0.0f;
    std::uint8_t rollout_candidate_count = 0;
    bool rollout_valid = false;
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
    ControlResponsePayload control_response;
    CommittedObservationPayload committed_observation;
    DeliveredControlPayload delivered_control;
    CausalResponseShadowPayload causal_shadow;
    VisionSampleQuality vision_sample_quality = VisionSampleQuality::Normal;
    IdentificationUpdateOutcome identification_update_outcome =
        IdentificationUpdateOutcome::NotEvaluated;

    // Compatibility fields used by the current runtime producer until the
    // enabled-only collectors are integrated.
    TelemetryRecordKind kind = TelemetryRecordKind::ManualControllerTick;
    std::uint64_t timestamp_ns = 0;
    float manual_x = 0.0f;
    float manual_y = 0.0f;
    float ai_x = 0.0f;
    float ai_y = 0.0f;
    float final_x = 0.0f;
    float final_y = 0.0f;
    float controller_pipeline_ms = 0.0f;
    float vigem_update_ms = 0.0f;
    std::uint32_t event_reason_flags = 0;
};

static_assert(std::is_trivially_copyable_v<TelemetryRecord>);
static_assert(sizeof(TelemetryRecord) <= 4096,
              "telemetry queue records must remain fixed and bounded");

} // namespace runtime_app
