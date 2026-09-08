#pragma once

#include <cmath>
#include <cstdint>

namespace mouse_native {

enum class MouseAimMode : std::uint8_t {
    Hipfire = 0,
    Ads = 1,
};

struct MouseResponseProfile {
    float px_per_count_x = 0.0f;
    float px_per_count_y = 0.0f;
    float counts_per_u_second_x = 0.0f;
    float counts_per_u_second_y = 0.0f;
    float confidence = 0.0f;
    std::uint64_t generation = 0;
    bool calibrated = false;
    bool estimated = false;
};

inline bool valid(const MouseResponseProfile& profile) noexcept {
    return (profile.calibrated || profile.estimated) && profile.generation != 0 &&
        std::isfinite(profile.px_per_count_x) &&
        std::isfinite(profile.px_per_count_y) &&
        std::isfinite(profile.counts_per_u_second_x) &&
        std::isfinite(profile.counts_per_u_second_y) &&
        std::isfinite(profile.confidence) &&
        profile.px_per_count_x > 0.0f &&
        profile.px_per_count_y > 0.0f &&
        profile.counts_per_u_second_x > 0.0f &&
        profile.counts_per_u_second_y > 0.0f &&
        profile.confidence >= 0.0f && profile.confidence <= 1.0f;
}

struct MouseSourceCounts {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

struct MouseNormalizedInput {
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
    bool transparent = true;
    bool envelope_exceeded = false;
};

struct MouseActuationResult {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    bool valid = false;
    bool transparent = false;
    bool saturated = false;
};

}  // namespace mouse_native
