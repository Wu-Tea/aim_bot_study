#pragma once

#include "pipeline_contract/vision_observation.h"
#include "curve_library_data.h"

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
    CustomLut,
};

struct AimResponseCurveConfig {
    AimResponseCurveAlgorithm algorithm = AimResponseCurveAlgorithm::Linear;
    // The scalar response learner is local. Anchor the nonlinear shape here so
    // enabling a plugin does not silently replace the learned sensitivity.
    float calibration_reference_stick = 0.50f;
    std::array<float, 128> custom_stick{};
    std::array<float, 128> custom_response{};
    std::size_t custom_count = 0;
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

inline constexpr auto& kCodDynamicLegacyStick = curve_library_data::cod_dynamic_legacy_0;
inline constexpr auto& kCodDynamicLegacyResponse = curve_library_data::cod_dynamic_legacy_1;

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
        case AimResponseCurveAlgorithm::CustomLut:
            return "custom_lut";
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
    if (name == "custom_lut") {
        algorithm = AimResponseCurveAlgorithm::CustomLut;
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

// Config loading validates endpoints and strict monotonicity before tick use.
inline float custom_curve_magnitude(float value, const AimResponseCurveConfig& config, bool inverse) noexcept {
    const auto& domain = inverse ? config.custom_response : config.custom_stick;
    const auto& range = inverse ? config.custom_stick : config.custom_response;
    value = std::clamp(value, 0.0f, 1.0f);
    for (std::size_t i = 1; i < config.custom_count; ++i) {
        if (value <= domain[i]) {
            const float t = (value - domain[i - 1]) / (domain[i] - domain[i - 1]);
            return range[i - 1] + t * (range[i] - range[i - 1]);
        }
    }
    return 1.0f;
}

inline float curve_forward_magnitude(float value, const AimResponseCurveConfig& config) noexcept {
    return config.algorithm == AimResponseCurveAlgorithm::CustomLut ? custom_curve_magnitude(value, config, false) :
        resolve_aim_response_curve_plugin(config.algorithm).forward_magnitude(value);
}

inline float curve_inverse_magnitude(float value, const AimResponseCurveConfig& config) noexcept {
    return config.algorithm == AimResponseCurveAlgorithm::CustomLut ? custom_curve_magnitude(value, config, true) :
        resolve_aim_response_curve_plugin(config.algorithm).inverse_magnitude(value);
}

inline pipeline_contract::Vec2f inverse_aim_response_curve(
    pipeline_contract::Vec2f linear_target,
    const AimResponseCurveConfig& config) noexcept {
    const float input_magnitude = std::hypot(linear_target.x, linear_target.y);
    if (input_magnitude <= 1.0e-7f ||
        config.algorithm == AimResponseCurveAlgorithm::Linear) {
        return linear_target;
    }

    const float reference = std::clamp(
        std::isfinite(config.calibration_reference_stick)
            ? config.calibration_reference_stick
            : 0.50f,
        0.05f,
        1.0f);
    const float reference_response = curve_forward_magnitude(reference, config);
    const float desired_response = std::clamp(
        reference_response * input_magnitude / reference, 0.0f, 1.0f);
    const float output_magnitude = curve_inverse_magnitude(desired_response, config);
    const float scale = output_magnitude / input_magnitude;
    return {linear_target.x * scale, linear_target.y * scale};
}

// Converts the delivered virtual-stick target back into the normalized
// camera-response space used by the controller.  This is the exact inverse of
// inverse_aim_response_curve within the plugin's unclipped range. Runtime
// response learning and simulation both use it so a configured nonlinear
// response curve is never measured as though it were linear.
inline pipeline_contract::Vec2f forward_aim_response_curve(
    pipeline_contract::Vec2f delivered_stick,
    const AimResponseCurveConfig& config) noexcept {
    const float stick_magnitude = std::hypot(
        delivered_stick.x, delivered_stick.y);
    if (stick_magnitude <= 1.0e-7f ||
        config.algorithm == AimResponseCurveAlgorithm::Linear) {
        return delivered_stick;
    }

    const float reference = std::clamp(
        std::isfinite(config.calibration_reference_stick)
            ? config.calibration_reference_stick
            : 0.50f,
        0.05f,
        1.0f);
    const float reference_response = curve_forward_magnitude(reference, config);
    if (!std::isfinite(reference_response) ||
        reference_response <= 1.0e-7f) {
        return delivered_stick;
    }
    const float response = curve_forward_magnitude(
        std::clamp(stick_magnitude, 0.0f, 1.0f), config);
    const float linear_magnitude = reference * response / reference_response;
    const float scale = linear_magnitude / stick_magnitude;
    return {delivered_stick.x * scale, delivered_stick.y * scale};
}

}  // namespace controller_native
