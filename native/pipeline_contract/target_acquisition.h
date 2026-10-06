#pragma once

#include <algorithm>
#include <cmath>

namespace pipeline_contract {

inline constexpr float kPickupSizeGain = 0.75f;
inline constexpr float kWideLowAspectThreshold = 0.65f;
inline constexpr float kAimRegionShrinkX = 0.22f;
inline constexpr float kAimRegionHalfHeightRatio = 0.18f;
inline constexpr float kWideLowAimRegionHalfHeightRatio = 0.22f;
inline constexpr float kMinPickupHeightRatio = 0.08f;
inline constexpr float kMinTrackingHeightRatio = 0.06f;
inline constexpr float kMinPickupAreaRatio = 0.003f;
inline constexpr float kMinTrackingAreaRatio = 0.002f;
inline constexpr float kMinAspectRatio = 0.85f;
inline constexpr float kMinWideLowAspectRatio = 0.30f;
inline constexpr float kMaxAspectRatio = 4.50f;

inline bool wide_low_body_shape(float width, float height) noexcept {
    return (width > 0 ? height / width : 0.0f) < kWideLowAspectThreshold;
}

inline bool body_geometry_admitted(float width, float height, int frame_width, int frame_height,
                                   bool tracking_candidate) noexcept {
    const float aspect = width > 0 ? height / width : 0.0f;
    const float min_aspect = wide_low_body_shape(width, height) ? kMinWideLowAspectRatio : kMinAspectRatio;
    if (aspect < min_aspect || aspect > kMaxAspectRatio) return false;
    const float min_height = frame_height * (tracking_candidate ? kMinTrackingHeightRatio : kMinPickupHeightRatio);
    const float min_area = (frame_width * frame_height) * (tracking_candidate ? kMinTrackingAreaRatio : kMinPickupAreaRatio);
    return height >= min_height && width * height >= min_area;
}

struct BodyAimGeometry {
    float aim_x = 0, aim_y = 0;
    float left = 0, top = 0, right = 0, bottom = 0;
    bool wide_low = false;
};

// Selector and configuration preview share one fallback body-geometry owner.
// It describes box geometry, not a skeletal pose classifier or fire authority.
inline BodyAimGeometry body_aim_geometry(float left, float top, float right, float bottom,
                                         float ordinary_ratio, float wide_low_ratio) noexcept {
    const float width = right - left, height = bottom - top;
    BodyAimGeometry result;
    result.wide_low = wide_low_body_shape(width, height);
    result.aim_x = (left + right) * .5f;
    result.aim_y = top + height * (result.wide_low ? wide_low_ratio : ordinary_ratio);
    const float half = height * (result.wide_low ? kWideLowAimRegionHalfHeightRatio : kAimRegionHalfHeightRatio);
    result.left = left + width * kAimRegionShrinkX;
    result.right = right - width * kAimRegionShrinkX;
    result.top = std::max(top, result.aim_y - half);
    result.bottom = std::min(bottom, result.aim_y + half);
    return result;
}

// New target identity uses one geometry rule across Vision selection and
// Controller telemetry. The base radius describes a small/far target. Apparent
// body height widens the envelope smoothly so a close person can be acquired
// farther from the reticle without granting the same reach to a distant one.
inline float target_scaled_pickup_radius(
    float base_radius_px,
    float normalized_target_size) noexcept {
    const float safe_base = std::isfinite(base_radius_px)
        ? std::max(0.0f, base_radius_px)
        : 0.0f;
    const float safe_size = std::isfinite(normalized_target_size)
        ? std::clamp(normalized_target_size, 0.0f, 1.0f)
        : 0.0f;
    return safe_base * (1.0f + kPickupSizeGain * safe_size);
}

}  // namespace pipeline_contract
