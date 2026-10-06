#pragma once
#include <algorithm>

namespace controller_native {
inline constexpr float kBodylockFeedbackMinimumPx = 18.0f;
inline constexpr float kBodylockFeedbackMultiplier = 1.5f;
// Compatibility conversion only. New configurations store the effective px.
inline float legacy_bodylock_feedback_distance(float configured) noexcept {
    return std::max(kBodylockFeedbackMinimumPx, configured * kBodylockFeedbackMultiplier);
}
inline float bodylock_feedback_distance(float legacy, float explicit_distance = 0.0f) noexcept {
    return explicit_distance > 0.0f ? explicit_distance : legacy_bodylock_feedback_distance(legacy);
}
} // namespace controller_native
