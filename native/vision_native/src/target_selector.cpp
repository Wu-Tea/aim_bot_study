#include "vision_native/target_selector.h"

#include "color_utils.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vision_native {
namespace {

constexpr float kUpperChestRatio = 0.38f;
constexpr float kTorsoBoxShrinkX = 0.22f;
constexpr float kTorsoBoxShrinkTop = 0.18f;
constexpr float kTorsoBoxShrinkBottom = 0.20f;
constexpr float kFireShrinkX = 0.12f;
constexpr float kFireShrinkTop = 0.05f;
constexpr float kFireShrinkBottom = 0.15f;
constexpr float kMinPickupHeightRatio = 0.08f;
constexpr float kMinTrackingHeightRatio = 0.06f;
constexpr float kMinPickupAreaRatio = 0.003f;
constexpr float kMinTrackingAreaRatio = 0.002f;
constexpr float kMinAspectRatio = 0.85f;
constexpr float kMaxAspectRatio = 4.50f;
constexpr float kConfidenceScoreScale = 400.0f;
constexpr float kMinSmoothingAlpha = 0.25f;
constexpr float kTrackingSwitchMargin = 80.0f;
constexpr float kMaxJumpXRatio = 180.0f / 640.0f;
constexpr float kMaxJumpYRatio = 180.0f / 640.0f;
constexpr float kDistanceScoreScale = 800.0f;
constexpr float kTrackingRadiusRatio = 120.0f / 640.0f;
constexpr float kMaxSmoothingJumpRatio = 24.0f / 640.0f;
constexpr float kPickupConfirmRadiusRatio = 32.0f / 640.0f;
constexpr float kIdealAreaRatio = 8000.0f / (640.0f * 640.0f);
constexpr float kMaxAreaLimitRatio = 40000.0f / (640.0f * 640.0f);
constexpr int kPickupConfirmFrames = 2;
constexpr int kTargetHoldFrames = 2;
constexpr int kSwitchConfirmFrames = 2;
constexpr float kActiveTargetIouThreshold = 0.12f;
constexpr float kActiveTargetCenterXRatio = 0.65f;
constexpr float kActiveTargetCenterYRatio = 0.35f;
constexpr float kActiveTargetScoreSwitchMargin = 2000.0f;
constexpr float kSwitchCrosshairMarginRatio = 16.0f / 640.0f;
constexpr float kCrosshairPriorityMarginRatio = 10.0f / 640.0f;
constexpr float kPickupConfidenceThreshold = 0.65f;
constexpr float kPickupEnemyConfidenceThreshold = 0.42f;
constexpr float kTrackingConfidenceThreshold = 0.40f;
constexpr float kTrackingBonus = 2000.0f;
constexpr float kMinScoreThreshold = -50000.0f;
constexpr float kMaxColorBonus = 10000.0f;
constexpr float kFriendlyMaskMinRatio = 0.02f;
constexpr float kFriendlyMaskMaxRatio = 0.35f;
constexpr float kEnemyMaskMinRatio = 0.03f;
constexpr float kEnemyMaskMaxRatio = 0.45f;
constexpr int kMaxStableSamples = 3;
constexpr float kMaxCenterXDelta = 24.0f;
constexpr float kMaxBottomDelta = 24.0f;
constexpr float kMinHeightRatio = 0.72f;
constexpr float kMinTopDrop = 12.0f;
constexpr float kAutoFireEdgePadding = 2.0f;
constexpr int kAutoFireReleaseGraceFrames = 4;
constexpr const char* kSelectorHoldSource = "selector_hold";
constexpr float kCueSeedBonus = 2600.0f;
constexpr float kCueSeedPreferredYOffsetRatio = 0.10f;
constexpr float kCueSeedMaxXRatio = 0.38f;
constexpr float kCueSeedAboveHeadRatio = 0.42f;
constexpr float kCueSeedBelowHeadRatio = 0.16f;

struct ColorClassification {
    float color_bonus = 0.0f;
    bool is_friendly = false;
};

struct IntRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

float rect_width(const VisionTargetSelector::Rect& rect) {
    return rect.right - rect.left;
}

float rect_height(const VisionTargetSelector::Rect& rect) {
    return rect.bottom - rect.top;
}

std::pair<float, float> rect_center(const VisionTargetSelector::Rect& rect) {
    return {
        (rect.left + rect.right) * 0.5f,
        (rect.top + rect.bottom) * 0.5f,
    };
}

float rect_iou(const VisionTargetSelector::Rect& lhs, const VisionTargetSelector::Rect& rhs) {
    const float left = std::max(lhs.left, rhs.left);
    const float top = std::max(lhs.top, rhs.top);
    const float right = std::min(lhs.right, rhs.right);
    const float bottom = std::min(lhs.bottom, rhs.bottom);
    const float inter_w = std::max(0.0f, right - left);
    const float inter_h = std::max(0.0f, bottom - top);
    const float inter_area = inter_w * inter_h;
    if (inter_area <= 0.0f) {
        return 0.0f;
    }

    const float lhs_area = std::max(0.0f, rect_width(lhs)) * std::max(0.0f, rect_height(lhs));
    const float rhs_area = std::max(0.0f, rect_width(rhs)) * std::max(0.0f, rect_height(rhs));
    const float union_area = lhs_area + rhs_area - inter_area;
    if (union_area <= 0.0f) {
        return 0.0f;
    }
    return inter_area / union_area;
}

float point_distance(
    const std::pair<float, float>& lhs,
    const std::pair<float, float>& rhs) {
    return std::hypot(lhs.first - rhs.first, lhs.second - rhs.second);
}

float clamp_value(float value, float limit) {
    return std::max(-limit, std::min(limit, value));
}

std::pair<float, float> apply_ego_warp(
    const EgoWarp& ego_warp,
    const std::pair<float, float>& point) {
    return {
        (ego_warp.a00 * point.first) + (ego_warp.a01 * point.second) + ego_warp.tx,
        (ego_warp.a10 * point.first) + (ego_warp.a11 * point.second) + ego_warp.ty,
    };
}

VisionTargetSelector::Rect apply_ego_warp(
    const EgoWarp& ego_warp,
    const VisionTargetSelector::Rect& rect) {
    const auto tl = apply_ego_warp(ego_warp, std::pair<float, float>{rect.left, rect.top});
    const auto tr = apply_ego_warp(ego_warp, std::pair<float, float>{rect.right, rect.top});
    const auto bl = apply_ego_warp(ego_warp, std::pair<float, float>{rect.left, rect.bottom});
    const auto br = apply_ego_warp(ego_warp, std::pair<float, float>{rect.right, rect.bottom});
    return {
        std::min(std::min(tl.first, tr.first), std::min(bl.first, br.first)),
        std::min(std::min(tl.second, tr.second), std::min(bl.second, br.second)),
        std::max(std::max(tl.first, tr.first), std::max(bl.first, br.first)),
        std::max(std::max(tl.second, tr.second), std::max(bl.second, br.second)),
    };
}

VisionTargetSelector::Candidate warp_candidate(
    const EgoWarp& ego_warp,
    const VisionTargetSelector::Candidate& candidate) {
    VisionTargetSelector::Candidate warped = candidate;
    warped.body_box = apply_ego_warp(ego_warp, candidate.body_box);
    warped.slow_zone = apply_ego_warp(ego_warp, candidate.slow_zone);
    warped.fire_zone = apply_ego_warp(ego_warp, candidate.fire_zone);
    const auto warped_point = apply_ego_warp(
        ego_warp,
        std::pair<float, float>{candidate.target_x, candidate.target_y});
    warped.target_x = warped_point.first;
    warped.target_y = warped_point.second;
    return warped;
}

VisionTargetSelector::TargetState warp_target_state(
    const EgoWarp& ego_warp,
    const VisionTargetSelector::TargetState& target) {
    VisionTargetSelector::TargetState warped = target;
    warped.candidate = warp_candidate(ego_warp, target.candidate);
    return warped;
}

float median_value(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }

    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if ((values.size() % 2) == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) * 0.5f;
}

