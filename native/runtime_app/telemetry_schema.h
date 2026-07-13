#pragma once

#include <cstdint>
#include <array>

namespace runtime_app {

inline constexpr std::uint16_t kTelemetrySchemaVersion = 2;

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
    float physical_x = 0.0f;
    float physical_y = 0.0f;
    float manual_x = 0.0f;
    float manual_y = 0.0f;
    float ai_x = 0.0f;
    float ai_y = 0.0f;
    float pre_recoil_x = 0.0f;
    float pre_recoil_y = 0.0f;
    float recoil_x = 0.0f;
    float recoil_y = 0.0f;
    float final_x = 0.0f;
    float final_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool has_target = false;
    bool aim_authority = false;
    bool fire_authority = false;
    float target_dx = 0.0f;
    float target_dy = 0.0f;
    float target_error_px = 0.0f;
    TargetIdentityQuality target_identity_quality = TargetIdentityQuality::None;
    std::array<char, 24> aim_mode{};
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

} // namespace runtime_app
