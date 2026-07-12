#pragma once

#include <cstdint>

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
    ControllerSamplePayload controller;

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
