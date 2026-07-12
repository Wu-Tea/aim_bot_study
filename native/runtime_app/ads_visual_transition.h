#pragma once

#include "telemetry_schema.h"

#include <cstdint>

namespace runtime_app {

struct AdsVisualFrame {
    std::uint64_t frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t target_track_id = 0;
    TargetIdentityQuality identity_quality = TargetIdentityQuality::None;
    bool live = false;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    float target_x = 0.0f, target_y = 0.0f;
    float motion_residual_px = 0.0f;
};

struct AdsVisualTransitionOptions {
    unsigned int settle_consecutive_frames = 3;
    float max_scale_derivative = 0.011f;
    float max_offset_derivative_px = 0.25f;
    float max_motion_residual_px = 0.5f;
};

struct AdsVisualEvidence {
    std::uint64_t frame_id = 0;
    bool accepted_new_frame = false;
    bool settled = false;
    bool clean_calibration = false;
    float scale_x = 1.0f, scale_y = 1.0f;
    float offset_x = 0.0f, offset_y = 0.0f;
    float visual_progress = 0.0f;
    float settle_confidence = 0.0f;
    float motion_residual_px = 0.0f;
};

class AdsVisualTransitionEstimator {
public:
    explicit AdsVisualTransitionEstimator(AdsVisualTransitionOptions options);
    void start(const AdsVisualFrame& anchor) noexcept;
    AdsVisualEvidence observe(const AdsVisualFrame& frame) noexcept;
    void observe_timer_only(std::uint64_t timestamp_ns) noexcept;
    const AdsVisualEvidence& latest() const noexcept;
    void reset() noexcept;

private:
    bool high_quality(const AdsVisualFrame& frame) const noexcept;

    AdsVisualTransitionOptions options_;
    AdsVisualFrame anchor_;
    AdsVisualFrame previous_;
    AdsVisualEvidence latest_;
    bool active_ = false;
    unsigned int stable_frames_ = 0;
};

} // namespace runtime_app