std::optional<IntRect> color_roi_bounds(
    const VisionTargetSelector::Rect& box,
    int frame_width,
    int frame_height) {
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    const float cx = (box.left + box.right) * 0.5f;
    const int roi_h = static_cast<int>(std::max(12.0f, std::min(36.0f, box_h * 0.20f)));
    const int roi_w = static_cast<int>(std::max(24.0f, std::min(80.0f, box_w * 0.80f)));
    const int roi_bottom = std::max(0, std::min(frame_height, static_cast<int>(box.top) - 2));
    const int roi_top = std::max(0, roi_bottom - roi_h);
    const int roi_left = std::max(0, static_cast<int>(cx - (static_cast<float>(roi_w) * 0.5f)));
    const int roi_right = std::min(frame_width, static_cast<int>(cx + (static_cast<float>(roi_w) * 0.5f)));

    if ((roi_bottom - roi_top) < 4 || (roi_right - roi_left) < 4) {
        return std::nullopt;
    }
    return IntRect{roi_left, roi_top, roi_right, roi_bottom};
}

ColorClassification classify_color(
    const VisionTargetSelector::Rect& box,
    const VisionTargetSelector::ColorFrameView& frame) {
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return {};
    }

    const auto bounds = color_roi_bounds(box, frame.width, frame.height);
    if (!bounds.has_value()) {
        return {};
    }

    int friendly_count = 0;
    int enemy_count = 0;
    int area = 0;
    for (int y = bounds->top; y < bounds->bottom; ++y) {
        for (int x = bounds->left; x < bounds->right; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            color_detail::read_rgb(frame, x, y, r, g, b);

            float h = 0.0f;
            float s = 0.0f;
            float v = 0.0f;
            color_detail::rgb_to_opencv_hsv(r, g, b, h, s, v);
            if (color_detail::hsv_in_range(h, s, v, 45.0f, 80.0f, 50.0f, 75.0f, 255.0f, 255.0f)) {
                ++friendly_count;
            }
            if (color_detail::hsv_in_range(h, s, v, 20.0f, 120.0f, 120.0f, 35.0f, 255.0f, 255.0f)
                || color_detail::hsv_in_range(h, s, v, 0.0f, 120.0f, 80.0f, 10.0f, 255.0f, 255.0f)
                || color_detail::hsv_in_range(h, s, v, 170.0f, 120.0f, 80.0f, 180.0f, 255.0f, 255.0f)) {
                ++enemy_count;
            }
            ++area;
        }
    }

    if (area <= 0) {
        return {};
    }

    const float friendly_ratio = static_cast<float>(friendly_count) / static_cast<float>(area);
    if (kFriendlyMaskMinRatio <= friendly_ratio && friendly_ratio <= kFriendlyMaskMaxRatio) {
        return ColorClassification{0.0f, true};
    }

    const float enemy_ratio = static_cast<float>(enemy_count) / static_cast<float>(area);
    if (kEnemyMaskMinRatio <= enemy_ratio && enemy_ratio <= kEnemyMaskMaxRatio) {
        return ColorClassification{kMaxColorBonus, false};
    }

    return {};
}

