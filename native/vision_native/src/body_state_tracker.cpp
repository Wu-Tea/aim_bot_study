#include "vision_native/body_state_tracker.h"

#include "image_ops.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vision_native {
namespace {

using detail::GrayFrame;
using DPoint = detail::Point2f;
using DRect = detail::RectF;

constexpr int kGrayDownsample = 1;
constexpr int kPointPatchRadius = 2;
constexpr int kPointSearchRadius = 6;
constexpr int kPatchSearchRadius = 9;
constexpr int kTemplateRadius = 6;
constexpr int kFeatureGridStep = 6;
constexpr float kFeatureVarianceThreshold = 45.0f;
constexpr int kMinStrongPoints = 5;
constexpr int kMinWeakPoints = 3;
constexpr int kMaxTrackPoints = 24;
constexpr int kMaxHoldFrames = 5;
constexpr float kCueAgreementRadius = 14.0f;
constexpr float kHoldCueSlack = 7.0f;
constexpr float kPointCenterSlack = 10.0f;
constexpr float kReacquireBasePadding = 4.0f;
constexpr float kReacquireGrowthPerFrame = 6.0f;
constexpr float kHoldCorrectionLimit = 3.0f;
constexpr float kResidualVelocityLimit = 12.0f;
constexpr float kHoldVelocityDecay = 0.60f;

const char* mode_to_string(BodyStateTracker::Mode mode) {
    switch (mode) {
    case BodyStateTracker::Mode::Strong:
        return "strong";
    case BodyStateTracker::Mode::Weak:
        return "weak";
    case BodyStateTracker::Mode::Hold:
        return "hold";
    case BodyStateTracker::Mode::Reacquire:
        return "reacquire";
    case BodyStateTracker::Mode::Drop:
    default:
        return "drop";
    }
}

BodyStateTracker::RectF to_rectf(const DRect& rect) {
    return {
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
    };
}

DRect to_detail_rect(const BodyStateTracker::RectF& rect) {
    return {
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
    };
}

BodyStateTracker::Point2f to_pointf(const DPoint& point) {
    return {
        point.x,
        point.y,
    };
}

DPoint to_detail_point(const BodyStateTracker::Point2f& point) {
    return {
        point.x,
        point.y,
    };
}

bool rect_valid(const DRect& rect) {
    return detail::rect_width(rect) > 1.0f && detail::rect_height(rect) > 1.0f;
}

DPoint clamp_point_to_rect(const DPoint& point, const DRect& rect) {
    return {
        detail::clampf(point.x, rect.left, rect.right),
        detail::clampf(point.y, rect.top, rect.bottom),
    };
}

DPoint add_points(const DPoint& lhs, const DPoint& rhs) {
    return {
        lhs.x + rhs.x,
        lhs.y + rhs.y,
    };
}

DPoint subtract_points(const DPoint& lhs, const DPoint& rhs) {
    return {
        lhs.x - rhs.x,
        lhs.y - rhs.y,
    };
}

DPoint scale_point(const DPoint& point, float scale) {
    return {
        point.x * scale,
        point.y * scale,
    };
}

DPoint clamp_velocity(const DPoint& velocity) {
    return {
        detail::clampf(velocity.x, -kResidualVelocityLimit, kResidualVelocityLimit),
        detail::clampf(velocity.y, -kResidualVelocityLimit, kResidualVelocityLimit),
    };
}

DPoint clamp_anchor_correction(const DPoint& anchor, const DPoint& predicted_anchor, float limit) {
    const float distance = detail::point_distance(anchor, predicted_anchor);
    if (distance <= limit || distance <= 0.0f) {
        return anchor;
    }

    const float scale = limit / distance;
    return {
        predicted_anchor.x + ((anchor.x - predicted_anchor.x) * scale),
        predicted_anchor.y + ((anchor.y - predicted_anchor.y) * scale),
    };
}

DRect expand_rect(const DRect& rect, float padding) {
    return {
        rect.left - padding,
        rect.top - padding,
        rect.right + padding,
        rect.bottom + padding,
    };
}

bool point_inside_rect(const DPoint& point, const DRect& rect) {
    return point.x >= rect.left
        && point.x <= rect.right
        && point.y >= rect.top
        && point.y <= rect.bottom;
}

std::vector<DPoint> collect_points_in_rect(const GrayFrame& gray, const DRect& rect) {
    std::vector<DPoint> points;
    if (gray.empty() || !rect_valid(rect)) {
        return points;
    }

    const int left = std::max(kPointPatchRadius + 1, static_cast<int>(std::floor(rect.left)));
    const int right = std::min(gray.width - (kPointPatchRadius + 1), static_cast<int>(std::ceil(rect.right)));
    const int top = std::max(kPointPatchRadius + 1, static_cast<int>(std::floor(rect.top)));
    const int bottom = std::min(gray.height - (kPointPatchRadius + 1), static_cast<int>(std::ceil(rect.bottom)));
    for (int y = top; y < bottom; y += kFeatureGridStep) {
        for (int x = left; x < right; x += kFeatureGridStep) {
            const float variance = detail::patch_variance(gray, x, y, kPointPatchRadius);
            if (variance < kFeatureVarianceThreshold) {
                continue;
            }
            points.push_back({static_cast<float>(x), static_cast<float>(y)});
            if (static_cast<int>(points.size()) >= kMaxTrackPoints) {
                return points;
            }
        }
    }
    return points;
}

struct PointTrackResult {
    std::vector<DPoint> points;
    DPoint anchor{};
    float confidence = 0.0f;
};

PointTrackResult track_points(
    const GrayFrame& previous_gray,
    const GrayFrame& current_gray,
    const std::vector<BodyStateTracker::Point2f>& previous_points,
    const std::vector<BodyStateTracker::Point2f>& point_offsets,
    const EgoWarp& ego_warp,
    const DPoint& residual_velocity,
    const DRect& allowed_rect) {
    PointTrackResult tracked;
    if (previous_gray.empty() || current_gray.empty() || previous_points.empty() || previous_points.size() != point_offsets.size()) {
        return tracked;
    }

    std::vector<float> anchor_xs;
    std::vector<float> anchor_ys;
    float confidence_sum = 0.0f;

    for (size_t index = 0; index < previous_points.size(); ++index) {
        const DPoint previous_point = to_detail_point(previous_points[index]);
        const DPoint expected = add_points(detail::apply_ego_warp(ego_warp, previous_point), residual_velocity);
        DPoint matched{};
        float point_confidence = 0.0f;
        if (!detail::track_patch_ssd(
                previous_gray,
                current_gray,
                nullptr,
                static_cast<int>(std::round(previous_point.x)),
                static_cast<int>(std::round(previous_point.y)),
                static_cast<int>(std::round(expected.x)),
                static_cast<int>(std::round(expected.y)),
                kPointSearchRadius,
                kPointPatchRadius,
                matched,
                point_confidence)) {
            continue;
        }

        if (!point_inside_rect(matched, expand_rect(allowed_rect, kPointCenterSlack))) {
            continue;
        }

        const DPoint offset = to_detail_point(point_offsets[index]);
        const DPoint anchor_candidate = subtract_points(matched, offset);
        anchor_xs.push_back(anchor_candidate.x);
        anchor_ys.push_back(anchor_candidate.y);
        tracked.points.push_back(matched);
        confidence_sum += point_confidence;
    }

    if (tracked.points.empty()) {
        return tracked;
    }

    tracked.anchor = {
        detail::median_value(anchor_xs),
        detail::median_value(anchor_ys),
    };
    tracked.confidence =
        (detail::clampf(static_cast<float>(tracked.points.size()) / static_cast<float>(kMinStrongPoints), 0.0f, 1.0f) * 0.65f)
        + ((confidence_sum / static_cast<float>(tracked.points.size())) * 0.35f);
    return tracked;
}

float patch_confidence_to_score(float confidence) {
    return detail::clampf(confidence, 0.0f, 1.0f);
}

BodyStateResult make_drop_result() {
    BodyStateResult result;
    result.body_state_mode = "drop";
    result.anchor_source = "none";
    return result;
}

DRect rect_from_keyframe(const TargetKeyframe& keyframe, bool torso) {
    if (torso) {
        return {
            keyframe.torso_x1,
            keyframe.torso_y1,
            keyframe.torso_x2,
            keyframe.torso_y2,
        };
    }
    return {
        keyframe.body_x1,
        keyframe.body_y1,
        keyframe.body_x2,
        keyframe.body_y2,
    };
}

DPoint anchor_prior_from_keyframe(const TargetKeyframe& keyframe, const DRect& torso_rect) {
    const DPoint raw_prior{keyframe.anchor_prior_x, keyframe.anchor_prior_y};
    if (point_inside_rect(raw_prior, torso_rect)) {
        return raw_prior;
    }
    return detail::rect_center(torso_rect);
}

} // namespace

