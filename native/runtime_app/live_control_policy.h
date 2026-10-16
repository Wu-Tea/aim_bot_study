#pragma once

#include "controller_native/runtime_config.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {

inline constexpr float kLiveVisualLifetimeMs = 40.0f;

inline float live_visual_lifetime_ms(float requested) noexcept {
    return std::isfinite(requested)
        ? std::clamp(requested, 0.0f, kLiveVisualLifetimeMs) : 0.0f;
}

// Live execution owns this ceiling, including profiles written before it was
// introduced. Offline model experiments may retain their configured horizons.
inline controller_native::RuntimeConfig live_control_config(
    controller_native::RuntimeConfig config) {
    // The native live I/O loop must never execute capture/inference inline.
    // Retain the old configuration field for profile compatibility only.
    if (!config.vision.gpu_service_enabled) {
        config.vision.gpu_service_enabled = true;
        config.effective_sources["runtime.vision.gpu_service_enabled"] = "live-policy";
    }
    const auto cap_lifetime = [&config](float& value, const char* key) {
        const float capped = live_visual_lifetime_ms(value);
        if (value != capped) {
            value = capped;
            config.effective_sources[key] = "live-policy";
        }
    };
    cap_lifetime(config.gamepad.tracker.max_observation_age_ms,
                 "gamepad.tracker.max_observation_age_ms");
    cap_lifetime(config.gamepad.auto_fire.max_source_age_ms,
                 "gamepad.auto_fire.max_source_age_ms");
    return config;
}

} // namespace runtime_app
