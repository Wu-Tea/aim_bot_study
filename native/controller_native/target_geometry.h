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

TargetGeometryResult resolve_target_geometry(
    const TargetGeometryInput& input,
    const TargetGeometryConfig& config) noexcept;

}  // namespace controller_native