BodyStateTracker::BodyStateTracker(int width, int height)
    : width_(width),
      height_(height) {}

void BodyStateTracker::reset() {
    active_ = false;
    previous_gray_.clear();
    previous_gray_width_ = 0;
    previous_gray_height_ = 0;
    body_box_ = {};
    torso_box_ = {};
    anchor_ = {};
    residual_velocity_ = {};
    previous_points_.clear();
    point_offsets_.clear();
    template_patch_.clear();
    template_patch_width_ = 0;
    template_patch_height_ = 0;
    missing_frames_ = 0;
    anchor_confidence_ = 0.0f;
    mode_ = Mode::Drop;
    debug_search_roi_ = {};
    debug_predicted_anchor_ = {};
    debug_patch_anchor_ = {};
    debug_patch_valid_ = false;
    debug_track_points_.clear();
}

bool BodyStateTracker::has_active_target() const {
    return active_;
}

BodyStateTracker::Mode BodyStateTracker::mode() const {
    return mode_;
}

float BodyStateTracker::anchor_confidence() const {
    return anchor_confidence_;
}

BodyStateResult BodyStateTracker::build_result(const char* anchor_source) const {
    BodyStateResult result;
    result.has_target = active_;
    result.has_body_box = active_;
    result.target_x = anchor_.x;
    result.target_y = anchor_.y;
    result.anchor_confidence = anchor_confidence_;
    result.body_x1 = body_box_.left;
    result.body_y1 = body_box_.top;
    result.body_x2 = body_box_.right;
    result.body_y2 = body_box_.bottom;
    result.torso_x1 = torso_box_.left;
    result.torso_y1 = torso_box_.top;
    result.torso_x2 = torso_box_.right;
    result.torso_y2 = torso_box_.bottom;
    result.body_state_mode = mode_to_string(mode_);
    result.anchor_source = anchor_source;
    result.debug_search_x1 = debug_search_roi_.left;
    result.debug_search_y1 = debug_search_roi_.top;
    result.debug_search_x2 = debug_search_roi_.right;
    result.debug_search_y2 = debug_search_roi_.bottom;
    result.debug_predicted_x = debug_predicted_anchor_.x;
    result.debug_predicted_y = debug_predicted_anchor_.y;
    result.debug_patch_x = debug_patch_anchor_.x;
    result.debug_patch_y = debug_patch_anchor_.y;
    result.debug_patch_valid = debug_patch_valid_;
    result.debug_template_w = static_cast<float>(template_patch_width_);
    result.debug_template_h = static_cast<float>(template_patch_height_);
    result.debug_track_points.reserve(debug_track_points_.size() * 2);
    for (const Point2f& point : debug_track_points_) {
        result.debug_track_points.push_back(point.x);
        result.debug_track_points.push_back(point.y);
    }
    return result;
}

