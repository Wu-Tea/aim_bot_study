#pragma once

#include "vision_native/types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace vision_native {
namespace color_detail {

template <typename FrameView>
inline void read_rgb(
    const FrameView& frame,
    int x,
    int y,
    int& r,
    int& g,
    int& b) {
    const uint8_t* pixel = frame.data + (static_cast<size_t>(y) * static_cast<size_t>(frame.row_pitch));
    if (frame.format == PixelFormat::BGRA8) {
        pixel += static_cast<size_t>(x) * 4;
        b = pixel[0];
        g = pixel[1];
        r = pixel[2];
        return;
    }

    pixel += static_cast<size_t>(x) * 3;
    r = pixel[0];
    g = pixel[1];
    b = pixel[2];
}

inline void rgb_to_opencv_hsv(int r, int g, int b, float& h, float& s, float& v) {
    const float rf = static_cast<float>(r) / 255.0f;
    const float gf = static_cast<float>(g) / 255.0f;
    const float bf = static_cast<float>(b) / 255.0f;
    const float max_value = std::max(rf, std::max(gf, bf));
    const float min_value = std::min(rf, std::min(gf, bf));
    const float delta = max_value - min_value;

    float hue_degrees = 0.0f;
    if (delta > 0.0f) {
        if (max_value == rf) {
            hue_degrees = 60.0f * std::fmod(((gf - bf) / delta), 6.0f);
        } else if (max_value == gf) {
            hue_degrees = 60.0f * (((bf - rf) / delta) + 2.0f);
        } else {
            hue_degrees = 60.0f * (((rf - gf) / delta) + 4.0f);
        }
    }
    if (hue_degrees < 0.0f) {
        hue_degrees += 360.0f;
    }

    h = hue_degrees * 0.5f;
    s = max_value <= 0.0f ? 0.0f : (delta / max_value) * 255.0f;
    v = max_value * 255.0f;
}

inline bool hsv_in_range(
    float h,
    float s,
    float v,
    float lower_h,
    float lower_s,
    float lower_v,
    float upper_h,
    float upper_s,
    float upper_v) {
    return lower_h <= h && h <= upper_h
        && lower_s <= s && s <= upper_s
        && lower_v <= v && v <= upper_v;
}

} // namespace color_detail
} // namespace vision_native
