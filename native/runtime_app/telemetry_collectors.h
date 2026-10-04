#pragma once

#include "pipeline_contract/committed_capture_observation.h"
#include "runtime_telemetry.h"

#include <cstdint>
#include <memory>

namespace runtime_app {

struct TelemetryTickInput {
    // Numeric/output fields use the record shape directly. Borrowed text is
    // copied only when sampled, preserving the existing hot-path cadence.
    ControllerSamplePayload controller{};
    std::uint64_t tick_id = 0;
    std::uint64_t physical_read_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    std::uint64_t output_sent_ns = 0;
    std::uint64_t sample_ns = 0;
    bool aiming = false;
    bool output_disabled = false;
    const char* aim_mode = "none";
    const char* manual_authority_mode = "no_target_passthrough";
    const char* assist_control_phase = "manual";
    const char* operation_class = "no_gesture";
    const char* bodylock_constraint_reason = "none";
    const char* auto_fire_block_reason = "none";
    const char* enemy_mark_block_reason = "disabled";
    const char* aim_region_source = "none";
    const char* desired_point_source = "none";
    float final_left_x = 0.0f, final_left_y = 0.0f;
    bool output_saturated = false;
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

// Acquisition inputs already have the fixed-size, pointer-free wire shape.
// Keep one field definition and copy the value as a whole into the record.
using TelemetryAcquisitionTraceInput = AdsAcquisitionTracePayload;

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