void BodyStateTracker::prime_from_keyframe(
    const TargetKeyframe& keyframe,
    const EgoFrameView& frame) {
    prime_from_keyframe_gray(keyframe, detail::to_grayscale(frame, kGrayDownsample));
}

void BodyStateTracker::prime_from_keyframe_gray(
    const TargetKeyframe& keyframe,
    const GrayFrame& current_gray) {
    const DRect selected_body = detail::clamp_rect(rect_from_keyframe(keyframe, false), width_, height_);
    const DRect selected_torso = detail::clamp_rect(rect_from_keyframe(keyframe, true), width_, height_);
    if (current_gray.empty() || !rect_valid(selected_body) || !rect_valid(selected_torso)) {
        reset();
        return;
    }

    const DPoint torso_prior = clamp_point_to_rect(
        anchor_prior_from_keyframe(keyframe, selected_torso),
        selected_torso);
    body_box_ = to_rectf(selected_body);
    torso_box_ = to_rectf(selected_torso);
    anchor_ = to_pointf(torso_prior);
    residual_velocity_ = {};
    debug_search_roi_ = to_rectf(selected_torso);
    debug_predicted_anchor_ = anchor_;
    debug_patch_anchor_ = anchor_;
    debug_patch_valid_ = false;
    previous_points_.clear();
    point_offsets_.clear();
    debug_track_points_.clear();
    for (const DPoint& point : collect_points_in_rect(current_gray, selected_torso)) {
        previous_points_.push_back(to_pointf(point));
        point_offsets_.push_back(to_pointf(subtract_points(point, torso_prior)));
        debug_track_points_.push_back(to_pointf(point));
    }
    if (!keyframe.torso_patch.empty() && keyframe.patch_width > 0 && keyframe.patch_height > 0) {
        template_patch_ = keyframe.torso_patch;
        template_patch_width_ = keyframe.patch_width;
        template_patch_height_ = keyframe.patch_height;
    } else {
        detail::extract_patch(
            current_gray,
            static_cast<int>(std::round(anchor_.x)),
            static_cast<int>(std::round(anchor_.y)),
            kTemplateRadius,
            template_patch_,
            template_patch_width_,
            template_patch_height_);
    }
    previous_gray_ = current_gray.pixels;
    previous_gray_width_ = current_gray.width;
    previous_gray_height_ = current_gray.height;
    active_ = true;
    missing_frames_ = 0;
    mode_ = previous_points_.size() >= static_cast<size_t>(kMinStrongPoints) ? Mode::Strong : Mode::Weak;
    anchor_confidence_ = previous_points_.empty()
        ? 0.32f
        : (previous_points_.size() >= static_cast<size_t>(kMinStrongPoints) ? 0.72f : 0.48f);
}