bool scored_candidate_better(
    const VisionTargetSelector::ScoredCandidate& lhs,
    const VisionTargetSelector::ScoredCandidate& rhs) {
    if (std::fabs(lhs.score - rhs.score) > 0.001f) {
        return lhs.score > rhs.score;
    }
    if (lhs.has_tracking_distance != rhs.has_tracking_distance) {
        return lhs.has_tracking_distance;
    }
    if (lhs.has_tracking_distance && rhs.has_tracking_distance) {
        if (std::fabs(lhs.tracking_distance - rhs.tracking_distance) > 0.001f) {
            return lhs.tracking_distance < rhs.tracking_distance;
        }
    }
    if (std::fabs(lhs.candidate.conf - rhs.candidate.conf) > 0.001f) {
        return lhs.candidate.conf > rhs.candidate.conf;
    }
    return lhs.candidate.target_y < rhs.candidate.target_y;
}

} // namespace

VisionTargetSelector::VisionTargetSelector(int frame_width, int frame_height)
    : frame_width_(static_cast<float>(frame_width)),
      frame_height_(static_cast<float>(frame_height)),
      screen_center_x_(frame_width_ * 0.5f),
      screen_center_y_(frame_height_ * 0.5f) {
    const float avg_dim = (frame_width_ + frame_height_) * 0.5f;
    const float frame_area = frame_width_ * frame_height_;
    max_jump_x_ = frame_width_ * kMaxJumpXRatio;
    max_jump_y_ = frame_height_ * kMaxJumpYRatio;
    tracking_radius_ = avg_dim * kTrackingRadiusRatio;
    max_smoothing_jump_ = avg_dim * kMaxSmoothingJumpRatio;
    pickup_confirm_radius_ = avg_dim * kPickupConfirmRadiusRatio;
    switch_crosshair_margin_ = avg_dim * kSwitchCrosshairMarginRatio;
    crosshair_priority_margin_ = avg_dim * kCrosshairPriorityMarginRatio;
    ideal_area_ = frame_area * kIdealAreaRatio;
    max_area_limit_ = frame_area * kMaxAreaLimitRatio;
}

void VisionTargetSelector::reset() {
    clear_tracking_state();
    clear_auto_fire_state();
}

void VisionTargetSelector::clear_tracking_state() {
    last_target_center_.reset();
    active_target_.reset();
    pending_target_.reset();
    pending_switch_target_.reset();
    stable_samples_.clear();
    pending_frames_ = 0;
    pending_switch_frames_ = 0;
    hold_frames_ = 0;
}

void VisionTargetSelector::clear_auto_fire_state() {
    auto_fire_holding_ = false;
    auto_fire_miss_frames_ = 0;
}

VisionResult VisionTargetSelector::select_with_frame(
    const DetectionBatch& batch,
    const ColorFrameView& frame,
    const EgoWarp& ego_warp,
    const std::optional<CueSeed>& cue_seed) {
    return select(annotate_colors(batch, frame), ego_warp, cue_seed);
}

std::vector<VisionTargetSelector::ScoredCandidate> VisionTargetSelector::rank_candidates(
    const DetectionBatch& batch) const {
    const auto candidates = build_candidates(batch, std::nullopt);
    std::vector<ScoredCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        ranked.push_back(score_candidate(candidate, std::nullopt));
    }
    std::sort(ranked.begin(), ranked.end(), scored_candidate_better);
    return ranked;
}

std::vector<VisionTargetSelector::ScoredCandidate> VisionTargetSelector::rank_candidates_with_frame(
    const DetectionBatch& batch,
    const ColorFrameView& frame) const {
    return rank_candidates(annotate_colors(batch, frame));
}

VisionResult VisionTargetSelector::empty_result(float boxes_seen) const {
    VisionResult result;
    result.screen_center_x = screen_center_x_;
    result.screen_center_y = screen_center_y_;
    result.target_x = screen_center_x_;
    result.target_y = screen_center_y_;
    result.boxes_seen = boxes_seen;
    return result;
}

VisionResult VisionTargetSelector::result_from_target(const TargetState& target, float boxes_seen) const {
    VisionResult result = empty_result(boxes_seen);
    result.has_target = true;
    result.has_body_box = true;
    result.target_x = target.candidate.target_x;
    result.target_y = target.candidate.target_y;
    result.dx = result.target_x - result.screen_center_x;
    result.dy = result.target_y - result.screen_center_y;
    result.body_x1 = target.candidate.body_box.left;
    result.body_y1 = target.candidate.body_box.top;
    result.body_x2 = target.candidate.body_box.right;
    result.body_y2 = target.candidate.body_box.bottom;
    result.target_source = target.candidate.source;
    return result;
}

VisionTargetSelector::Rect VisionTargetSelector::to_rect(const Detection& detection) const {
    return {
        detection.x1,
        detection.y1,
        detection.x2,
        detection.y2,
    };
}

std::pair<float, float> VisionTargetSelector::target_point(const Rect& box) const {
    return {
        (box.left + box.right) * 0.5f,
        box.top + (rect_height(box) * kUpperChestRatio),
    };
}

