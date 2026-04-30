#include "vision_native/center_cue_refiner.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vision_native {
namespace {

constexpr int kCenterWindowSize = 250;
constexpr int kMinYellowPixels = 40;
constexpr float kMinYellowRatio = 0.0025f;
constexpr float kMaxCueTargetDistance = 72.0f;
constexpr float kRefineAlphaX = 0.18f;
constexpr float kRefineAlphaY = 0.58f;
constexpr float kProjectedTorsoYOffsetRatio = 0.18f;

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

float clampf(float value, float lower, float upper) {
    return std::max(lower, std::min(upper, value));
}

bool rect_valid(const RectF& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

RectF clamp_rect(RectF rect, int width, int height) {
    rect.left = clampf(rect.left, 0.0f, static_cast<float>(width));
    rect.right = clampf(rect.right, 0.0f, static_cast<float>(width));
    rect.top = clampf(rect.top, 0.0f, static_cast<float>(height));
    rect.bottom = clampf(rect.bottom, 0.0f, static_cast<float>(height));
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
    return rect;
}

float rect_height(const RectF& rect) {
    return rect.bottom - rect.top;
}

float point_distance(float x1, float y1, float x2, float y2) {
    return std::hypot(x1 - x2, y1 - y2);
}

RectF upper_body_rect(
    float body_x1,
    float body_y1,
    float body_x2,
    float body_y2,
    float torso_x1,
    float torso_y1,
    float torso_x2,
    float torso_y2) {
    RectF torso{torso_x1, torso_y1, torso_x2, torso_y2};
    if (rect_valid(torso)) {
        return torso;
    }

    RectF body{body_x1, body_y1, body_x2, body_y2};
    if (!rect_valid(body)) {
        return {};
    }

    const float height = rect_height(body);
    return {
        body.left + ((body.right - body.left) * 0.22f),
        body.top + (height * 0.18f),
        body.right - ((body.right - body.left) * 0.22f),
        body.bottom - (height * 0.20f),
    };
}

void read_rgb(
    const CenterCueFrameView& frame,
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

void rgb_to_opencv_hsv(int r, int g, int b, float& h, float& s, float& v) {
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

bool is_yellow_hsv(float h, float s, float v) {
    return 18.0f <= h && h <= 42.0f
        && 120.0f <= s && s <= 255.0f
        && 120.0f <= v && v <= 255.0f;
}

} // namespace

CenterCueRefiner::CenterCueRefiner(int width, int height)
    : width_(width),
      height_(height) {}

void CenterCueRefiner::reset() {}

CenterCueResult CenterCueRefiner::refine(
    const CenterCueFrameView& frame,
    float target_x,
    float target_y,
    float screen_center_x,
    float screen_center_y,
    float body_x1,
    float body_y1,
    float body_x2,
    float body_y2,
    float torso_x1,
    float torso_y1,
    float torso_x2,
    float torso_y2,
    const char* body_state_mode) const {
    CenterCueResult result;
    result.refined_target_x = target_x;
    result.refined_target_y = target_y;

    const RectF center_window = clamp_rect(
        {
            screen_center_x - (kCenterWindowSize * 0.5f),
            screen_center_y - (kCenterWindowSize * 0.5f),
            screen_center_x + (kCenterWindowSize * 0.5f),
            screen_center_y + (kCenterWindowSize * 0.5f),
        },
        width_,
        height_);
    result.yellow_roi_x1 = center_window.left;
    result.yellow_roi_y1 = center_window.top;
    result.yellow_roi_x2 = center_window.right;
    result.yellow_roi_y2 = center_window.bottom;

    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return result;
    }
    if (body_state_mode != nullptr && std::string(body_state_mode) == "drop") {
        return result;
    }
    if (target_x < center_window.left || target_x > center_window.right
        || target_y < center_window.top || target_y > center_window.bottom) {
        return result;
    }

    int yellow_count = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (int y = static_cast<int>(std::floor(center_window.top)); y < static_cast<int>(std::ceil(center_window.bottom)); ++y) {
        for (int x = static_cast<int>(std::floor(center_window.left)); x < static_cast<int>(std::ceil(center_window.right)); ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            read_rgb(frame, x, y, r, g, b);
            float h = 0.0f;
            float s = 0.0f;
            float v = 0.0f;
            rgb_to_opencv_hsv(r, g, b, h, s, v);
            if (!is_yellow_hsv(h, s, v)) {
                continue;
            }
            ++yellow_count;
            sum_x += static_cast<double>(x);
            sum_y += static_cast<double>(y);
        }
    }

    const float roi_area = std::max(1.0f, (center_window.right - center_window.left) * (center_window.bottom - center_window.top));
    result.yellow_mask_area = static_cast<float>(yellow_count);
    if (yellow_count < kMinYellowPixels || (static_cast<float>(yellow_count) / roi_area) < kMinYellowRatio) {
        return result;
    }

    result.yellow_cue_present = true;
    result.yellow_cue_x = static_cast<float>(sum_x / static_cast<double>(yellow_count));
    result.yellow_cue_y = static_cast<float>(sum_y / static_cast<double>(yellow_count));
    result.yellow_cue_score = clampf(static_cast<float>(yellow_count) / 220.0f, 0.0f, 1.0f);

    if (point_distance(result.yellow_cue_x, result.yellow_cue_y, target_x, target_y) > kMaxCueTargetDistance) {
        return result;
    }

    RectF clamp_zone = upper_body_rect(body_x1, body_y1, body_x2, body_y2, torso_x1, torso_y1, torso_x2, torso_y2);
    if (!rect_valid(clamp_zone)) {
        return result;
    }
    clamp_zone = clamp_rect(clamp_zone, width_, height_);
    if (result.yellow_cue_y > (clamp_zone.bottom + 8.0f)) {
        return result;
    }

    const float projected_x = clampf(result.yellow_cue_x, clamp_zone.left, clamp_zone.right);
    const float projected_y = clampf(
        result.yellow_cue_y + (rect_height(clamp_zone) * kProjectedTorsoYOffsetRatio),
        clamp_zone.top,
        clamp_zone.bottom);

    result.refined_target_x = clampf(
        target_x + ((projected_x - target_x) * kRefineAlphaX),
        clamp_zone.left,
        clamp_zone.right);
    result.refined_target_y = clampf(
        target_y + ((projected_y - target_y) * kRefineAlphaY),
        clamp_zone.top,
        clamp_zone.bottom);
    result.refiner_applied = true;
    return result;
}

} // namespace vision_native