BodyStateResult BodyStateTracker::update_observed(
    const TargetKeyframe& keyframe,
    const EgoFrameView& frame,
    const EgoWarp& ego_warp) {
    return update_observed_gray(keyframe, detail::to_grayscale(frame, kGrayDownsample), ego_warp);
}

BodyStateResult BodyStateTracker::update_observed_gray(
    const TargetKeyframe& keyframe,
    const GrayFrame& current_gray,
    const EgoWarp& ego_warp) {
    const DRect selected_body = detail::clamp_rect(rect_from_keyframe(keyframe, false), width_, height_);
    const DRect selected_torso = detail::clamp_rect(rect_from_keyframe(keyframe, true), width_, height_);
    if (current_gray.empty() || !rect_valid(selected_body) || !rect_valid(selected_torso)) {
        reset();
        return make_drop_result();
    }

    const DPoint torso_prior = anchor_prior_from_keyframe(keyframe, selected_torso);
    const bool recovering_from_missing =
        missing_frames_ > 0 || mode_ == Mode::Hold || mode_ == Mode::Reacquire;
    const bool has_previous_gray =
        !previous_gray_.empty()
        && previous_gray_width_ == current_gray.width
        && previous_gray_height_ == current_gray.height;

    if (!active_ || !has_previous_gray) {
        prime_from_keyframe_gray(keyframe, current_gray);
        return build_result(previous_points_.empty() ? "torso_prior" : "torso_anchor");
    }

    GrayFrame previous_gray;
    previous_gray.width = previous_gray_width_;
    previous_gray.height = previous_gray_height_;
    previous_gray.pixels = previous_gray_;

    const DPoint warped_anchor = add_points(detail::apply_ego_warp(ego_warp, to_detail_point(anchor_)), to_detail_point(residual_velocity_));
    const DPoint predicted_anchor = clamp_point_to_rect(warped_anchor, selected_torso);
    const PointTrackResult tracked = track_points(
        previous_gray,
        current_gray,
        previous_points_,
        point_offsets_,
        ego_warp,
        to_detail_point(residual_velocity_),
        selected_torso);

    DPoint patch_anchor = predicted_anchor;
    float patch_confidence = 0.0f;
    const bool patch_ok = detail::search_patch_ncc(
        current_gray,
        template_patch_,
        template_patch_width_,
        template_patch_height_,
        static_cast<int>(std::round(predicted_anchor.x)),
        static_cast<int>(std::round(predicted_anchor.y)),
        kPatchSearchRadius,
        nullptr,
        patch_anchor,
        patch_confidence)
        && point_inside_rect(patch_anchor, expand_rect(selected_torso, kPointCenterSlack));
    debug_search_roi_ = to_rectf(selected_torso);
    debug_predicted_anchor_ = to_pointf(predicted_anchor);
    debug_patch_anchor_ = to_pointf(patch_anchor);
    debug_patch_valid_ = patch_ok;
    debug_track_points_.clear();
    for (const DPoint& point : tracked.points) {
        debug_track_points_.push_back(to_pointf(point));
    }

    const bool klt_strong = tracked.points.size() >= static_cast<size_t>(kMinStrongPoints);
    const bool klt_weak = tracked.points.size() >= static_cast<size_t>(kMinWeakPoints);
    const bool patch_agrees = patch_ok && detail::point_distance(tracked.anchor, patch_anchor) <= kCueAgreementRadius;

    DPoint final_anchor = clamp_point_to_rect(torso_prior, selected_torso);
    const char* anchor_source = "torso_prior";
    Mode next_mode = recovering_from_missing ? Mode::Reacquire : Mode::Weak;
    float next_confidence = recovering_from_missing
        ? (0.24f + (ego_warp.confidence * 0.08f))
        : (0.35f + (ego_warp.confidence * 0.10f));

    if (klt_strong && patch_agrees) {
        final_anchor = clamp_point_to_rect(
            {
                (tracked.anchor.x + patch_anchor.x) * 0.5f,
                (tracked.anchor.y + patch_anchor.y) * 0.5f,
            },
            selected_torso);
        anchor_source = "torso_anchor";
        next_mode = Mode::Strong;
        next_confidence = std::max(tracked.confidence, patch_confidence_to_score(patch_confidence));
    } else if (klt_strong && !patch_ok) {
        final_anchor = clamp_point_to_rect(tracked.anchor, selected_torso);
        anchor_source = "torso_anchor";
        next_mode = Mode::Strong;
        next_confidence = tracked.confidence;
    } else if (klt_weak && (!patch_ok || patch_agrees)) {
        final_anchor = clamp_point_to_rect(tracked.anchor, selected_torso);
        anchor_source = "torso_anchor";
        next_mode = recovering_from_missing ? Mode::Reacquire : Mode::Weak;
        next_confidence = std::max(
            recovering_from_missing ? 0.34f : 0.45f,
            tracked.confidence * 0.85f);
    } else if (patch_ok && detail::point_distance(patch_anchor, torso_prior) <= kCueAgreementRadius) {
        final_anchor = clamp_point_to_rect(patch_anchor, selected_torso);
        anchor_source = "torso_anchor";
        next_mode = patch_confidence >= 0.75f
            ? Mode::Strong
            : (recovering_from_missing ? Mode::Reacquire : Mode::Weak);
        next_confidence = std::max(
            recovering_from_missing ? 0.38f : 0.46f,
            patch_confidence_to_score(patch_confidence));
    }

    const DPoint new_residual_velocity = clamp_velocity(subtract_points(final_anchor, detail::apply_ego_warp(ego_warp, to_detail_point(anchor_))));

    body_box_ = to_rectf(selected_body);
    torso_box_ = to_rectf(selected_torso);
    anchor_ = to_pointf(final_anchor);
    residual_velocity_ = to_pointf(new_residual_velocity);
    previous_points_.clear();
    point_offsets_.clear();

    const std::vector<DPoint> reseed_points = tracked.points.size() >= static_cast<size_t>(kMinWeakPoints)
        ? tracked.points
        : collect_points_in_rect(current_gray, selected_torso);
    for (const DPoint& point : reseed_points) {
        previous_points_.push_back(to_pointf(point));
        point_offsets_.push_back(to_pointf(subtract_points(point, final_anchor)));
    }

    if (next_mode == Mode::Strong) {
        detail::extract_patch(
            current_gray,
            static_cast<int>(std::round(final_anchor.x)),
            static_cast<int>(std::round(final_anchor.y)),
            kTemplateRadius,
            template_patch_,
            template_patch_width_,
            template_patch_height_);
    }

    previous_gray_ = current_gray.pixels;
    previous_gray_width_ = current_gray.width;
    previous_gray_height_ = current_gray.height;
    active_ = true;
    missing_frames_ = 0;
    mode_ = next_mode;
    anchor_confidence_ = detail::clampf(next_confidence, 0.0f, 1.0f);
    return build_result(anchor_source);
}

