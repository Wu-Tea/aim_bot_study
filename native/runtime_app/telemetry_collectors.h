#pragma once

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
    bool aim_authority = false;
    bool fire_authority = false;
    const char* aim_mode = "none";
    float left_trigger = 0.0f, right_trigger = 0.0f;
    float physical_x = 0.0f, physical_y = 0.0f;
    float manual_x = 0.0f, manual_y = 0.0f;
    float ai_x = 0.0f, ai_y = 0.0f;
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
    float pre_recoil_x = 0.0f, pre_recoil_y = 0.0f;
    float recoil_x = 0.0f, recoil_y = 0.0f;
    float final_x = 0.0f, final_y = 0.0f;
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

struct TelemetryCollectorsCounters {
    std::uint64_t state_transitions = 0;
    std::uint64_t constructed_records = 0;
};

struct TelemetrySessionContext {
    const char* build_commit = "unknown";
    const char* config_hash = "unknown";
    const char* engine_hash = "unknown";
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
