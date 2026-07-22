#pragma once

#include "pipeline_contract/committed_capture_observation.h"
#include "control_learning/causal_online_response_learner.h"
#include "control_learning/pending_motion_model.h"
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
    float ai_x = 0.0f, ai_y = 0.0f;
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
    void observe_committed_capture(
        const pipeline_contract::CommittedCaptureObservation& observation) noexcept;
    const control_learning::ControlHistory<1024>* control_history() const noexcept;
    void observe_causal_shadow(
        const pipeline_contract::CommittedCaptureObservation& observation,
        const control_learning::SampleAssessment& assessment,
        const control_learning::CausalResponseEstimate& estimate,
        const control_learning::PendingMotionEstimate& pending) noexcept;
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
