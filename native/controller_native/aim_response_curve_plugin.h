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
    // Read-only legacy configuration field. Full-response calibration no longer
    // depends on an arbitrary stick-position anchor.
    float calibration_reference_stick = 0.50f;
    std::array<float, 128> custom_stick{};
    std::array<float, 128> custom_response{};
    std::size_t custom_count = 0;
    // Curve-owned active domain and output range; zero is always neutral.
    float input_start = 0.f, input_end = 0.f;
    float output_start = 0.f, output_end = 0.f;
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
inline float custom_curve_shape(float value, const AimResponseCurveConfig& config, bool inverse) noexcept {
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

inline bool curve_has_window(const AimResponseCurveConfig& config) noexcept {
    return config.input_start != 0.f || config.input_end != 0.f ||
           config.output_start != 0.f || config.output_end != 0.f;
}

inline float custom_curve_magnitude(float value, const AimResponseCurveConfig& config, bool inverse) noexcept {
    if (!curve_has_window(config)) return custom_curve_shape(value, config, inverse);
    if (value <= 0.f || (!inverse && value <= config.input_start)) return 0.f;
    if (inverse) {
        const float normalized = (value-config.output_start)/(1.f-config.output_start-config.output_end);
        return config.input_start + (1.f-config.input_start-config.input_end)*custom_curve_shape(normalized, config, true);
    }
    const float normalized = (value-config.input_start)/(1.f-config.input_start-config.input_end);
    return config.output_start + (1.f-config.output_start-config.output_end)*custom_curve_shape(normalized, config, false);
}

inline float curve_forward_magnitude(float value, const AimResponseCurveConfig& config) noexcept {
    return config.algorithm == AimResponseCurveAlgorithm::CustomLut ? custom_curve_magnitude(value, config, false) :
        resolve_aim_response_curve_plugin(config.algorithm).forward_magnitude(value);
}

inline float curve_inverse_magnitude(float value, const AimResponseCurveConfig& config) noexcept {
    return config.algorithm == AimResponseCurveAlgorithm::CustomLut ? custom_curve_magnitude(value, config, true) :
        resolve_aim_response_curve_plugin(config.algorithm).inverse_magnitude(value);
}

// UI percentages and camera demand share full-scale normalized response.
// Apply the radial curve without a local calibration anchor.
inline pipeline_contract::Vec2f normalized_aim_response_curve(
    pipeline_contract::Vec2f value, const AimResponseCurveConfig& config,
    bool inverse = false) noexcept {
    const float radius = std::hypot(value.x, value.y);
    if (radius == 0.f || config.algorithm == AimResponseCurveAlgorithm::Linear) return value;
    const float mapped = inverse ? curve_inverse_magnitude(radius, config)
                                 : curve_forward_magnitude(radius, config);
    const float scale = mapped / radius;
    return {value.x * scale, value.y * scale};
}

// Camera demand and learning both use full-scale normalized response. The
// same px/s calibration therefore has the same units for every curve.
inline pipeline_contract::Vec2f inverse_aim_response_curve(
    pipeline_contract::Vec2f response, const AimResponseCurveConfig& config) noexcept {
    return normalized_aim_response_curve(response, config, true);
}

inline pipeline_contract::Vec2f forward_aim_response_curve(
    pipeline_contract::Vec2f stick, const AimResponseCurveConfig& config) noexcept {
    return normalized_aim_response_curve(stick, config);
}

} // namespace controller_native