VisionTargetSelector::Rect VisionTargetSelector::fallback_slow_zone(const Rect& box) const {
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    return {
        box.left + (box_w * kTorsoBoxShrinkX),
        box.top + (box_h * kTorsoBoxShrinkTop),
        box.right - (box_w * kTorsoBoxShrinkX),
        box.bottom - (box_h * kTorsoBoxShrinkBottom),
    };
}

VisionTargetSelector::Rect VisionTargetSelector::fire_zone(const Rect& box) const {
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    return {
        box.left + (box_w * kFireShrinkX),
        box.top + (box_h * kFireShrinkTop),
        box.right - (box_w * kFireShrinkX),
        box.bottom - (box_h * kFireShrinkBottom),
    };
}

DetectionBatch VisionTargetSelector::annotate_colors(
    const DetectionBatch& batch,
    const ColorFrameView& frame) const {
    DetectionBatch annotated = batch;
    for (auto& detection : annotated.detections) {
        const ColorClassification classification = classify_color(to_rect(detection), frame);
        detection.color_bonus = classification.color_bonus;
        detection.is_friendly = classification.is_friendly;
        detection.color_classified = true;
    }
    return annotated;
}

std::optional<VisionTargetSelector::Candidate> VisionTargetSelector::try_reconstruct(
    const Candidate& candidate) const {
    if (stable_samples_.empty()) {
        return std::nullopt;
    }

    const TrackSample& latest = stable_samples_.back();
    const Rect& box = candidate.body_box;
    const float width = rect_width(box);
    const float current_height = rect_height(box);
    if (width <= 0.0f || current_height <= 0.0f) {
        return std::nullopt;
    }

    std::vector<float> heights;
    std::vector<float> centers;
    std::vector<float> bottoms;
    const size_t start = stable_samples_.size() > kMaxStableSamples
        ? stable_samples_.size() - kMaxStableSamples
        : 0;
    for (size_t index = start; index < stable_samples_.size(); ++index) {
        heights.push_back(stable_samples_[index].height);
        centers.push_back((stable_samples_[index].selected_box.left + stable_samples_[index].selected_box.right) * 0.5f);
        bottoms.push_back(stable_samples_[index].bottom_y);
    }

    const float stable_height = median_value(heights);
    const float stable_center_x = median_value(centers);
    const float stable_bottom_y = median_value(bottoms);
    if (stable_height <= 0.0f || current_height >= stable_height) {
        return std::nullopt;
    }

    const float center_x = (box.left + box.right) * 0.5f;
    const float top_drop = box.top - latest.selected_box.top;
    const bool height_shrank = current_height <= (stable_height * kMinHeightRatio);
    if (std::fabs(center_x - stable_center_x) > kMaxCenterXDelta) {
        return std::nullopt;
    }
    if (std::fabs(box.bottom - stable_bottom_y) > kMaxBottomDelta) {
        return std::nullopt;
    }
    if (!(top_drop >= kMinTopDrop || height_shrank)) {
        return std::nullopt;
    }

    std::vector<float> target_y_ratios;
    for (size_t index = start; index < stable_samples_.size(); ++index) {
        const TrackSample& sample = stable_samples_[index];
        const float height = rect_height(sample.selected_box);
        if (height > 0.0f) {
            target_y_ratios.push_back((sample.target_y - sample.selected_box.top) / height);
        }
    }

    float target_y_ratio = target_y_ratios.empty()
        ? kUpperChestRatio
        : median_value(target_y_ratios);
    target_y_ratio = std::max(0.0f, std::min(1.0f, target_y_ratio));
    const float stable_target_x_offset =
        latest.target_x - ((latest.selected_box.left + latest.selected_box.right) * 0.5f);
    const float reconstructed_top = box.bottom - stable_height;

    Candidate reconstructed = candidate;
    reconstructed.target_x = center_x + stable_target_x_offset;
    reconstructed.target_y = reconstructed_top + (stable_height * target_y_ratio);
    reconstructed.body_box.top = reconstructed_top;
    reconstructed.source = "reconstructed";
    reconstructed.slow_zone = fallback_slow_zone(reconstructed.body_box);
    reconstructed.fire_zone = fire_zone(reconstructed.body_box);
    return reconstructed;
}

void VisionTargetSelector::record_observation(const TargetState& target, float timestamp) {
    const Rect& box = target.candidate.body_box;
    if (box.right <= box.left || box.bottom <= box.top) {
        return;
    }

    TrackSample sample;
    sample.target_x = target.candidate.target_x;
    sample.target_y = target.candidate.target_y;
    sample.selected_box = box;
    sample.bottom_y = box.bottom;
    sample.height = box.bottom - box.top;
    sample.timestamp = timestamp;
    sample.source = target.candidate.source;
    stable_samples_.push_back(sample);
    if (stable_samples_.size() > kMaxStableSamples) {
        stable_samples_.erase(stable_samples_.begin());
    }
}

bool VisionTargetSelector::is_crosshair_inside_zone(const Rect& zone) const {
    return (zone.left - kAutoFireEdgePadding) <= screen_center_x_
        && screen_center_x_ <= (zone.right + kAutoFireEdgePadding)
        && (zone.top - kAutoFireEdgePadding) <= screen_center_y_
        && screen_center_y_ <= (zone.bottom + kAutoFireEdgePadding);
}

bool VisionTargetSelector::update_auto_fire(const TargetState* target) {
    if (target != nullptr && is_crosshair_inside_zone(target->candidate.fire_zone)) {
        auto_fire_holding_ = true;
        auto_fire_miss_frames_ = 0;
        return true;
    }

    if (auto_fire_holding_) {
        auto_fire_miss_frames_ += 1;
        if (auto_fire_miss_frames_ >= kAutoFireReleaseGraceFrames) {
            clear_auto_fire_state();
            return false;
        }
    }

    return auto_fire_holding_;
}

