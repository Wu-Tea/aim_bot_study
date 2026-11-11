#pragma once

#include "../pipeline_contract/control_command.h"
#include "cod_linear_reference_curve.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

enum class GameStickResponseModel { Power, CodLinearLegacyLut, CustomLut, OutputLut };

inline const char* game_stick_response_model_name(GameStickResponseModel model) noexcept {
    return model == GameStickResponseModel::OutputLut ? "output_lut" : model == GameStickResponseModel::CustomLut ? "custom_lut" : model == GameStickResponseModel::CodLinearLegacyLut ? "cod_linear_legacy_lut" : "power";
}

// A game/output boundary, separate from AI shaping and physical intent.
// Deadzone-normalized wire magnitude goes through either a power function or
// the historical COD Linear LUT. Neither model claims a current MW4 calibration.
struct GameStickTransferConfig {
    bool enabled = false;
    bool axial = false;
    float deadzone = 0.16f;
    float game_exponent = 1.0f;
    GameStickResponseModel response_model = GameStickResponseModel::Power;
    AimResponseCurveConfig curve;
    // Fraction of the outer travel where the game already produces full speed.
    float outer_deadzone = 0.0f;
};

inline float game_stick_magnitude(float magnitude,
                                 const GameStickTransferConfig& config,
                                 bool decode) noexcept {
    if (magnitude == 0.0f) return 0.0f;
    if (magnitude >= 1.0f && config.outer_deadzone == 0.0f && !curve_has_window(config.curve)) return magnitude;
    const float span = 1.0f - config.deadzone - config.outer_deadzone;
    // A prepared input/output mapping is already C, not a game model A.
    // The AI model remains independently selected in aim_response_curve.
    if (config.response_model == GameStickResponseModel::OutputLut) {
        if (decode) {
            const float normalized = std::clamp((magnitude - config.deadzone) / span, 0.0f, 1.0f);
            return custom_curve_magnitude(normalized, config.curve, true);
        }
        const float mapped = custom_curve_magnitude(magnitude, config.curve, false);
        return config.deadzone + span * mapped;
    }
    if (decode) {
        const float normalized = std::clamp((magnitude - config.deadzone) / span, 0.0f, 1.0f);
        if (config.response_model == GameStickResponseModel::CustomLut)
            return custom_curve_magnitude(normalized, config.curve, false);
        if (config.response_model == GameStickResponseModel::CodLinearLegacyLut)
            return cod_linear_reference_magnitude(normalized, true);
        return config.game_exponent == 1.0f ? normalized
            : std::pow(normalized, config.game_exponent);
    }
    const float linearized = config.response_model == GameStickResponseModel::CustomLut
        ? custom_curve_magnitude(magnitude, config.curve, true)
        : config.response_model == GameStickResponseModel::CodLinearLegacyLut
        ? cod_linear_reference_magnitude(magnitude, false)
        : config.game_exponent == 1.0f ? magnitude
            : std::pow(magnitude, 1.0f / config.game_exponent);
    return config.deadzone + span * linearized;
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
    if (radius == 0.0f) return stick;
    // Keep square-report directions/corners. The outer rim scales with the
    // configured full-speed threshold; undo that scale in delivered feedback.
    const float outer = 1.0f - config.outer_deadzone -
        (1.0f-config.deadzone-config.outer_deadzone)*config.curve.output_end;
    if (config.curve.input_end == 0.f && radius >= (decode ? outer : 1.0f)) {
        if (outer == 1.0f) return stick;
        const float scale = decode ? 1.0f / outer : outer;
        return {stick.x * scale, stick.y * scale};
    }
    const float scale = game_stick_magnitude(radius, config, decode) / radius;
    return {stick.x * scale, stick.y * scale};
}

} // namespace controller_native
