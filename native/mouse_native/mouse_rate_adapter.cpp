#include "mouse_native/mouse_rate_adapter.h"

#include <cmath>

namespace mouse_native {

MouseRateAdapter::MouseRateAdapter(MouseRateAdapterConfig config)
    : config_(config) {
    if (!std::isfinite(config_.maximum_abs_normalized) ||
        config_.maximum_abs_normalized <= 0.0f) {
        config_.maximum_abs_normalized = 1.0f;
    }
    if (!std::isfinite(config_.minimum_dt_seconds) ||
        config_.minimum_dt_seconds <= 0.0f) {
        config_.minimum_dt_seconds = 0.0001f;
    }
    if (!std::isfinite(config_.maximum_dt_seconds) ||
        config_.maximum_dt_seconds < config_.minimum_dt_seconds) {
        config_.maximum_dt_seconds = 0.0500f;
    }
}

MouseNormalizedInput MouseRateAdapter::adapt(
    MouseSourceCounts source,
    float dt_seconds,
    const MouseResponseProfile& profile) const noexcept {
    MouseNormalizedInput result{};
    if (!mouse_native::valid(profile) || !std::isfinite(dt_seconds) ||
        dt_seconds < config_.minimum_dt_seconds ||
        dt_seconds > config_.maximum_dt_seconds) {
        return result;
    }

    const float x_denominator = profile.counts_per_u_second_x * dt_seconds;
    const float y_denominator = profile.counts_per_u_second_y * dt_seconds;
    if (!std::isfinite(x_denominator) || !std::isfinite(y_denominator) ||
        x_denominator <= 0.0f || y_denominator <= 0.0f) {
        return result;
    }

    result.x = static_cast<float>(source.dx) / x_denominator;
    // Relative mouse Y grows downward while the controller's right-stick Y
    // grows upward.
    result.y = -static_cast<float>(source.dy) / y_denominator;
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
        return {};
    }

    result.valid = true;
    result.envelope_exceeded =
        std::abs(result.x) > config_.maximum_abs_normalized ||
        std::abs(result.y) > config_.maximum_abs_normalized;
    // Never clamp a physical flick into a smaller synthetic stick request.
    // The caller must route the original source counts transparently instead.
    result.transparent = result.envelope_exceeded;
    return result;
}

}  // namespace mouse_native
