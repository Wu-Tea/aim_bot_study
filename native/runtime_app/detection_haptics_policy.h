#pragma once

#include "vision_native/types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace runtime_app {
enum class HapticDirection : unsigned char { None, Left, Center, Right };

// Notification-only occupancy. Never changes selector identity or control authority.
// Re-arm only from a continuous run of fresh empty captures, not a stalled feed.
class DetectionHapticsPolicy {
public:
    void reset() noexcept { *this = {}; }
    HapticDirection observe(const vision_native::VisionResult& result, float capture_width) noexcept {
        const auto time = result.captured_at_ns;
        if (!result.frame_updated || !time || !result.frame_id ||
            result.frame_id <= last_frame_ || time <= last_capture_ ||
            !std::isfinite(capture_width) || capture_width <= 0) return HapticDirection::None;
        if (time - last_capture_ > 100'000'000) empty_since_ = 0;
        last_frame_ = result.frame_id;
        last_capture_ = time;
        float nearest = std::numeric_limits<float>::max(), x = 0;
        bool present = false;
        for (const auto& box : result.detections) {
            if (box.class_id != 0 || box.is_friendly || !std::isfinite(box.conf) || box.conf <= 0 ||
                !std::isfinite(box.x1) || !std::isfinite(box.x2) ||
                !std::isfinite(box.y1) || !std::isfinite(box.y2) ||
                box.x2 <= box.x1 || box.y2 <= box.y1) continue;
            const float center = (box.x1 + box.x2) * .5f;
            const float distance = std::abs(center - capture_width * .5f);
            if (distance < nearest) { nearest = distance; x = center; present = true; }
        }
        if (!present) {
            if (!empty_since_) empty_since_ = time;
            if (time - empty_since_ >= 250'000'000) occupied_ = false;
            return HapticDirection::None;
        }
        empty_since_ = 0;
        if (occupied_) return HapticDirection::None;
        occupied_ = true;
        return x < capture_width / 3 ? HapticDirection::Left :
            x > capture_width * (2.0f / 3) ? HapticDirection::Right : HapticDirection::Center;
    }
private:
    std::uint64_t last_frame_ = 0, last_capture_ = 0, empty_since_ = 0;
    bool occupied_ = false;
};

inline float detection_haptic_sample(double seconds, float strength) noexcept {
    if (seconds < 0 || seconds >= .1 || !std::isfinite(strength)) return 0;
    const double envelope = std::min({1.0, seconds / .01, (.1 - seconds) / .01});
    return static_cast<float>(std::clamp(strength, 0.0f, 1.0f) * envelope *
        std::sin(2 * 3.141592653589793 * 120 * seconds));
}
inline bool haptic_channel_active(HapticDirection direction, unsigned channel) noexcept {
    return (channel == 2 && (direction == HapticDirection::Left || direction == HapticDirection::Center)) ||
           (channel == 3 && (direction == HapticDirection::Right || direction == HapticDirection::Center));
}
} // namespace runtime_app
