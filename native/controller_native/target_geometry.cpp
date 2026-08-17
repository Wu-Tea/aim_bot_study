#include "target_geometry.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

namespace {

bool valid_box(const common_native::Box2f& box) noexcept {
    return std::isfinite(box.x) && std::isfinite(box.y) &&
        std::isfinite(box.w) && std::isfinite(box.h) &&
        box.w > 1.0f && box.h > 1.0f;
}

}  // namespace

StableBodyAimResult StableBodyAimTracker::update(
    common_native::Vec2f resolved_aim_px,
    common_native::Box2f body_box_px,
    bool stabilize_shape_motion,
    bool has_motion_anchor,
    common_native::Vec2f motion_anchor_px) noexcept {
    if (!valid_box(body_box_px) ||
        !std::isfinite(resolved_aim_px.x) ||
        !std::isfinite(resolved_aim_px.y)) {
        reset();
        return {resolved_aim_px, false};
    }
    if (!initialized_) {
        previous_raw_aim_px_ = resolved_aim_px;
        stable_aim_px_ = resolved_aim_px;
        has_previous_motion_anchor_ =
            has_motion_anchor &&
            std::isfinite(motion_anchor_px.x) &&
            std::isfinite(motion_anchor_px.y);
        previous_motion_anchor_px_ = motion_anchor_px;
        last_anchor_delta_px_ = {};
        predicted_motion_since_anchor_px_ = {};
        missing_anchor_frames_ = 0;
        initialized_ = true;
        return {resolved_aim_px, false};
    }
    const bool anchor_valid =
        has_motion_anchor &&
        std::isfinite(motion_anchor_px.x) &&
        std::isfinite(motion_anchor_px.y);
    if (!stabilize_shape_motion) {
        if (anchor_valid && has_previous_motion_anchor_) {
            last_anchor_delta_px_ = {
                motion_anchor_px.x - previous_motion_anchor_px_.x,
                motion_anchor_px.y - previous_motion_anchor_px_.y,
            };
        } else {
            last_anchor_delta_px_ = {};
        }
        previous_raw_aim_px_ = resolved_aim_px;
        stable_aim_px_ = resolved_aim_px;
        previous_motion_anchor_px_ = motion_anchor_px;
        has_previous_motion_anchor_ = anchor_valid;
        predicted_motion_since_anchor_px_ = {};
        missing_anchor_frames_ = 0;
        return {resolved_aim_px, false};
    }
    if (!anchor_valid) {
        if (has_previous_motion_anchor_ && missing_anchor_frames_ < 2) {
            stable_aim_px_.x += last_anchor_delta_px_.x;
            stable_aim_px_.y += last_anchor_delta_px_.y;
            predicted_motion_since_anchor_px_.x +=
                last_anchor_delta_px_.x;
            predicted_motion_since_anchor_px_.y +=
                last_anchor_delta_px_.y;
            ++missing_anchor_frames_;
            previous_raw_aim_px_ = resolved_aim_px;
            return {stable_aim_px_, true};
        }
        previous_raw_aim_px_ = resolved_aim_px;
        stable_aim_px_ = resolved_aim_px;
        has_previous_motion_anchor_ = false;
        predicted_motion_since_anchor_px_ = {};
        missing_anchor_frames_ = 0;
        return {resolved_aim_px, false};
    }
    if (!has_previous_motion_anchor_) {
        previous_raw_aim_px_ = resolved_aim_px;
        stable_aim_px_ = resolved_aim_px;
        previous_motion_anchor_px_ = motion_anchor_px;
        has_previous_motion_anchor_ = true;
        last_anchor_delta_px_ = {};
        predicted_motion_since_anchor_px_ = {};
        missing_anchor_frames_ = 0;
        return {resolved_aim_px, false};
    }

    const common_native::Vec2f raw_delta{
        resolved_aim_px.x - previous_raw_aim_px_.x,
        resolved_aim_px.y - previous_raw_aim_px_.y,
    };
    const common_native::Vec2f total_anchor_delta{
        motion_anchor_px.x - previous_motion_anchor_px_.x,
        motion_anchor_px.y - previous_motion_anchor_px_.y,
    };
    const common_native::Vec2f anchor_correction{
        total_anchor_delta.x - predicted_motion_since_anchor_px_.x,
        total_anchor_delta.y - predicted_motion_since_anchor_px_.y,
    };
    stable_aim_px_.x += anchor_correction.x;
    stable_aim_px_.y += anchor_correction.y;
    const float interval_count =
        static_cast<float>(missing_anchor_frames_ + 1);
    last_anchor_delta_px_ = {
        total_anchor_delta.x / interval_count,
        total_anchor_delta.y / interval_count,
    };
    const bool rejected_shape_motion =
        std::hypot(
            raw_delta.x - anchor_correction.x,
            raw_delta.y - anchor_correction.y) > 0.75f;

    previous_raw_aim_px_ = resolved_aim_px;
    previous_motion_anchor_px_ = motion_anchor_px;
    predicted_motion_since_anchor_px_ = {};
    missing_anchor_frames_ = 0;
    has_previous_motion_anchor_ = true;
    return {stable_aim_px_, rejected_shape_motion};
}

void StableBodyAimTracker::reset() noexcept {
    previous_raw_aim_px_ = {};
    stable_aim_px_ = {};
    previous_motion_anchor_px_ = {};
    last_anchor_delta_px_ = {};
    predicted_motion_since_anchor_px_ = {};
    missing_anchor_frames_ = 0;
    has_previous_motion_anchor_ = false;
    initialized_ = false;
}

TargetGeometryResult resolve_target_geometry(
    const TargetGeometryInput& input,
    const TargetGeometryConfig& config) noexcept {
    const auto& box = input.body_box_px;
    if (!input.has_body_box || box.w <= 0.0f || box.h <= 0.0f) {
        return {input.vision_aim_px, false};
    }

    if (box.h / box.w < 0.65f) {
        return {{
            std::clamp(input.vision_aim_px.x, box.x, box.x + box.w),
            std::clamp(input.vision_aim_px.y, box.y, box.y + box.h)}, true};
    }

    return {{
        input.vision_aim_px.x,
        box.y + box.h * std::clamp(config.aim_height_ratio, 0.0f, 1.0f)}, true};
}

float target_scaled_radius(
    float base_radius_px,
    float normalized_target_size) noexcept {
    return std::max(0.0f, base_radius_px) *
        (1.0f + 0.75f *
            std::clamp(normalized_target_size, 0.0f, 1.0f));
}

}  // namespace controller_native
