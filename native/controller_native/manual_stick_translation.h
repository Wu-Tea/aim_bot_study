#pragma once

#include "runtime_config.h"

namespace controller_native {

// Physical intent always consumes the raw sample. Prepared mappings use a
// virtual command domain shared with the independently selected AI model.
// Older profiles retain their original input-shaping semantics.
inline pipeline_contract::Vec2f translate_manual_stick(
    pipeline_contract::Vec2f stick, const GamepadRuntimeConfig& config) noexcept {
    if (config.output_transfer.enabled && config.output_transfer.response_model == GameStickResponseModel::OutputLut &&
        !config.ai_aim.adapter_direct_mouse_manual) {
        // C maps both virtual manual and AI commands at the output boundary.
        // Opting manual input out converts raw wire coordinates back into the
        // virtual domain first, preserving exact passthrough after C.
        return config.follow_response_curve ? stick : transfer_game_stick(stick, config.output_transfer, true);
    }
    if (!config.follow_response_curve || config.ai_aim.adapter_direct_mouse_manual ||
        config.aim_response_curve.algorithm == AimResponseCurveAlgorithm::Linear)
        return stick;
    if (!std::isfinite(stick.x) || !std::isfinite(stick.y)) return {};
    const float radius = std::hypot(stick.x, stick.y);
    // Keep exact center and the square report's outer rim/corners, just as
    // the existing output transfer does. Inside the circle retain direction.
    if (radius == 0.0f || radius >= 1.0f) return stick;
    const float scale = curve_forward_magnitude(radius, config.aim_response_curve) / radius;
    return {stick.x * scale, stick.y * scale};
}

// Prepared output mapping C never replaces the independently selected AI
// response B. Only legacy input-shaping profiles assume a linear solver.
inline const AimResponseCurveConfig& controller_response_curve(
    const GamepadRuntimeConfig& config) noexcept {
    static const AimResponseCurveConfig linear{};
    return config.follow_response_curve && !config.ai_aim.adapter_direct_mouse_manual &&
        config.output_transfer.response_model != GameStickResponseModel::OutputLut
        ? linear : config.aim_response_curve;
}

} // namespace controller_native