BodyStateResult BodyStateTracker::update_unobserved(
    const EgoFrameView& frame,
    const EgoWarp& ego_warp,
    bool consume_scan_miss_budget) {
    return update_unobserved_gray(detail::to_grayscale(frame, kGrayDownsample), ego_warp, consume_scan_miss_budget);
}

BodyStateResult BodyStateTracker::update_unobserved_gray(
    const GrayFrame& current_gray,
    const EgoWarp& ego_warp,
    bool consume_scan_miss_budget) {
    if (!active_) {
        return make_drop_result();
    }

    if (current_gray.empty()) {
        reset();
        return make_drop_result();
    }

    GrayFrame previous_gray;
    previous_gray.width = previous_gray_width_;
    previous_gray.height = previous_gray_height_;
    previous_gray.pixels = previous_gray_;
    if (previous_gray.empty()) {
        reset();
        return make_drop_result();
    }

    const DRect warped_body = detail::clamp_rect(detail::apply_ego_warp(ego_warp, to_detail_rect(body_box_)), width_, height_);
    const DRect warped_torso = detail::clamp_rect(detail::apply_ego_warp(ego_warp, to_detail_rect(torso_box_)), width_, height_);
    const int next_missing_frames = missing_frames_ + (consume_scan_miss_budget ? 1 : 0);
    const float reacquire_padding =
        kReacquireBasePadding + (static_cast<float>(std::max(0, next_missing_frames - 1)) * kReacquireGrowthPerFrame);
    const DRect reacquire_window =
        detail::clamp_rect(expand_rect(warped_torso, reacquire_padding), width_, height_);
    DPoint damped_velocity = scale_point(to_detail_point(residual_velocity_), kHoldVelocityDecay);
    DPoint predicted_anchor = clamp_point_to_rect(
        add_points(detail::apply_ego_warp(ego_warp, to_detail_point(anchor_)), damped_velocity),
        warped_torso);

    const PointTrackResult tracked = track_points(
        previous_gray,
        current_gray,
        previous_points_,
        point_offsets_,
        ego_warp,
        damped_velocity,
        reacquire_window);

    DPoint patch_anchor = predicted_anchor;
    float patch_confidence = 0.0f;
    const bool patch_ok = detail::search_patch_ncc(
        current_gray,
        template_patch_,
        template_patch_width_,
        template_patch_height_,
        static_cast<int>(std::round(predicted_anchor.x)),
        static_cast<int>(std::round(predicted_anchor.y)),
        kPatchSearchRadius,
        nullptr,
        patch_anchor,
        patch_confidence)
        && point_inside_rect(patch_anchor, expand_rect(reacquire_window, kPointCenterSlack));
    debug_search_roi_ = to_rectf(reacquire_window);
    debug_predicted_anchor_ = to_pointf(predicted_anchor);
    debug_patch_anchor_ = to_pointf(patch_anchor);
    debug_patch_valid_ = patch_ok;
    debug_track_points_.clear();
    for (const DPoint& point : tracked.points) {
        debug_track_points_.push_back(to_pointf(point));
    }

    const bool klt_weak = tracked.points.size() >= static_cast<size_t>(kMinWeakPoints);
    const bool klt_near_predicted =
        klt_weak && detail::point_distance(tracked.anchor, predicted_anchor) <= kHoldCueSlack;
    const bool patch_near_predicted =
        patch_ok && detail::point_distance(patch_anchor, predicted_anchor) <= kHoldCueSlack;
    const bool patch_agrees =
        patch_near_predicted && (!klt_weak || detail::point_distance(tracked.anchor, patch_anchor) <= kCueAgreementRadius);

    DPoint final_anchor = predicted_anchor;
    const char* anchor_source = "torso_prior";
    Mode next_mode = Mode::Hold;
    float next_confidence = 0.18f + (ego_warp.confidence * 0.20f);

    if (klt_near_predicted && patch_agrees) {
        final_anchor = clamp_point_to_rect(
            clamp_anchor_correction(
            patch_ok
                ? DPoint{
                    (tracked.anchor.x + patch_anchor.x) * 0.5f,
                    (tracked.anchor.y + patch_anchor.y) * 0.5f,
                }
                : tracked.anchor,
            predicted_anchor,
            kHoldCorrectionLimit),
            warped_torso);
        anchor_source = "torso_anchor";
        next_mode = Mode::Hold;
        next_confidence = std::max(tracked.confidence * 0.85f, patch_confidence_to_score(patch_confidence) * 0.80f);
    } else if (klt_near_predicted) {
        final_anchor = clamp_point_to_rect(
            clamp_anchor_correction(tracked.anchor, predicted_anchor, kHoldCorrectionLimit),
            warped_torso);
        anchor_source = "torso_anchor";
        next_mode = Mode::Hold;
        next_confidence = std::max(0.24f, tracked.confidence * 0.75f);
    } else if (patch_near_predicted) {
        final_anchor = clamp_point_to_rect(
            clamp_anchor_correction(patch_anchor, predicted_anchor, kHoldCorrectionLimit),
            warped_torso);
        anchor_source = "torso_anchor";
        next_mode = Mode::Hold;
        next_confidence = std::max(0.28f, patch_confidence_to_score(patch_confidence) * 0.75f);
    } else if ((klt_weak || patch_ok) && missing_frames_ == 0) {
        next_mode = Mode::Hold;
        next_confidence = 0.18f + (ego_warp.confidence * 0.16f);
    } else if (ego_warp.confidence >= 0.15f) {
        next_mode = Mode::Reacquire;
        next_confidence = 0.12f + (ego_warp.confidence * 0.14f);
    } else if (!klt_weak && ego_warp.confidence < 0.15f) {
        reset();
        return make_drop_result();
    }

    if (consume_scan_miss_budget) {
        missing_frames_ = next_missing_frames;
    }
    if (consume_scan_miss_budget && missing_frames_ > kMaxHoldFrames) {
        reset();
        return make_drop_result();
    }

    body_box_ = to_rectf(warped_body);
    torso_box_ = to_rectf(warped_torso);
    anchor_ = to_pointf(final_anchor);
    residual_velocity_ = to_pointf(damped_velocity);
    previous_gray_ = current_gray.pixels;
    previous_gray_width_ = current_gray.width;
    previous_gray_height_ = current_gray.height;
    previous_points_.clear();
    point_offsets_.clear();

    const std::vector<DPoint> reseed_points = tracked.points.size() >= static_cast<size_t>(kMinWeakPoints)
        ? tracked.points
        : collect_points_in_rect(current_gray, reacquire_window);
    for (const DPoint& point : reseed_points) {
        previous_points_.push_back(to_pointf(point));
        point_offsets_.push_back(to_pointf(subtract_points(point, final_anchor)));
    }

    mode_ = next_mode;
    anchor_confidence_ = detail::clampf(next_confidence, 0.0f, 1.0f);

    return build_result(anchor_source);
}

