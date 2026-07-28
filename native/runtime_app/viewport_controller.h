#pragma once

#include "pipeline_contract/committed_capture_observation.h"
#include "pipeline_contract/target_plan.h"

#include <cstdint>

namespace runtime_app {

enum class ViewportLevel : unsigned char {
    Precision = 0,
    Normal = 1,
    Rescue = 2,
};

const char* viewport_level_name(ViewportLevel level) noexcept;

struct ViewportDimensions {
    int width = 0;
    int height = 0;
};

struct ViewportControllerConfig {
    bool enabled = false;
    ViewportDimensions precision{360, 312};
    ViewportDimensions normal{480, 416};
    ViewportDimensions rescue{600, 520};
    float prediction_seconds = 0.100f;
    float safety_margin_x_px = 24.0f;
    float safety_margin_y_px = 20.0f;
    float shrink_extra_margin_px = 30.0f;
    float center_velocity_alpha = 0.45f;
    float size_velocity_alpha = 0.35f;
    float max_center_velocity_px_per_sec = 1600.0f;
    float max_half_size_velocity_px_per_sec = 1000.0f;
    float aim_height_ratio = 0.365f;
    std::uint32_t expand_confirm_frames = 2;
    float normal_min_dwell_ms = 200.0f;
    float rescue_min_dwell_ms = 300.0f;
    float shrink_stable_ms = 400.0f;
    float edge_loss_rescue_hold_ms = 200.0f;
};

struct ViewportRequest {
    ViewportLevel level = ViewportLevel::Normal;
    int width = 0;
    int height = 0;
    std::uint64_t sequence = 0;
    std::uint64_t source_frame_id = 0;
    bool changed = false;
};

class ViewportController {
public:
    explicit ViewportController(ViewportControllerConfig config = {});

    ViewportRequest update(
        const pipeline_contract::TargetPlan& plan,
        const pipeline_contract::CommittedCaptureObservation* observation,
        std::uint64_t now_ns) noexcept;

    ViewportRequest reset(std::uint64_t now_ns = 0) noexcept;
    const ViewportRequest& current() const noexcept;

private:
    ViewportDimensions dimensions(ViewportLevel level) const noexcept;
    ViewportLevel required_level(
        float center_x,
        float center_y,
        float half_width,
        float half_height,
        float extra_margin = 0.0f) const noexcept;
    bool fits_level(
        ViewportLevel level,
        float center_x,
        float center_y,
        float half_width,
        float half_height,
        float extra_margin) const noexcept;
    void switch_to(
        ViewportLevel level,
        std::uint64_t now_ns,
        std::uint64_t source_frame_id) noexcept;

    ViewportControllerConfig config_{};
    ViewportRequest current_{};
    std::uint64_t target_id_ = 0;
    std::uint64_t last_source_frame_id_ = 0;
    std::uint64_t last_observed_ns_ = 0;
    std::uint64_t state_entered_ns_ = 0;
    std::uint64_t shrink_candidate_since_ns_ = 0;
    std::uint32_t expand_evidence_frames_ = 0;
    float center_x_ = 0.0f;
    float center_y_ = 0.0f;
    float half_width_ = 0.0f;
    float half_height_ = 0.0f;
    float center_velocity_x_ = 0.0f;
    float center_velocity_y_ = 0.0f;
    float half_width_velocity_ = 0.0f;
    float half_height_velocity_ = 0.0f;
    bool has_geometry_ = false;
    bool edge_pressure_ = false;
};

}  // namespace runtime_app
