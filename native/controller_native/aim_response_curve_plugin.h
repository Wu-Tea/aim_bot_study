#pragma once

#include "pipeline_contract/vision_observation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace controller_native {

// Converts desired normalized camera response into the final virtual-stick
// target T. Manual input is deliberately absent: residual allocation remains
// A = T - M downstream.
enum class AimResponseCurveAlgorithm : unsigned char {
    Linear,
    CodDynamicLegacyLut,
};

struct AimResponseCurveConfig {
    AimResponseCurveAlgorithm algorithm = AimResponseCurveAlgorithm::Linear;
    // The scalar response learner is local. Anchor the nonlinear shape here so
    // enabling a plugin does not silently replace the learned sensitivity.
    float calibration_reference_stick = 0.50f;
};

struct AimResponseCurvePlugin {
    AimResponseCurveAlgorithm algorithm = AimResponseCurveAlgorithm::Linear;
    const char* name = "linear";
    float (*forward_magnitude)(float) noexcept = nullptr;
    float (*inverse_magnitude)(float) noexcept = nullptr;
};

namespace aim_response_curve_detail {

inline float linear_magnitude(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

inline constexpr std::array<float, 20> kCodDynamicLegacyStick{
    0.00000f, 0.10152f, 0.15032f, 0.20134f, 0.24929f,
    0.30004f, 0.35003f, 0.40038f, 0.45165f, 0.50182f,
    0.54907f, 0.59924f, 0.65057f, 0.70370f, 0.75021f,
    0.80227f, 0.85135f, 0.90277f, 0.94913f, 1.00000f,
};

// Normalized camera rate for the versioned legacy COD Dynamic seed curve.
inline constexpr std::array<float, 20> kCodDynamicLegacyResponse{
    0.000000000f, 0.012697267f, 0.025216655f, 0.042092669f,
    0.061012139f, 0.084497817f, 0.111752815f, 0.141086402f,
    0.176631675f, 0.211591033f, 0.253604194f, 0.300000000f,
    0.344000000f, 0.399586990f, 0.459619953f, 0.525815217f,
    0.611374408f, 0.710091743f, 0.844978166f, 1.000000000f,
};

template <std::size_t N>
inline float interpolate_monotonic(
    float value,
    const std::array<float, N>& domain,
    const std::array<float, N>& range) noexcept {
    const float bounded = std::clamp(value, domain.front(), domain.back());
    for (std::size_t index = 1; index < N; ++index) {
        if (bounded <= domain[index]) {
            const float width = domain[index] - domain[index - 1];
            if (width <= 1.0e-7f) return range[index];
            const float t = (bounded - domain[index - 1]) / width;
            return range[index - 1] + t * (range[index] - range[index - 1]);
        }
    }
    return range.back();
}

inline float cod_dynamic_legacy_forward(float stick) noexcept {
    return interpolate_monotonic(
        stick, kCodDynamicLegacyStick, kCodDynamicLegacyResponse);
}

inline float cod_dynamic_legacy_inverse(float response) noexcept {
    return interpolate_monotonic(
        response, kCodDynamicLegacyResponse, kCodDynamicLegacyStick);
}

inline constexpr AimResponseCurvePlugin kLinearPlugin{
    AimResponseCurveAlgorithm::Linear,
    "linear",
    &linear_magnitude,
    &linear_magnitude,
};

inline constexpr AimResponseCurvePlugin kCodDynamicLegacyPlugin{
    AimResponseCurveAlgorithm::CodDynamicLegacyLut,
    "cod_dynamic_legacy_lut",
    &cod_dynamic_legacy_forward,
    &cod_dynamic_legacy_inverse,
};

}  // namespace aim_response_curve_detail

inline const char* aim_response_curve_algorithm_name(
    AimResponseCurveAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case AimResponseCurveAlgorithm::CodDynamicLegacyLut:
            return "cod_dynamic_legacy_lut";
        case AimResponseCurveAlgorithm::Linear:
        default:
            return "linear";
    }
}

inline bool try_parse_aim_response_curve_algorithm(
    std::string_view name,
    AimResponseCurveAlgorithm& algorithm) noexcept {
    if (name == "linear") {
        algorithm = AimResponseCurveAlgorithm::Linear;
        return true;
    }
    if (name == "cod_dynamic_legacy_lut") {
        algorithm = AimResponseCurveAlgorithm::CodDynamicLegacyLut;
        return true;
    }
    return false;
}

inline const AimResponseCurvePlugin& resolve_aim_response_curve_plugin(
    AimResponseCurveAlgorithm algorithm) noexcept {
    using namespace aim_response_curve_detail;
    switch (algorithm) {
        case AimResponseCurveAlgorithm::CodDynamicLegacyLut:
            return kCodDynamicLegacyPlugin;
        case AimResponseCurveAlgorithm::Linear:
        default:
            return kLinearPlugin;
    }
}

inline pipeline_contract::Vec2f inverse_aim_response_curve(
    pipeline_contract::Vec2f linear_target,
    const AimResponseCurveConfig& config) noexcept {
    const float input_magnitude = std::hypot(linear_target.x, linear_target.y);
    if (input_magnitude <= 1.0e-7f ||
        config.algorithm == AimResponseCurveAlgorithm::Linear) {
        return linear_target;
    }

    const AimResponseCurvePlugin& plugin =
        resolve_aim_response_curve_plugin(config.algorithm);
    const float reference = std::clamp(
        std::isfinite(config.calibration_reference_stick)
            ? config.calibration_reference_stick
            : 0.50f,
        0.05f,
        1.0f);
    const float reference_response = plugin.forward_magnitude(reference);
    const float desired_response = std::clamp(
        reference_response * input_magnitude / reference, 0.0f, 1.0f);
    const float output_magnitude = plugin.inverse_magnitude(desired_response);
    const float scale = output_magnitude / input_magnitude;
    return {linear_target.x * scale, linear_target.y * scale};
}

// Converts the delivered virtual-stick target back into the normalized
// camera-response space used by the controller.  This is the exact inverse of
// inverse_aim_response_curve within the plugin's unclipped range. The current
// production-only simulator uses it to model what the game receives instead
// of treating every configured response curve as linear.
inline pipeline_contract::Vec2f forward_aim_response_curve(
    pipeline_contract::Vec2f delivered_stick,
    const AimResponseCurveConfig& config) noexcept {
    const float stick_magnitude = std::hypot(
        delivered_stick.x, delivered_stick.y);
    if (stick_magnitude <= 1.0e-7f ||
        config.algorithm == AimResponseCurveAlgorithm::Linear) {
        return delivered_stick;
    }

    const AimResponseCurvePlugin& plugin =
        resolve_aim_response_curve_plugin(config.algorithm);
    const float reference = std::clamp(
        std::isfinite(config.calibration_reference_stick)
            ? config.calibration_reference_stick
            : 0.50f,
        0.05f,
        1.0f);
    const float reference_response = plugin.forward_magnitude(reference);
    if (!std::isfinite(reference_response) ||
        reference_response <= 1.0e-7f) {
        return delivered_stick;
    }
    const float response = plugin.forward_magnitude(
        std::clamp(stick_magnitude, 0.0f, 1.0f));
    const float linear_magnitude = reference * response / reference_response;
    const float scale = linear_magnitude / stick_magnitude;
    return {delivered_stick.x * scale, delivered_stick.y * scale};
}

}  // namespace controller_native