BodyStateResult BodyStateTracker::update_interframe(
    const EgoFrameView& frame,
    const EgoWarp& ego_warp) {
    return update_unobserved(frame, ego_warp, false);
}

BodyStateResult BodyStateTracker::update_interframe_gray(
    const GrayFrame& frame,
    const EgoWarp& ego_warp) {
    return update_unobserved_gray(frame, ego_warp, false);
}

BodyStateResult BodyStateTracker::update_scan_miss(
    const EgoFrameView& frame,
    const EgoWarp& ego_warp) {
    return update_unobserved(frame, ego_warp, true);
}

BodyStateResult BodyStateTracker::update_scan_miss_gray(
    const GrayFrame& frame,
    const EgoWarp& ego_warp) {
    return update_unobserved_gray(frame, ego_warp, true);
}

BodyStateResult BodyStateTracker::update_selected(
    const VisionResult& selected_target,
    const EgoFrameView& frame,
    const EgoWarp& ego_warp) {
    return update_selected_gray(selected_target, detail::to_grayscale(frame, kGrayDownsample), ego_warp);
}

BodyStateResult BodyStateTracker::update_selected_gray(
    const VisionResult& selected_target,
    const GrayFrame& frame,
    const EgoWarp& ego_warp) {
    if (!selected_target.has_body_box) {
        reset();
        return make_drop_result();
    }

    TargetKeyframe keyframe;
    keyframe.body_x1 = selected_target.body_x1;
    keyframe.body_y1 = selected_target.body_y1;
    keyframe.body_x2 = selected_target.body_x2;
    keyframe.body_y2 = selected_target.body_y2;
    keyframe.torso_x1 = selected_target.torso_x1;
    keyframe.torso_y1 = selected_target.torso_y1;
    keyframe.torso_x2 = selected_target.torso_x2;
    keyframe.torso_y2 = selected_target.torso_y2;
    const DRect body_rect = detail::rect_from_result(selected_target);
    const DRect torso_rect = rect_valid(rect_from_keyframe(keyframe, true))
        ? rect_from_keyframe(keyframe, true)
        : detail::torso_band_from_body_box(body_rect);
    keyframe.torso_x1 = torso_rect.left;
    keyframe.torso_y1 = torso_rect.top;
    keyframe.torso_x2 = torso_rect.right;
    keyframe.torso_y2 = torso_rect.bottom;
    keyframe.anchor_prior_x = selected_target.target_x;
    keyframe.anchor_prior_y = selected_target.target_y;
    keyframe.target_source = selected_target.target_source;
    return update_observed_gray(keyframe, frame, ego_warp);
}

BodyStateResult BodyStateTracker::update_missing(
    const EgoFrameView& frame,
    const EgoWarp& ego_warp) {
    return update_scan_miss(frame, ego_warp);
}

BodyStateResult BodyStateTracker::update_missing_gray(
    const GrayFrame& frame,
    const EgoWarp& ego_warp) {
    return update_scan_miss_gray(frame, ego_warp);
}

} // namespace vision_native