bool VisionTargetSelector::passes_geometry_gate(float box_w, float box_h, bool tracking_candidate) const {
    const float aspect_ratio = box_w > 0.0f ? (box_h / box_w) : 0.0f;
    if (aspect_ratio < kMinAspectRatio || aspect_ratio > kMaxAspectRatio) {
        return false;
    }

    const float min_height = frame_height_ * (tracking_candidate ? kMinTrackingHeightRatio : kMinPickupHeightRatio);
    const float min_area = (frame_width_ * frame_height_) * (tracking_candidate ? kMinTrackingAreaRatio : kMinPickupAreaRatio);
    return box_h >= min_height && (box_w * box_h) >= min_area;
}

bool VisionTargetSelector::passes_confidence_gate(
    float conf,
    bool tracking_candidate,
    bool enemy_colored) const {
    if (tracking_candidate) {
        return conf >= kTrackingConfidenceThreshold;
    }
    return conf >= (enemy_colored ? kPickupEnemyConfidenceThreshold : kPickupConfidenceThreshold);
}

std::optional<VisionTargetSelector::Candidate> VisionTargetSelector::build_candidate(
    const Detection& detection,
    const std::optional<std::pair<float, float>>& last_target_center) const {
    const Rect box = to_rect(detection);
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return std::nullopt;
    }
    if (detection.is_friendly) {
        return std::nullopt;
    }

    const auto point = target_point(box);
    Candidate observed;
    observed.target_x = point.first;
    observed.target_y = point.second;
    observed.conf = detection.conf;
    observed.color_bonus = detection.color_bonus;
    observed.body_box = box;
    observed.slow_zone = fallback_slow_zone(box);
    observed.fire_zone = fire_zone(box);
    observed.source = "observed";

    const auto reconstructed = try_reconstruct(observed);
    const Candidate candidate = reconstructed.value_or(observed);
    const bool tracking_candidate = tracking_distance(
        candidate.target_x,
        candidate.target_y,
        last_target_center).has_value();
    const float candidate_w = rect_width(candidate.body_box);
    const float candidate_h = rect_height(candidate.body_box);
    if (!passes_geometry_gate(candidate_w, candidate_h, tracking_candidate)) {
        return std::nullopt;
    }
    if (!passes_confidence_gate(detection.conf, tracking_candidate, detection.color_bonus > 0.0f)) {
        return std::nullopt;
    }

    return candidate;
}

std::vector<VisionTargetSelector::Candidate> VisionTargetSelector::build_candidates(
    const DetectionBatch& batch,
    const std::optional<std::pair<float, float>>& last_target_center) const {
    std::vector<Candidate> candidates;
    candidates.reserve(batch.detections.size());
    for (const auto& detection : batch.detections) {
        const auto candidate = build_candidate(detection, last_target_center);
        if (candidate.has_value()) {
            candidates.push_back(*candidate);
        }
    }
    return candidates;
}

float VisionTargetSelector::crosshair_distance(float x, float y) const {
    return std::hypot(x - screen_center_x_, y - screen_center_y_);
}

std::optional<float> VisionTargetSelector::tracking_distance(
    float x,
    float y,
    const std::optional<std::pair<float, float>>& last_target_center) const {
    if (!last_target_center.has_value()) {
        return std::nullopt;
    }
    const float distance = point_distance({x, y}, *last_target_center);
    if (distance >= tracking_radius_) {
        return std::nullopt;
    }
    return distance;
}

float VisionTargetSelector::tracking_bonus_for_distance(const std::optional<float>& distance) const {
    if (!distance.has_value()) {
        return 0.0f;
    }
    const float proximity = 1.0f - (*distance / tracking_radius_);
    return kTrackingBonus * proximity;
}

std::optional<float> VisionTargetSelector::cue_alignment_score(
    const Candidate& candidate,
    const std::optional<CueSeed>& cue_seed) const {
    if (!cue_seed.has_value() || cue_seed->score <= 0.0f) {
        return std::nullopt;
    }

    const float box_w = rect_width(candidate.body_box);
    const float box_h = rect_height(candidate.body_box);
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return std::nullopt;
    }

    const float body_center_x = (candidate.body_box.left + candidate.body_box.right) * 0.5f;
    const float expected_cue_y = candidate.body_box.top - (box_h * kCueSeedPreferredYOffsetRatio);
    const float dx_limit = std::max(10.0f, box_w * kCueSeedMaxXRatio);
    const float above_limit = std::max(12.0f, box_h * kCueSeedAboveHeadRatio);
    const float below_limit = std::max(8.0f, box_h * kCueSeedBelowHeadRatio);

    const float dx = std::fabs(cue_seed->x - body_center_x);
    if (dx > dx_limit) {
        return std::nullopt;
    }

    const float cue_y_delta = cue_seed->y - expected_cue_y;
    const float dy_limit = cue_y_delta >= 0.0f ? below_limit : above_limit;
    if (std::fabs(cue_y_delta) > dy_limit) {
        return std::nullopt;
    }

    const float normalized_dx = dx / dx_limit;
    const float normalized_dy = std::fabs(cue_y_delta) / dy_limit;
    const float geometric_score =
        std::max(0.0f, 1.0f - (normalized_dx * 0.65f) - (normalized_dy * 0.35f));
    if (geometric_score <= 0.0f) {
        return std::nullopt;
    }

    return geometric_score * kCueSeedBonus * cue_seed->score;
}

