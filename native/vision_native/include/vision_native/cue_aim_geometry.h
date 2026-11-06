#pragma once
#include <cmath>

namespace vision_native {
// Cue pixels are an anchor above the anatomical point, never its aim region.
inline bool cue_aim_is_below_marker(float aim_y, float cue_y, float cue_size) noexcept {
    return std::isfinite(aim_y) && std::isfinite(cue_y) &&
        std::isfinite(cue_size) && cue_size >= 0 && aim_y > cue_y + cue_size * .5f;
}

// Model confidence says "person", not "complete body". A contracting box
// cannot shorten the calibrated head gap. Recovered extent can refresh it in
// either direction; a point farther below the cue also supplies a wider gap.
// Current person admission remains independent from this calibration policy.
inline bool supports_cue_reference_refresh(float width, float height, float offset_y,
    float calibrated_width, float calibrated_height, float calibrated_offset_y) noexcept {
    return (width >= calibrated_width && height >= calibrated_height) ||
        offset_y >= calibrated_offset_y;
}
}
