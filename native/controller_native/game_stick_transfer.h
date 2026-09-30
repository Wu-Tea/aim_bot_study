#pragma once

#include "../pipeline_contract/control_command.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

// A game/output boundary, separate from AI shaping and physical intent.
// Model: camera magnitude = ((wire - deadzone) / (1 - deadzone))^exponent.
// Parameters are a user calibration, not a claim about a particular game.
struct GameStickTransferConfig {
    bool enabled = false;
    bool axial = false;
    float deadzone = 0.16f;
    float game_exponent = 1.0f;
};

inline float game_stick_magnitude(float magnitude,
                                 const GameStickTransferConfig& config,
                                 bool decode) noexcept {
    if (magnitude == 0.0f || magnitude >= 1.0f) return magnitude;
    if (decode) {
        const float normalized = std::max(0.0f,
            (magnitude - config.deadzone) / (1.0f - config.deadzone));
        return config.game_exponent == 1.0f ? normalized
            : std::pow(normalized, config.game_exponent);
    }
    const float linearized = config.game_exponent == 1.0f ? magnitude
        : std::pow(magnitude, 1.0f / config.game_exponent);
    return config.deadzone + (1.0f - config.deadzone) * linearized;
}

inline pipeline_contract::Vec2f transfer_game_stick(
    pipeline_contract::Vec2f stick, const GameStickTransferConfig& config,
    bool decode = false) noexcept {
    if (!config.enabled) return stick; // Exact legacy passthrough.
    if (!std::isfinite(stick.x) || !std::isfinite(stick.y)) return {};
    stick.x = std::clamp(stick.x, -1.0f, 1.0f);
    stick.y = std::clamp(stick.y, -1.0f, 1.0f);
    if (config.axial) {
        return {std::copysign(game_stick_magnitude(std::fabs(stick.x), config, decode), stick.x),
                std::copysign(game_stick_magnitude(std::fabs(stick.y), config, decode), stick.y)};
    }
    const float radius = std::hypot(stick.x, stick.y);
    // Preserve the square report's outer rim/corners and exact center. Inside
    // the unit circle, preserve direction (no new axial deadzone or snapping).
    if (radius == 0.0f || radius >= 1.0f) return stick;
    const float scale = game_stick_magnitude(radius, config, decode) / radius;
    return {stick.x * scale, stick.y * scale};
}

} // namespace controller_native