bool VisionTargetSelector::prefer_candidate(
    const std::optional<ScoredCandidate>& current,
    const ScoredCandidate& challenger) const {
    if (!current.has_value()) {
        return true;
    }

    if (challenger.cue_aligned != current->cue_aligned) {
        return challenger.cue_aligned;
    }
    if (challenger.cue_aligned && current->cue_aligned) {
        if (std::fabs(challenger.cue_score - current->cue_score) > 0.001f) {
            return challenger.cue_score > current->cue_score;
        }
    }

    const float current_crosshair_distance = crosshair_distance(
        current->candidate.target_x,
        current->candidate.target_y);
    const float challenger_crosshair_distance = crosshair_distance(
        challenger.candidate.target_x,
        challenger.candidate.target_y);

    if (challenger_crosshair_distance < (current_crosshair_distance - crosshair_priority_margin_)) {
        return true;
    }
    if (current_crosshair_distance < (challenger_crosshair_distance - crosshair_priority_margin_)) {
        return false;
    }
    return challenger.score > current->score;
}

VisionTargetSelector::ScoredCandidate VisionTargetSelector::score_candidate(
    const Candidate& candidate,
    const std::optional<std::pair<float, float>>& last_target_center,
    const std::optional<CueSeed>& cue_seed) const {
    const float half_w = frame_width_ * 0.5f;
    const float half_h = frame_height_ * 0.5f;
    const float norm_dx = (candidate.target_x - screen_center_x_) / half_w;
    const float norm_dy = (candidate.target_y - screen_center_y_) / half_h;
    float score =
        (-std::hypot(norm_dx, norm_dy) * kDistanceScoreScale)
        + candidate.color_bonus
        + (candidate.conf * kConfidenceScoreScale);

    const float area = rect_width(candidate.body_box) * rect_height(candidate.body_box);
    if (area > max_area_limit_) {
        score -= (area - max_area_limit_) * 0.1f;
    } else {
        const float area_diff = std::fabs(area - ideal_area_);
        score += (ideal_area_ - area_diff) * 0.005f;
    }

    const std::optional<float> distance = tracking_distance(
        candidate.target_x,
        candidate.target_y,
        last_target_center);
    score += tracking_bonus_for_distance(distance);

    const std::optional<float> cue_score = cue_alignment_score(candidate, cue_seed);
    score += cue_score.value_or(0.0f);

    ScoredCandidate scored;
    scored.candidate = candidate;
    scored.score = score;
    scored.has_tracking_distance = distance.has_value();
    scored.tracking_distance = distance.value_or(0.0f);
    scored.cue_aligned = cue_score.has_value();
    scored.cue_score = cue_score.value_or(0.0f);
    return scored;
}

VisionTargetSelector::TargetState VisionTargetSelector::target_from_candidate(
    const Candidate& candidate,
    float score) const {
    TargetState target;
    target.candidate = candidate;
    target.score = score;
    return target;
}

bool VisionTargetSelector::boxes_match(const Rect& lhs, const Rect& rhs) const {
    if (rect_iou(lhs, rhs) >= kActiveTargetIouThreshold) {
        return true;
    }

    const auto lhs_center = rect_center(lhs);
    const auto rhs_center = rect_center(rhs);
    const float allowed_dx = std::max(8.0f, std::max(rect_width(lhs), rect_width(rhs)) * kActiveTargetCenterXRatio);
    const float allowed_dy = std::max(8.0f, std::max(rect_height(lhs), rect_height(rhs)) * kActiveTargetCenterYRatio);
    return std::fabs(lhs_center.first - rhs_center.first) <= allowed_dx
        && std::fabs(lhs_center.second - rhs_center.second) <= allowed_dy;
}

bool VisionTargetSelector::targets_match(const TargetState& lhs, const TargetState& rhs) const {
    if (boxes_match(lhs.candidate.body_box, rhs.candidate.body_box)) {
        return true;
    }
    return point_distance(
        {lhs.candidate.target_x, lhs.candidate.target_y},
        {rhs.candidate.target_x, rhs.candidate.target_y}) <= pickup_confirm_radius_;
}

bool VisionTargetSelector::active_target_matches_candidate(
    const Candidate& candidate,
    const std::optional<TargetState>& active_target) const {
    if (!active_target.has_value()) {
        return false;
    }

    if (boxes_match(active_target->candidate.body_box, candidate.body_box)) {
        return true;
    }

    return point_distance(
        {candidate.target_x, candidate.target_y},
        {active_target->candidate.target_x, active_target->candidate.target_y}) <= pickup_confirm_radius_;
}

