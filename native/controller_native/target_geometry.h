#pragma once

#include "../common_native/screen_geometry.h"

namespace controller_native {

struct TargetGeometryConfig {
    float aim_height_ratio = 0.365f;
};

struct TargetGeometryInput {
    common_native::Vec2f vision_aim_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
};

struct TargetGeometryResult {
    common_native::Vec2f aim_px;
    bool geometry_resolved = false;
};

struct StableBodyAimResult {
    common_native::Vec2f aim_px;
    bool rejected_shape_motion = false;
};

// Uses an independently measured upper-body appearance anchor for rigid
// person motion, instead of deriving motion from detector-box edges that a
// weapon or optic can reconstruct. Rejected box displacement is never stored
// as debt, so a restored box cannot release an old impulse later.
class StableBodyAimTracker {
public:
    StableBodyAimResult update(
        common_native::Vec2f resolved_aim_px,
        common_native::Box2f body_box_px,
        bool stabilize_shape_motion,
        bool has_motion_anchor = false,
        common_native::Vec2f motion_anchor_px = {}) noexcept;
    void reset() noexcept;

private:
    common_native::Vec2f previous_raw_aim_px_{};
    common_native::Vec2f stable_aim_px_{};
    common_native::Vec2f previous_motion_anchor_px_{};
    common_native::Vec2f last_anchor_delta_px_{};
    common_native::Vec2f predicted_motion_since_anchor_px_{};
    int missing_anchor_frames_ = 0;
    bool has_previous_motion_anchor_ = false;
    bool initialized_ = false;
};

TargetGeometryResult resolve_target_geometry(
    const TargetGeometryInput& input,
    const TargetGeometryConfig& config) noexcept;

}  // namespace controller_native
