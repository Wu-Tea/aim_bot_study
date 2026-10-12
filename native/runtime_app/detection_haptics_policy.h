#pragma once

#include "vision_native/types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace runtime_app {
enum class HapticDirection : unsigned char { None, Left, Center, Right };

// Consume selector ownership, never elect another target or apply another
// admission threshold. One pulse per selected identity, including handover.
class DetectionHapticsPolicy {
public:
    void reset() noexcept { *this = {}; }
    HapticDirection observe(const vision_native::VisionResult& result, float capture_width) noexcept {
        const auto time = result.captured_at_ns;
        if (!result.frame_updated || !time || !result.frame_id ||
            result.frame_id <= last_frame_ || time <= last_capture_ ||
            !std::isfinite(capture_width) || capture_width <= 0) return HapticDirection::None;
        last_frame_ = result.frame_id;
        last_capture_ = time;
        // Idle detection deliberately has no actuator authority. Selection
        // remains valid there; cue-only continuation cannot start a pulse.
        if (!result.has_target || !result.has_selected_detection ||
            !result.selector_target_generation ||
            result.selector_target_generation == notified_generation_ ||
            !std::isfinite(result.target_x)) return HapticDirection::None;
        notified_generation_ = result.selector_target_generation;
        const float x = result.target_x;
        return x < capture_width / 3 ? HapticDirection::Left :
            x > capture_width * (2.0f / 3) ? HapticDirection::Right : HapticDirection::Center;
    }
private:
    std::uint64_t last_frame_ = 0, last_capture_ = 0, notified_generation_ = 0;
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