bool VisionTargetSelector::should_switch_targets(
    const TargetState& locked,
    const TargetState& challenger) const {
    if (challenger.score >= (locked.score + kActiveTargetScoreSwitchMargin)) {
        return true;
    }

    const float locked_distance = crosshair_distance(
        locked.candidate.target_x,
        locked.candidate.target_y);
    const float challenger_distance = crosshair_distance(
        challenger.candidate.target_x,
        challenger.candidate.target_y);
    return challenger_distance < (locked_distance - switch_crosshair_margin_);
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::confirm_pickup(
    const TargetState& target) {
    if (kPickupConfirmFrames <= 1) {
        return target;
    }

    if (!pending_target_.has_value() || !targets_match(*pending_target_, target)) {
        pending_target_ = target;
        pending_frames_ = 1;
        return std::nullopt;
    }

    pending_target_ = target;
    pending_frames_ += 1;
    if (pending_frames_ < kPickupConfirmFrames) {
        return std::nullopt;
    }

    clear_pending();
    return target;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::confirm_switch(
    const TargetState& target) {
    if (kSwitchConfirmFrames <= 1) {
        return target;
    }

    if (!pending_switch_target_.has_value() || !targets_match(*pending_switch_target_, target)) {
        pending_switch_target_ = target;
        pending_switch_frames_ = 1;
        return std::nullopt;
    }

    pending_switch_target_ = target;
    pending_switch_frames_ += 1;
    if (pending_switch_frames_ < kSwitchConfirmFrames) {
        return std::nullopt;
    }

    clear_switch_pending();
    return target;
}

void VisionTargetSelector::clear_pending() {
    pending_target_.reset();
    pending_frames_ = 0;
}

void VisionTargetSelector::clear_switch_pending() {
    pending_switch_target_.reset();
    pending_switch_frames_ = 0;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::commit_target(
    const TargetState& target,
    bool clear_switch_pending_flag) {
    std::optional<TargetState> committed = target;
    if (!active_target_.has_value()) {
        committed = confirm_pickup(target);
        if (!committed.has_value()) {
            return std::nullopt;
        }
    } else {
        clear_pending();
    }

    if (clear_switch_pending_flag) {
        clear_switch_pending();
    }

    active_target_ = *committed;
    hold_frames_ = 0;
    last_target_center_ = {
        active_target_->candidate.target_x,
        active_target_->candidate.target_y,
    };
    return active_target_;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::select_single_candidate(
    const Candidate& candidate) const {
    return target_from_candidate(candidate, candidate.color_bonus + (candidate.conf * kConfidenceScoreScale));
}

std::pair<std::optional<VisionTargetSelector::TargetState>, std::optional<VisionTargetSelector::TargetState>>
VisionTargetSelector::select_multi_candidate(
    const std::vector<Candidate>& candidates,
    const std::optional<std::pair<float, float>>& last_target_center,
    const std::optional<TargetState>& active_target,
    const std::optional<CueSeed>& cue_seed) const {
    std::optional<ScoredCandidate> best;
    std::optional<ScoredCandidate> tracked;
    std::optional<std::pair<float, ScoredCandidate>> active_match;

    for (const auto& candidate : candidates) {
        const ScoredCandidate scored = score_candidate(candidate, last_target_center, cue_seed);
        if (prefer_candidate(best, scored)) {
            best = scored;
        }

        if (scored.has_tracking_distance) {
            if (!tracked.has_value()
                || scored.tracking_distance < tracked->tracking_distance
                || (std::fabs(scored.tracking_distance - tracked->tracking_distance) < 0.001f
                    && scored.score > tracked->score)) {
                tracked = scored;
            }
        }

        if (active_target_matches_candidate(candidate, active_target)) {
            const float active_distance = point_distance(
                {candidate.target_x, candidate.target_y},
                {active_target->candidate.target_x, active_target->candidate.target_y});
            if (!active_match.has_value()
                || active_distance < active_match->first
                || (std::fabs(active_distance - active_match->first) < 0.001f
                    && scored.score > active_match->second.score)) {
                active_match = std::make_pair(active_distance, scored);
            }
        }
    }

    if (!best.has_value() || best->score <= kMinScoreThreshold) {
        return {std::nullopt, std::nullopt};
    }

    if (!active_target.has_value()
        && tracked.has_value()
        && (best->candidate.target_x != tracked->candidate.target_x
            || best->candidate.target_y != tracked->candidate.target_y)
        && best->score < (tracked->score + kTrackingSwitchMargin)) {
        best = tracked;
    }

    const std::optional<TargetState> chosen_target = target_from_candidate(best->candidate, best->score);
    const std::optional<TargetState> active_match_target = active_match.has_value()
        ? std::optional<TargetState>(target_from_candidate(active_match->second.candidate, active_match->second.score))
        : std::nullopt;
    return {chosen_target, active_match_target};
}

std::pair<std::optional<VisionTargetSelector::TargetState>, std::optional<VisionTargetSelector::TargetState>>
VisionTargetSelector::select_candidate_targets(
    const std::vector<Candidate>& candidates,
    const std::optional<std::pair<float, float>>& last_target_center,
    const std::optional<TargetState>& active_target,
    const std::optional<CueSeed>& cue_seed) const {
    if (candidates.empty()) {
        return {std::nullopt, std::nullopt};
    }
    if (candidates.size() == 1) {
        const auto chosen = select_single_candidate(candidates.front());
        const auto active_match = (chosen.has_value() && active_target_matches_candidate(candidates.front(), active_target))
            ? chosen
            : std::nullopt;
        return {chosen, active_match};
    }
    return select_multi_candidate(candidates, last_target_center, active_target, cue_seed);
}

std::pair<std::optional<VisionTargetSelector::TargetState>, bool>
VisionTargetSelector::resolve_active_target_transition(
    const TargetState& chosen_target,
    const std::optional<TargetState>& active_match_target,
    const std::optional<TargetState>& active_target) {
    if (!active_target.has_value()) {
        clear_switch_pending();
        return {chosen_target, false};
    }

    if (active_match_target.has_value()) {
        if (!targets_match(chosen_target, *active_match_target)) {
            if (should_switch_targets(*active_match_target, chosen_target)) {
                const auto confirmed_switch = confirm_switch(chosen_target);
                if (!confirmed_switch.has_value()) {
                    return {*active_match_target, true};
                }
                return {*confirmed_switch, false};
            }
            clear_switch_pending();
            return {*active_match_target, false};
        }

        clear_switch_pending();
        return {*active_match_target, false};
    }

    const auto confirmed_switch = confirm_switch(chosen_target);
    if (!confirmed_switch.has_value()) {
        return {std::nullopt, false};
    }
    return {*confirmed_switch, false};
}

bool VisionTargetSelector::fails_tracking_jump(
    const std::pair<float, float>& point,
    const std::optional<std::pair<float, float>>& last_target_center) const {
    if (!last_target_center.has_value()) {
        return false;
    }
    const float dx = point.first - last_target_center->first;
    const float dy = point.second - last_target_center->second;
    return std::fabs(dx) > max_jump_x_ || std::fabs(dy) > max_jump_y_;
}

bool VisionTargetSelector::fails_first_pickup_flick(const std::pair<float, float>& point) const {
    const float flick_dx = point.first - screen_center_x_;
    const float flick_dy = point.second - screen_center_y_;
    return std::fabs(flick_dx) >= max_jump_x_ || std::fabs(flick_dy) >= max_jump_y_;
}

std::pair<float, float> VisionTargetSelector::smooth_target_point(
    const std::pair<float, float>& point,
    const std::optional<std::pair<float, float>>& last_target_center) const {
    if (!last_target_center.has_value()) {
        return point;
    }

    const float jump = point_distance(point, *last_target_center);
    if (jump <= 0.0f || jump >= max_smoothing_jump_) {
        return point;
    }

    const float alpha = std::max(kMinSmoothingAlpha, jump / max_smoothing_jump_);
    return {
        last_target_center->first + ((point.first - last_target_center->first) * alpha),
        last_target_center->second + ((point.second - last_target_center->second) * alpha),
    };
}

VisionResult VisionTargetSelector::hold_or_reset(
    float boxes_seen,
    const std::optional<TargetState>& active_target) {
    clear_pending();
    if (!active_target_.has_value() || !active_target.has_value()) {
        clear_tracking_state();
        VisionResult result = empty_result(boxes_seen);
        result.auto_fire = update_auto_fire(nullptr);
        return result;
    }

    if (hold_frames_ < kTargetHoldFrames) {
        hold_frames_ += 1;
        VisionResult result = result_from_target(*active_target, boxes_seen);
        result.target_source = kSelectorHoldSource;
        result.auto_fire = update_auto_fire(&*active_target_);
        return result;
    }

    clear_tracking_state();
    VisionResult result = empty_result(boxes_seen);
    result.auto_fire = update_auto_fire(nullptr);
    return result;
}

VisionResult VisionTargetSelector::finalize_selected_target(
    const TargetState& chosen_target,
    const std::optional<std::pair<float, float>>& last_target_center,
    const std::optional<TargetState>& active_target,
    float boxes_seen,
    bool preserve_switch_pending,
    float sample_timestamp) {
    const std::pair<float, float> chosen_point = {
        chosen_target.candidate.target_x,
        chosen_target.candidate.target_y,
    };

    if (!last_target_center.has_value() && fails_first_pickup_flick(chosen_point)) {
        clear_pending();
        clear_switch_pending();
        return empty_result(boxes_seen);
    }

    if (fails_tracking_jump(chosen_point, last_target_center)) {
        clear_switch_pending();
        return hold_or_reset(boxes_seen, active_target);
    }

    const auto smoothed_point = smooth_target_point(chosen_point, last_target_center);
    TargetState smoothed_target = chosen_target;
    smoothed_target.candidate.target_x = smoothed_point.first;
    smoothed_target.candidate.target_y = smoothed_point.second;

    const auto committed = commit_target(smoothed_target, !preserve_switch_pending);
    if (!committed.has_value()) {
        return empty_result(boxes_seen);
    }
    record_observation(*committed, sample_timestamp);
    VisionResult result = result_from_target(*committed, boxes_seen);
    result.auto_fire = update_auto_fire(&*committed);
    return result;
}

VisionResult VisionTargetSelector::select(
    const DetectionBatch& batch,
    const EgoWarp& ego_warp,
    const std::optional<CueSeed>& cue_seed) {
    const float sample_timestamp = sample_clock_ += 1.0f;
    const float boxes_seen = static_cast<float>(batch.detections.size());
    const auto last_target_center = last_target_center_.has_value()
        ? std::optional<std::pair<float, float>>(apply_ego_warp(ego_warp, *last_target_center_))
        : std::nullopt;
    const auto active_target = active_target_.has_value()
        ? std::optional<TargetState>(warp_target_state(ego_warp, *active_target_))
        : std::nullopt;
    const auto candidates = build_candidates(batch, last_target_center);
    if (candidates.empty()) {
        clear_tracking_state();
        VisionResult result = empty_result(boxes_seen);
        result.auto_fire = update_auto_fire(nullptr);
        return result;
    }

    const auto selected = select_candidate_targets(candidates, last_target_center, active_target, cue_seed);
    if (!selected.first.has_value()) {
        return hold_or_reset(boxes_seen, active_target);
    }

    const auto transition = resolve_active_target_transition(*selected.first, selected.second, active_target);
    if (!transition.first.has_value()) {
        return hold_or_reset(boxes_seen, active_target);
    }

    return finalize_selected_target(
        *transition.first,
        last_target_center,
        active_target,
        boxes_seen,
        transition.second,
        sample_timestamp);
}

} // namespace vision_native
