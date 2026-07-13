#pragma once

#include "ads_visual_transition.h"

#include <cstdint>
#include <optional>

namespace runtime_app {

struct AdsTransitionCollectorOptions {
    std::uint64_t timeout_ns = 500'000'000;
    unsigned int minimum_new_frames = 4;
    float clean_command_integral = 0.002f;
    AdsVisualTransitionOptions visual{3, 0.04f, 3.0f, 6.0f};
};

struct AdsTransitionEvent {
    std::uint64_t ads_event_id = 0;
    std::uint64_t target_track_id = 0;
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
    bool valid = false;
    AdsCalibrationClass calibration_class = AdsCalibrationClass::DiagnosticOnly;
    AdsInvalidReason invalid_reason = AdsInvalidReason::None;
    TelemetryReadiness readiness = TelemetryReadiness::Diagnostic;
    TelemetryCompleteness completeness;
};

class AdsTransitionCollector {
public:
    explicit AdsTransitionCollector(
        AdsTransitionCollectorOptions options = AdsTransitionCollectorOptions{});
    void observe_hipfire(const AdsVisualFrame& frame) noexcept;
    void on_ads_pressed(std::uint64_t timestamp_ns) noexcept;
    void observe_vision(const AdsVisualFrame& frame) noexcept;
    void observe_command(float manual_integral, float ai_integral, float recoil_integral) noexcept;
    void on_tick(std::uint64_t timestamp_ns) noexcept;
    void on_required_sample_dropped() noexcept;
    void shutdown(std::uint64_t timestamp_ns) noexcept;
    std::optional<AdsTransitionEvent> take_completed() noexcept;

private:
    void invalidate(AdsInvalidReason reason) noexcept;
    void complete(const AdsVisualFrame& frame, const AdsVisualEvidence& evidence) noexcept;
    bool high_quality(const AdsVisualFrame& frame) const noexcept;

    AdsTransitionCollectorOptions options_;
    AdsVisualTransitionEstimator visual_;
    AdsVisualFrame hipfire_;
    bool has_hipfire_ = false;
    bool active_ = false;
    std::uint64_t next_event_id_ = 1;
    std::uint64_t active_event_id_ = 0;
    std::uint64_t pressed_at_ns_ = 0;
    std::uint64_t first_seq_ = 0;
    std::uint64_t last_seq_ = 0;
    unsigned int new_frames_ = 0;
    std::uint32_t gaps_ = 0;
    float cumulative_manual_ = 0.0f;
    float cumulative_ai_ = 0.0f;
    float cumulative_recoil_ = 0.0f;
    std::optional<AdsTransitionEvent> completed_;
};

} // namespace runtime_app
