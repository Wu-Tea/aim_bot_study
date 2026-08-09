#pragma once

#include "../pipeline_contract/target_plan.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

// Keeps the target solver as the sole direction owner while allowing physical
// input that agrees with the solved target vector to add a small, bounded
// amount of force. This is intended to cross an in-game aim-assist slowdown
// bubble without restoring unbounded manual + AI addition.
inline pipeline_contract::Vec2f apply_helpful_manual_overdrive(
    pipeline_contract::Vec2f target_stick,
    pipeline_contract::Vec2f physical_manual_stick,
    float max_scale) {
    const float target_magnitude = std::hypot(target_stick.x, target_stick.y);
    const float manual_magnitude = std::hypot(
        physical_manual_stick.x, physical_manual_stick.y);
    if (!std::isfinite(target_magnitude) ||
        !std::isfinite(manual_magnitude) ||
        target_magnitude <= 1.0e-5f || manual_magnitude <= 1.0e-5f) {
        return target_stick;
    }

    const float bounded_scale = std::clamp(max_scale, 1.0f, 1.25f);
    if (bounded_scale <= 1.0f) return target_stick;

    const pipeline_contract::Vec2f target_direction{
        target_stick.x / target_magnitude,
        target_stick.y / target_magnitude};
    const float helpful_projection =
        physical_manual_stick.x * target_direction.x +
        physical_manual_stick.y * target_direction.y;
    if (!std::isfinite(helpful_projection) || helpful_projection <= 0.0f) {
        return target_stick;
    }

    const float overdrive_fraction = bounded_scale - 1.0f;
    const float requested_extra = helpful_projection * overdrive_fraction;
    const float target_headroom = target_magnitude * overdrive_fraction;
    const float extra = std::min(requested_extra, target_headroom);
    const float final_magnitude = std::min(1.0f, target_magnitude + extra);
    return {
        target_direction.x * final_magnitude,
        target_direction.y * final_magnitude};
}

}  // namespace controller_native
