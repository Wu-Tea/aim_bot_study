#include "target_geometry.h"

#include <algorithm>

namespace controller_native {

TargetGeometryResult resolve_target_geometry(
    const TargetGeometryInput& input,
    const TargetGeometryConfig& config) noexcept {
    const auto& box = input.body_box_px;
    if (!input.has_body_box || box.w <= 0.0f || box.h <= 0.0f) {
        return {input.vision_aim_px, false};
    }

    if (box.h / box.w < 0.65f) {
        return {{
            std::clamp(input.vision_aim_px.x, box.x, box.x + box.w),
            std::clamp(input.vision_aim_px.y, box.y, box.y + box.h)}, true};
    }

    return {{
        input.vision_aim_px.x,
        box.y + box.h * std::clamp(config.aim_height_ratio, 0.0f, 1.0f)}, true};
}

}  // namespace controller_native
