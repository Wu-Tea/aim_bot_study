#pragma once

#include "mouse_native/mouse_control_types.h"
#include <cmath>

namespace mouse_native {

struct MouseCodDefaultConfig {
    bool enabled = true;
    float dpi = 1200.0f;
    float sensitivity = 5.0f;
    float horizontal_fov_16_9 = 104.0f;
    float ads_multiplier = 1.0f;
    // Initial assumption only; the runtime supplies DXGI output height before
    // delivering the first target. A cropped ROI is not the full game view.
    int view_height_px = 1080;
};

// Converter reference: https://sensconverter.app/cod-sensitivity-converter/
// COD's sensitivity unit is 0.0066 degrees/count at sensitivity 1.
constexpr double kCodYawDegreesPerCount = 0.0066;

inline double cod_cm_per_360(const MouseCodDefaultConfig& config) noexcept {
    return 360.0 * 2.54 / (config.dpi * config.sensitivity * kCodYawDegreesPerCount);
}

inline MouseResponseProfile make_cod_default_profile(
    const MouseCodDefaultConfig& config, MouseAimMode mode,
    std::uint64_t generation = (std::uint64_t{1} << 63)) noexcept {
    if (!config.enabled || !std::isfinite(config.dpi) || config.dpi <= 0 ||
        !std::isfinite(config.sensitivity) || config.sensitivity <= 0 ||
        !std::isfinite(config.horizontal_fov_16_9) || config.horizontal_fov_16_9 <= 0 ||
        config.horizontal_fov_16_9 >= 180 || !std::isfinite(config.ads_multiplier) ||
        config.ads_multiplier <= 0 || config.view_height_px <= 0) return {};

    constexpr double radians_per_degree = 3.14159265358979323846 / 180.0;
    // FOV is COD's horizontal angle at 16:9. Preserve its vertical FOV on
    // other aspect ratios. Vision detections are in native capture ROI pixels.
    const double focal_px = config.view_height_px * (16.0 / 9.0) /
        (2.0 * std::tan(config.horizontal_fov_16_9 * radians_per_degree / 2.0));
    // Raw Input and SendInput already use counts: never multiply DPI again.
    // ADS uses the same near-centre screen response times the user multiplier
    // as an estimate; scope/FOV/MDC-specific response remains calibratable.
    const double px_per_count = focal_px * kCodYawDegreesPerCount *
        config.sensitivity * radians_per_degree *
        (mode == MouseAimMode::Ads ? config.ads_multiplier : 1.0);
    MouseResponseProfile profile{};
    profile.px_per_count_x = profile.px_per_count_y = static_cast<float>(px_per_count);
    profile.counts_per_u_second_x = profile.counts_per_u_second_y =
        static_cast<float>(500.0 / px_per_count);
    profile.generation = generation;
    profile.estimated = true;
    // calibrated stays false and measured confidence stays zero.
    return valid(profile) ? profile : MouseResponseProfile{};
}

}  // namespace mouse_native
