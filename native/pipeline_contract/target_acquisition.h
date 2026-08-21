#pragma once

#include <algorithm>
#include <cmath>

namespace pipeline_contract {

// New target identity uses one geometry rule across Vision selection and
// Controller telemetry. The base radius describes a small/far target. Apparent
// body height widens the envelope smoothly so a close person can be acquired
// farther from the reticle without granting the same reach to a distant one.
inline float target_scaled_pickup_radius(
    float base_radius_px,
    float normalized_target_size) noexcept {
    const float safe_base = std::isfinite(base_radius_px)
        ? std::max(0.0f, base_radius_px)
        : 0.0f;
    const float safe_size = std::isfinite(normalized_target_size)
        ? std::clamp(normalized_target_size, 0.0f, 1.0f)
        : 0.0f;
    return safe_base * (1.0f + 0.75f * safe_size);
}

}  // namespace pipeline_contract
