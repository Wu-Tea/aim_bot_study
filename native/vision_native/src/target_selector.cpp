#include "vision_native/target_selector.h"

#include "pipeline_contract/target_acquisition.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cmath>
#include <limits>
#include <utility>

namespace vision_native {
namespace {

constexpr float kChestTargetRatio = 0.40f;
constexpr float kCrouchedTargetRatio = 0.40f;
constexpr float kWideLowTargetRatio = 0.65f;
constexpr float kAimRegionShrinkX = 0.22f;
constexpr float kAimRegionHalfHeightRatio = 0.18f;
constexpr float kWideLowAimRegionHalfHeightRatio = 0.22f;
constexpr float kFireShrinkX = 0.12f;
constexpr float kFireShrinkTop = 0.05f;
constexpr float kFireShrinkBottom = 0.15f;
constexpr float kMinPickupHeightRatio = 0.08f;
constexpr float kMinTrackingHeightRatio = 0.06f;
constexpr float kMinPickupAreaRatio = 0.003f;
constexpr float kMinTrackingAreaRatio = 0.002f;
constexpr float kMinAspectRatio = 0.85f;
constexpr float kMinWideLowAspectRatio = 0.30f;
constexpr float kWideLowAspectThreshold = 0.65f;
constexpr float kCrouchedHeightRatio = 0.24f;
constexpr float kMaxAspectRatio = 4.50f;
constexpr float kConfidenceScoreScale = 400.0f;
constexpr float kTrackingSwitchMargin = 80.0f;
constexpr float kDistanceScoreScale = 800.0f;
constexpr float kTargetHeightScoreScale = 800.0f;
constexpr float kTrackingRadiusRatio = 120.0f / 640.0f;
constexpr float kPickupConfirmRadiusRatio = 32.0f / 640.0f;
constexpr float kMaxAreaLimitRatio = 40000.0f / (640.0f * 640.0f);
constexpr int kPickupConfirmFrames = 2;
constexpr int kActiveIdentityMissFramesBeforeInvalidation = 2;
constexpr float kActiveTargetIouThreshold = 0.12f;
constexpr float kActiveTargetCenterXRatio = 0.65f;
constexpr float kActiveTargetCenterYRatio = 0.35f;
constexpr float kCrosshairPriorityMarginRatio = 10.0f / 640.0f;
constexpr float kStaleActiveHeightRetainRatio = 0.75f;
constexpr float kStaleActiveSwitchConfidenceSlack = 0.05f;
constexpr float kStaleActiveSwitchRadiusScale = 1.15f;
constexpr float kStaleActiveCueSwitchRadiusScale = 1.60f;
constexpr float kPickupConfidenceThreshold = 0.65f;
constexpr float kPickupEnemyConfidenceThreshold = 0.42f;
constexpr float kTrackingConfidenceThreshold = 0.40f;
constexpr float kWeakAssociationConfidenceThreshold = 0.20f;
constexpr float kTrackingBonus = 2000.0f;
constexpr float kMinScoreThreshold = -50000.0f;
constexpr float kMaxColorBonus = 10000.0f;
constexpr float kFriendlyMaskMinRatio = 0.02f;
constexpr float kFriendlyMaskMaxRatio = 0.35f;
constexpr float kEnemyMaskMinRatio = 0.03f;
constexpr float kEnemyMaskMaxRatio = 0.45f;
constexpr int kCueMinPixels = 8;
constexpr int kCueHoldMinPixels = 6;
// VisionTargetSelector only receives frames while the runtime is aiming.  A
// directly observed person+cue pair may therefore lend the cue bounded ADS
// continuation authority, but never cue-only pickup or fire authority.
constexpr std::uint64_t kCueHoldEvidenceGapNs = 50'000'000ull;
// Product-contract safety ceiling. Continuous fresh cue evidence may bridge
// only a short direct-person occlusion; it is not a one-second target hold.
constexpr std::uint64_t kCueHoldMaxDurationNs = 180'000'000ull;
constexpr int kMaxCueHoldFallbackFrames = 12;
// Cue continuation is a current-frame color observation, not a prediction
// from the previous person point.  Keep one bounded window large enough for a
// close target to move between 100-200 Hz Vision results.
constexpr int kCueHoldSearchRadius = 42;
constexpr int kCueHoldSearchDiameter = (kCueHoldSearchRadius * 2) + 1;
constexpr int kCueHoldSearchCapacity =
    kCueHoldSearchDiameter * kCueHoldSearchDiameter;
// A cue can move with the retained person, but a reconstructed point that
// jumps farther than this from the current same-generation point is not a
// valid continuation.  Reject it before it can become aim authority.
constexpr float kCueMaxReconstructedTargetResidualPx = 36.0f;
// Yellow glyphs can have one-pixel antialiasing holes.  A two-pixel component
// link is the local equivalent of a tiny morphology close, without adding an
// OpenCV dependency or allocating on the Vision hot path.
constexpr int kCueComponentLinkRadius = 2;
constexpr float kCueOffsetSmoothingAlpha = 0.35f;
constexpr float kAutoFireEdgePadding = 2.0f;
constexpr int kAutoFireReleaseGraceFrames = 4;
constexpr float kIntentMinStrength = 0.05f;
constexpr float kIntentScoreScale = 700.0f;
constexpr float kIntentPickupConfidenceThreshold = 0.50f;
constexpr float kIntentPickupScoreThreshold = kIntentScoreScale * 0.85f;
constexpr float kHandoverCapturedRadiusPx = 24.0f;
constexpr float kHandoverActiveIntentRatio = 0.75f;
constexpr float kWeakObservedScorePenalty = 1200.0f;
constexpr float kTargetValidityScoreScale = 250.0f;
constexpr float kCorpseRiskScoreScale = 650.0f;
constexpr float kCorpseRejectRiskThreshold = 0.85f;
constexpr float kCorpseRejectLiveScoreThreshold = 0.75f;
constexpr float kWeakObservedRiskThreshold = 0.50f;

struct IntentScore {
    bool applied = false;
    const char* decision = "none";
    float bonus = 0.0f;
};

struct TargetEvidence {
    float live_score = 1.0f;
    float corpse_risk = 0.0f;
    float uncertainty = 0.0f;
    bool reject = false;
    bool weak_only = false;
};

bool source_equals(const char* lhs, const char* rhs) {
    return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
}

const char* target_tier_for_source(const char* source) {
    if (source_equals(source, "observed")) {
        return "observed_strong";
    }
    if (source_equals(source, "associated_weak") || source_equals(source, "weak_observed")) {
        return "associated_weak";
    }
    if (source_equals(source, "cue_hold")) {
        return "cue_hold";
    }
    return "unknown";
}

bool aim_authority_for_source(const char* source) {
    return source_equals(source, "observed") ||
        source_equals(source, "associated_weak") ||
        source_equals(source, "weak_observed") ||
        source_equals(source, "cue_hold");
}

bool fire_authority_for_source(const char* source) {
    return source_equals(source, "observed");
}

struct ColorClassification {
    float color_bonus = 0.0f;
    bool is_friendly = false;
    bool has_cue = false;
    float cue_x = 0.0f;
    float cue_y = 0.0f;
    float cue_score = 0.0f;
};

struct YellowCueObservation {
    bool found = false;
    float cue_x = 0.0f;
    float cue_y = 0.0f;
    float score = 0.0f;
    int pixels = 0;
};

using IntRect = VisionTargetSelector::FrameRegion;

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

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

std::optional<std::pair<float, float>> normalized_vector(float x, float y) {
    const float length = std::hypot(x, y);
    if (length <= 0.001f) {
        return std::nullopt;
    }
    return std::make_pair(x / length, y / length);
}

float aspect_ratio_h_over_w(float box_w, float box_h) {
    return box_w > 0.0f ? (box_h / box_w) : 0.0f;
}

bool is_wide_low_pose(float box_w, float box_h) {
    return aspect_ratio_h_over_w(box_w, box_h) < kWideLowAspectThreshold;
}

bool is_crouched_pose(float box_w, float box_h, int frame_height) {
    if (frame_height <= 0) {
        return false;
    }
    return aspect_ratio_h_over_w(box_w, box_h) < 1.0f
        && (box_h / static_cast<float>(frame_height)) <= kCrouchedHeightRatio;
}

bool has_enemy_cue_evidence(const Detection& detection) {
    return detection.has_cue_point || detection.color_bonus > 0.0f;
}

bool is_color_checked_without_enemy_cue(const Detection& detection) {
    return detection.color_classified && !has_enemy_cue_evidence(detection);
}

bool is_color_checked_wide_low_without_enemy_cue(
    const Detection& detection,
    float box_w,
    float box_h) {
    return is_color_checked_without_enemy_cue(detection)
        && is_wide_low_pose(box_w, box_h);
}

TargetEvidence evaluate_target_evidence(
    const Detection& detection,
    float box_w,
    float box_h,
    bool tracking_candidate,
    bool active_match,
    bool active_had_enemy_evidence) {
    const bool wide_low_without_enemy_cue =
        is_color_checked_wide_low_without_enemy_cue(detection, box_w, box_h);
    const bool checked_missing_enemy_cue = is_color_checked_without_enemy_cue(detection);

    TargetEvidence evidence;
    evidence.live_score = clamp01(detection.conf);
    evidence.uncertainty = detection.color_classified ? 0.15f : 0.35f;

    if (has_enemy_cue_evidence(detection)) {
        evidence.live_score += 0.30f;
        evidence.uncertainty -= 0.10f;
    }
    if (tracking_candidate) {
        evidence.live_score += 0.10f;
        evidence.uncertainty -= 0.05f;
    }
    if (active_match) {
        evidence.live_score += 0.05f;
    }
    if (checked_missing_enemy_cue) {
        evidence.uncertainty += 0.20f;
    }
    if (wide_low_without_enemy_cue) {
        evidence.live_score -= 0.25f;
        evidence.corpse_risk += 0.60f;
        evidence.uncertainty += 0.25f;
    }
    if (active_match && active_had_enemy_evidence && checked_missing_enemy_cue) {
        evidence.live_score -= 0.20f;
        evidence.corpse_risk += 0.30f;
    }
    if (active_match && wide_low_without_enemy_cue) {
        evidence.corpse_risk += 0.08f;
    }

    evidence.live_score = clamp01(evidence.live_score);
    evidence.corpse_risk = clamp01(evidence.corpse_risk);
    evidence.uncertainty = clamp01(evidence.uncertainty);
    evidence.reject = evidence.corpse_risk >= kCorpseRejectRiskThreshold
        && evidence.live_score < kCorpseRejectLiveScoreThreshold;
    evidence.weak_only = evidence.corpse_risk >= kWeakObservedRiskThreshold;
    return evidence;
}

VisionTargetSelector::Rect shift_rect(
    const VisionTargetSelector::Rect& rect,
    float dx,
    float dy) {
    return {
        rect.left + dx,
        rect.top + dy,
        rect.right + dx,
        rect.bottom + dy,
    };
}

std::optional<IntRect> color_roi_bounds(
    const VisionTargetSelector::Rect& box,
    int frame_width,
    int frame_height) {
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    const float cx = (box.left + box.right) * 0.5f;
    const bool wide_low = is_wide_low_pose(box_w, box_h);
    const int roi_h = wide_low
        ? static_cast<int>(std::max(12.0f, std::min(32.0f, box_h * 0.35f)))
        : static_cast<int>(std::max(12.0f, std::min(36.0f, box_h * 0.20f)));
    const int roi_w = wide_low
        ? static_cast<int>(std::max(32.0f, std::min(120.0f, box_w * 0.70f)))
        : static_cast<int>(std::max(24.0f, std::min(80.0f, box_w * 0.80f)));
    const int roi_top = wide_low
        ? std::max(0, std::min(frame_height, static_cast<int>(box.top + (box_h * 0.05f))))
        : std::max(0, std::min(frame_height, static_cast<int>(box.top) - 2)) - roi_h;
    const int roi_bottom = wide_low
        ? std::min(frame_height, roi_top + roi_h)
        : std::max(0, std::min(frame_height, static_cast<int>(box.top) - 2));
    const int clamped_roi_top = std::max(0, roi_top);
    const int roi_left = std::max(0, static_cast<int>(cx - (static_cast<float>(roi_w) * 0.5f)));
    const int roi_right = std::min(frame_width, static_cast<int>(cx + (static_cast<float>(roi_w) * 0.5f)));

    if ((roi_bottom - clamped_roi_top) < 4 || (roi_right - roi_left) < 4) {
        return std::nullopt;
    }
    return IntRect{roi_left, clamped_roi_top, roi_right, roi_bottom};
}

std::optional<IntRect> motion_anchor_roi_bounds(
    const VisionTargetSelector::Rect& box,
    int frame_width,
    int frame_height) {
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    if (box_w < 8.0f || box_h < 12.0f) {
        return std::nullopt;
    }
    const bool wide_low = is_wide_low_pose(box_w, box_h);
    const float left_ratio = wide_low ? 0.10f : 0.14f;
    const float right_ratio = wide_low ? 0.90f : 0.86f;
    const float top_ratio = wide_low ? 0.08f : 0.05f;
    const float bottom_ratio = wide_low ? 0.72f : 0.55f;
    const IntRect bounds{
        std::clamp(
            static_cast<int>(std::floor(box.left + box_w * left_ratio)),
            0, frame_width),
        std::clamp(
            static_cast<int>(std::floor(box.top + box_h * top_ratio)),
            0, frame_height),
        std::clamp(
            static_cast<int>(std::ceil(box.left + box_w * right_ratio)),
            0, frame_width),
        std::clamp(
            static_cast<int>(std::ceil(box.top + box_h * bottom_ratio)),
            0, frame_height),
    };
    if (bounds.right - bounds.left < 6 ||
        bounds.bottom - bounds.top < 6) {
        return std::nullopt;
    }
    return bounds;
}

int effective_frame_width(const VisionTargetSelector::ColorFrameView& frame) {
    return frame.frame_width > 0 ? frame.frame_width : frame.width;
}

int effective_frame_height(const VisionTargetSelector::ColorFrameView& frame) {
    return frame.frame_height > 0 ? frame.frame_height : frame.height;
}

bool frame_covers(const IntRect& bounds, const VisionTargetSelector::ColorFrameView& frame) {
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return false;
    }

    return bounds.left >= frame.origin_x
        && bounds.top >= frame.origin_y
        && bounds.right <= (frame.origin_x + frame.width)
        && bounds.bottom <= (frame.origin_y + frame.height);
}

bool rect_contains_point(const IntRect& bounds, float x, float y) {
    return x >= static_cast<float>(bounds.left)
        && x < static_cast<float>(bounds.right)
        && y >= static_cast<float>(bounds.top)
        && y < static_cast<float>(bounds.bottom);
}

void read_rgb(
    const VisionTargetSelector::ColorFrameView& frame,
    int x,
    int y,
    int& r,
    int& g,
    int& b) {
    const int local_x = x - frame.origin_x;
    const int local_y = y - frame.origin_y;
    assert(frame.data != nullptr);
    assert(local_x >= 0 && local_x < frame.width);
    assert(local_y >= 0 && local_y < frame.height);
    assert(frame.row_pitch > 0);
    const uint8_t* pixel = frame.data + (static_cast<size_t>(local_y) * frame.row_pitch);
    if (frame.format == PixelFormat::BGRA8) {
        assert(frame.row_pitch >= frame.width * 4);
        pixel += static_cast<size_t>(local_x) * 4;
        b = pixel[0];
        g = pixel[1];
        r = pixel[2];
        return;
    }

    assert(frame.row_pitch >= frame.width * 3);
    pixel += static_cast<size_t>(local_x) * 3;
    r = pixel[0];
    g = pixel[1];
    b = pixel[2];
}

constexpr int kMotionPatchWidth = 10;
constexpr int kMotionPatchHeight = 10;
constexpr int kMotionPatchSamples =
    kMotionPatchWidth * kMotionPatchHeight;

bool sample_motion_patch(
    const VisionTargetSelector::ColorFrameView& frame,
    float center_x,
    float center_y,
    int spacing,
    std::array<float, kMotionPatchSamples>& patch) {
    const int origin_x = static_cast<int>(std::lround(center_x)) -
        ((kMotionPatchWidth - 1) * spacing) / 2;
    const int origin_y = static_cast<int>(std::lround(center_y)) -
        ((kMotionPatchHeight - 1) * spacing) / 2;
    const IntRect bounds{
        origin_x,
        origin_y,
        origin_x + (kMotionPatchWidth - 1) * spacing + 1,
        origin_y + (kMotionPatchHeight - 1) * spacing + 1,
    };
    if (!frame_covers(bounds, frame)) {
        return false;
    }
    std::size_t index = 0;
    for (int row = 0; row < kMotionPatchHeight; ++row) {
        for (int column = 0; column < kMotionPatchWidth; ++column) {
            int r = 0;
            int g = 0;
            int b = 0;
            read_rgb(
                frame,
                origin_x + column * spacing,
                origin_y + row * spacing,
                r, g, b);
            patch[index++] =
                0.299f * static_cast<float>(r) +
                0.587f * static_cast<float>(g) +
                0.114f * static_cast<float>(b);
        }
    }
    return true;
}

float normalized_patch_correlation(
    const std::array<float, kMotionPatchSamples>& lhs,
    const std::array<float, kMotionPatchSamples>& rhs) {
    float lhs_mean = 0.0f;
    float rhs_mean = 0.0f;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        lhs_mean += lhs[index];
        rhs_mean += rhs[index];
    }
    lhs_mean /= static_cast<float>(lhs.size());
    rhs_mean /= static_cast<float>(rhs.size());
    double numerator = 0.0;
    double lhs_energy = 0.0;
    double rhs_energy = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const double a = lhs[index] - lhs_mean;
        const double b = rhs[index] - rhs_mean;
        numerator += a * b;
        lhs_energy += a * a;
        rhs_energy += b * b;
    }
    if (lhs_energy < 1.0 || rhs_energy < 1.0) {
        return -1.0f;
    }
    return static_cast<float>(
        numerator / std::sqrt(lhs_energy * rhs_energy));
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

bool in_hsv_range(
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

IntentScore score_intent(
    const VisionTargetSelector::Candidate& candidate,
    float screen_center_x,
    float screen_center_y,
    float frame_width,
    float frame_height,
    const pipeline_contract::UserAimIntent* intent) {
    if (intent == nullptr || !intent->valid || !intent->aiming ||
        intent->purpose ==
            pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget) {
        return {};
    }

    const float strength = clamp01(intent->strength);
    if (strength < kIntentMinStrength) {
        return {};
    }

    if (intent->has_point) {
        const float avg_dim = (frame_width + frame_height) * 0.5f;
        const float max_distance = std::max(1.0f, avg_dim * 0.45f);
        const float distance = std::hypot(
            candidate.target_x - intent->point_px.x,
            candidate.target_y - intent->point_px.y);
        const float proximity = clamp01(1.0f - (distance / max_distance));
        if (proximity <= 0.0f) {
            return {};
        }
        return IntentScore{true, "applied_point", proximity * strength * kIntentScoreScale};
    }

    if (!intent->has_direction) {
        return {};
    }

    const auto intent_direction =
        normalized_vector(intent->direction.x, intent->direction.y);
    const auto candidate_direction = normalized_vector(
        candidate.target_x - screen_center_x,
        candidate.target_y - screen_center_y);
    if (!intent_direction.has_value() || !candidate_direction.has_value()) {
        return {};
    }

    const float alignment =
        (intent_direction->first * candidate_direction->first)
        + (intent_direction->second * candidate_direction->second);
    const float positive_alignment = clamp01(alignment);
    if (positive_alignment <= 0.0f) {
        return {};
    }
    return IntentScore{
        true,
        "applied_direction",
        positive_alignment * strength * kIntentScoreScale,
    };
}

bool is_friendly_hsv(float h, float s, float v) {
    return in_hsv_range(h, s, v, 45.0f, 80.0f, 50.0f, 75.0f, 255.0f, 255.0f);
}

bool is_enemy_hsv(float h, float s, float v) {
    return in_hsv_range(h, s, v, 20.0f, 120.0f, 120.0f, 35.0f, 255.0f, 255.0f)
        || in_hsv_range(h, s, v, 0.0f, 120.0f, 80.0f, 10.0f, 255.0f, 255.0f)
        || in_hsv_range(h, s, v, 170.0f, 120.0f, 80.0f, 180.0f, 255.0f, 255.0f);
}

YellowCueObservation scan_yellow_window(
    const IntRect& bounds,
    const VisionTargetSelector::ColorFrameView& frame,
    float expected_x,
    float expected_y) {
    YellowCueObservation observation;
    if (!frame_covers(bounds, frame)) {
        return observation;
    }

    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 ||
        width > kCueHoldSearchDiameter || height > kCueHoldSearchDiameter) {
        return observation;
    }

    // 0 = not yellow, 1 = unvisited yellow, 2 = visited yellow.
    std::array<std::uint8_t, kCueHoldSearchCapacity> mask{};
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            read_rgb(frame, x, y, r, g, b);

            float h = 0.0f;
            float s = 0.0f;
            float v = 0.0f;
            rgb_to_opencv_hsv(r, g, b, h, s, v);
            if (is_enemy_hsv(h, s, v)) {
                const int local_x = x - bounds.left;
                const int local_y = y - bounds.top;
                mask[static_cast<std::size_t>(local_y * width + local_x)] = 1;
            }
        }
    }

    std::array<int, kCueHoldSearchCapacity> queue{};
    float best_distance_sq = std::numeric_limits<float>::infinity();
    int best_pixels = 0;
    float best_x = 0.0f;
    float best_y = 0.0f;
    for (int start_y = 0; start_y < height; ++start_y) {
        for (int start_x = 0; start_x < width; ++start_x) {
            const int start_index = start_y * width + start_x;
            if (mask[static_cast<std::size_t>(start_index)] != 1) {
                continue;
            }

            int head = 0;
            int tail = 0;
            queue[static_cast<std::size_t>(tail++)] = start_index;
            mask[static_cast<std::size_t>(start_index)] = 2;
            int pixels = 0;
            float sum_x = 0.0f;
            float sum_y = 0.0f;
            while (head < tail) {
                const int index = queue[static_cast<std::size_t>(head++)];
                const int local_y = index / width;
                const int local_x = index - (local_y * width);
                ++pixels;
                sum_x += static_cast<float>(bounds.left + local_x);
                sum_y += static_cast<float>(bounds.top + local_y);

                for (int dy = -kCueComponentLinkRadius;
                     dy <= kCueComponentLinkRadius; ++dy) {
                    for (int dx = -kCueComponentLinkRadius;
                         dx <= kCueComponentLinkRadius; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int next_x = local_x + dx;
                        const int next_y = local_y + dy;
                        if (next_x < 0 || next_y < 0 ||
                            next_x >= width || next_y >= height) {
                            continue;
                        }
                        const int next_index = next_y * width + next_x;
                        if (mask[static_cast<std::size_t>(next_index)] != 1) {
                            continue;
                        }
                        mask[static_cast<std::size_t>(next_index)] = 2;
                        queue[static_cast<std::size_t>(tail++)] = next_index;
                    }
                }
            }

            if (pixels < kCueHoldMinPixels) {
                continue;
            }
            const float cue_x = sum_x / static_cast<float>(pixels);
            const float cue_y = sum_y / static_cast<float>(pixels);
            const float dx = cue_x - expected_x;
            const float dy = cue_y - expected_y;
            const float distance_sq = dx * dx + dy * dy;
            if (distance_sq < best_distance_sq ||
                (distance_sq == best_distance_sq && pixels > best_pixels)) {
                best_distance_sq = distance_sq;
                best_pixels = pixels;
                best_x = cue_x;
                best_y = cue_y;
            }
        }
    }

    if (best_pixels < kCueHoldMinPixels) return observation;
    observation.found = true;
    observation.cue_x = best_x;
    observation.cue_y = best_y;
    observation.pixels = best_pixels;
    observation.score = static_cast<float>(best_pixels) /
        static_cast<float>(width * height);
    return observation;
}

ColorClassification classify_color(
    const VisionTargetSelector::Rect& box,
    const VisionTargetSelector::ColorFrameView& frame) {
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return {};
    }

    ColorClassification classification;
    const auto bounds = color_roi_bounds(box, effective_frame_width(frame), effective_frame_height(frame));
    if (!bounds.has_value()) {
        return classification;
    }
    if (!frame_covers(*bounds, frame)) {
        return classification;
    }

    int friendly_count = 0;
    int cue_pixels = 0;
    float cue_sum_x = 0.0f;
    float cue_sum_y = 0.0f;
    int area = 0;
    for (int y = bounds->top; y < bounds->bottom; ++y) {
        for (int x = bounds->left; x < bounds->right; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            read_rgb(frame, x, y, r, g, b);

            float h = 0.0f;
            float s = 0.0f;
            float v = 0.0f;
            rgb_to_opencv_hsv(r, g, b, h, s, v);
            if (is_friendly_hsv(h, s, v)) {
                ++friendly_count;
            }
            if (is_enemy_hsv(h, s, v)) {
                ++cue_pixels;
                cue_sum_x += static_cast<float>(x);
                cue_sum_y += static_cast<float>(y);
            }
            ++area;
        }
    }

    if (area <= 0) {
        return classification;
    }

    const float friendly_ratio = static_cast<float>(friendly_count) / static_cast<float>(area);
    if (kFriendlyMaskMinRatio <= friendly_ratio && friendly_ratio <= kFriendlyMaskMaxRatio) {
        classification.is_friendly = true;
        return classification;
    }

    classification.has_cue = cue_pixels >= kCueMinPixels;
    if (cue_pixels > 0) {
        classification.cue_x = cue_sum_x / static_cast<float>(cue_pixels);
        classification.cue_y = cue_sum_y / static_cast<float>(cue_pixels);
        classification.cue_score = static_cast<float>(cue_pixels) / static_cast<float>(area);
    }

    const float enemy_ratio = classification.cue_score;
    if (kEnemyMaskMinRatio <= enemy_ratio && enemy_ratio <= kEnemyMaskMaxRatio) {
        classification.color_bonus = kMaxColorBonus;
    }
    return classification;
}

} // namespace

VisionTargetSelector::VisionTargetSelector(
    int frame_width,
    int frame_height,
    float pickup_base_radius_px)
    : frame_width_(static_cast<float>(frame_width)),
      frame_height_(static_cast<float>(frame_height)),
      screen_center_x_(frame_width_ * 0.5f),
      screen_center_y_(frame_height_ * 0.5f),
      pickup_base_radius_px_(std::max(0.0f, pickup_base_radius_px)) {
    const float avg_dim = (frame_width_ + frame_height_) * 0.5f;
    const float frame_area = frame_width_ * frame_height_;
    tracking_radius_ = avg_dim * kTrackingRadiusRatio;
    pickup_confirm_radius_ = avg_dim * kPickupConfirmRadiusRatio;
    crosshair_priority_margin_ = avg_dim * kCrosshairPriorityMarginRatio;
    max_area_limit_ = frame_area * kMaxAreaLimitRatio;
}

void VisionTargetSelector::reset() {
    clear_tracking_state();
    clear_auto_fire_state();
    selector_target_generation_ = 0;
    selector_target_changed_ = false;
}

void VisionTargetSelector::clear_tracking_state() {
    last_target_center_.reset();
    active_target_.reset();
    active_generation_had_enemy_evidence_ = false;
    active_marker_expired_ = false;
    active_identity_miss_frames_ = 0;
    pending_target_.reset();
    clear_cue_tracking();
    pending_frames_ = 0;
    reset_motion_anchor();
}

void VisionTargetSelector::clear_cue_tracking() {
    last_cue_point_.reset();
    last_target_offset_from_cue_.reset();
    cue_tracking_generation_ = 0;
    last_direct_cue_observation_ns_ = 0;
    last_cue_observation_ns_ = 0;
    cue_hold_frames_ = 0;
}

void VisionTargetSelector::clear_auto_fire_state() {
    auto_fire_holding_ = false;
    auto_fire_miss_frames_ = 0;
}

bool VisionTargetSelector::wants_color_frame() const {
    return active_target_.has_value() && last_cue_point_.has_value() && last_target_offset_from_cue_.has_value();
}

std::optional<VisionTargetSelector::FrameRegion> VisionTargetSelector::required_color_region(
    const DetectionBatch& batch) const {
    IntRect merged{};
    bool has_merged = false;
    const int frame_width = static_cast<int>(frame_width_);
    const int frame_height = static_cast<int>(frame_height_);
    const auto last_target_center = last_target_center_;
    const auto merge_bounds = [&](const std::optional<IntRect>& bounds) {
        if (!bounds.has_value()) return;
        if (!has_merged) {
            merged = *bounds;
            has_merged = true;
            return;
        }
        merged.left = std::min(merged.left, bounds->left);
        merged.top = std::min(merged.top, bounds->top);
        merged.right = std::max(merged.right, bounds->right);
        merged.bottom = std::max(merged.bottom, bounds->bottom);
    };

    for (const auto& detection : batch.detections) {
        if (detection.color_classified) {
            continue;
        }

        const Rect box = to_rect(detection);
        const auto point = target_point(box);
        const bool tracking_candidate = tracking_distance(
            point.first,
            point.second,
            last_target_center).has_value();
        const float candidate_w = rect_width(box);
        const float candidate_h = rect_height(box);
        if (!passes_geometry_gate(candidate_w, candidate_h, tracking_candidate)) {
            continue;
        }

        const float min_conf = tracking_candidate
            ? kTrackingConfidenceThreshold
            : kPickupEnemyConfidenceThreshold;
        if (detection.conf < min_conf) {
            continue;
        }

        merge_bounds(color_roi_bounds(box, frame_width, frame_height));
        merge_bounds(motion_anchor_roi_bounds(
            box, frame_width, frame_height));
    }

    // Keep the previously confirmed appearance anchor readable even when the
    // detector reconstructs one body-box edge around a weapon or optic.  The
    // matcher searches from this anchor, not from the reconstructed box.
    if (has_motion_template_ && has_merged) {
        constexpr int kMaximumSearchRadius = 12;
        const int patch_radius =
            ((kMotionPatchWidth - 1) * motion_template_spacing_) / 2 + 1;
        const int margin = kMaximumSearchRadius + patch_radius;
        merge_bounds(IntRect{
            std::clamp(
                static_cast<int>(std::floor(motion_anchor_x_)) - margin,
                0, frame_width),
            std::clamp(
                static_cast<int>(std::floor(motion_anchor_y_)) - margin,
                0, frame_height),
            std::clamp(
                static_cast<int>(std::ceil(motion_anchor_x_)) + margin + 1,
                0, frame_width),
            std::clamp(
                static_cast<int>(std::ceil(motion_anchor_y_)) + margin + 1,
                0, frame_height),
        });
    }

    if (has_merged) {
        return merged;
    }
    if (batch.has_external_cue) {
        return std::nullopt;
    }
    return cue_hold_search_region(batch.captured_at_ns);
}

VisionResult VisionTargetSelector::empty_result(float boxes_seen) const {
    VisionResult result;
    result.selector_identity_protocol = true;
    result.selector_target_generation = selector_target_generation_;
    result.selector_target_changed = selector_target_changed_;
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
    result.has_selected_detection = target.candidate.has_source_detection;
    result.selected_detection_index = target.candidate.source_detection_index;
    result.has_body_box = true;
    result.target_x = target.candidate.target_x;
    result.target_y = target.candidate.target_y;
    result.dx = result.target_x - result.screen_center_x;
    result.dy = result.target_y - result.screen_center_y;
    result.body_x1 = target.candidate.body_box.left;
    result.body_y1 = target.candidate.body_box.top;
    result.body_x2 = target.candidate.body_box.right;
    result.body_y2 = target.candidate.body_box.bottom;
    result.has_aim_region =
        rect_width(target.candidate.aim_region) > 1.0f &&
        rect_height(target.candidate.aim_region) > 1.0f;
    result.aim_region_x1 = target.candidate.aim_region.left;
    result.aim_region_y1 = target.candidate.aim_region.top;
    result.aim_region_x2 = target.candidate.aim_region.right;
    result.aim_region_y2 = target.candidate.aim_region.bottom;
    result.target_source = target.candidate.source;
    result.target_tier = target_tier_for_source(target.candidate.source);
    result.aim_authority = aim_authority_for_source(target.candidate.source);
    result.fire_authority = fire_authority_for_source(target.candidate.source);
    result.enemy_cue_current = candidate_has_enemy_evidence(target.candidate);
    result.enemy_identity_confirmed = active_generation_had_enemy_evidence_ ||
        result.enemy_cue_current;
    result.association_stage = target.candidate.source;
    result.target_confidence = target.candidate.conf;
    result.intent_applied = target.intent_applied;
    result.intent_decision = target.intent_decision;
    result.intent_score = target.intent_score;
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
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    float target_ratio = kChestTargetRatio;
    if (is_wide_low_pose(box_w, box_h)) {
        target_ratio = kWideLowTargetRatio;
    } else if (is_crouched_pose(box_w, box_h, frame_height_)) {
        target_ratio = kCrouchedTargetRatio;
    }

    return {
        (box.left + box.right) * 0.5f,
        box.top + (box_h * target_ratio),
    };
}

VisionTargetSelector::Rect VisionTargetSelector::fallback_aim_region(const Rect& box) const {
    // Lightweight V1 geometry: keep R around the pose-aware default point,
    // rather than relabelling most of the detector box as upper chest/head.
    // This owner can later be replaced by a calibrated body/pose estimator
    // without changing the downstream I/R/D/T control contract.
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    const auto point = target_point(box);
    const float half_height = box_h * (
        is_wide_low_pose(box_w, box_h)
            ? kWideLowAimRegionHalfHeightRatio
            : kAimRegionHalfHeightRatio);
    return {
        box.left + (box_w * kAimRegionShrinkX),
        std::max(box.top, point.second - half_height),
        box.right - (box_w * kAimRegionShrinkX),
        std::min(box.bottom, point.second + half_height),
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
        detection.has_cue_point = classification.has_cue;
        detection.cue_x = classification.cue_x;
        detection.cue_y = classification.cue_y;
        detection.cue_score = classification.cue_score;

        if (!detection.is_friendly && batch.has_external_cue) {
            const auto bounds = color_roi_bounds(
                to_rect(detection),
                effective_frame_width(frame),
                effective_frame_height(frame));
            if (bounds.has_value()
                && rect_contains_point(*bounds, batch.external_cue_x, batch.external_cue_y)) {
                detection.has_cue_point = true;
                detection.cue_x = batch.external_cue_x;
                detection.cue_y = batch.external_cue_y;
                detection.cue_score = std::max(detection.cue_score, batch.external_cue_score);
                detection.color_bonus = std::max(detection.color_bonus, kMaxColorBonus);
            }
        }
    }
    return annotated;
}

void VisionTargetSelector::reset_motion_anchor() {
    motion_template_.fill(0.0f);
    motion_template_box_ = {};
    motion_anchor_x_ = 0.0f;
    motion_anchor_y_ = 0.0f;
    motion_template_spacing_ = 1;
    motion_anchor_misses_ = 0;
    has_motion_template_ = false;
}

void VisionTargetSelector::update_selected_motion_anchor(
    Detection& detection,
    const ColorFrameView& frame) {
    detection.has_motion_anchor = false;
    detection.motion_anchor_score = 0.0f;
    const Rect box = to_rect(detection);
    const float width = rect_width(box);
    const float height = rect_height(box);
    if (width < 8.0f || height < 12.0f) {
        reset_motion_anchor();
        return;
    }

    const float expected_x = (box.left + box.right) * 0.5f;
    const float expected_y = box.top + height * 0.30f;
    const int detected_spacing = std::clamp(
        static_cast<int>(std::lround(
            std::min(width / 18.0f, height / 36.0f))),
        1, 3);
    const float previous_width = rect_width(motion_template_box_);
    const float previous_height = rect_height(motion_template_box_);
    const float previous_center_x =
        (motion_template_box_.left + motion_template_box_.right) * 0.5f;
    const float previous_center_y =
        (motion_template_box_.top + motion_template_box_.bottom) * 0.5f;
    const float center_distance = std::hypot(
        expected_x - previous_center_x,
        (box.top + height * 0.5f) - previous_center_y);
    const float continuity_radius = std::max(
        24.0f, std::max(previous_width, previous_height) * 0.85f);
    const bool continuous_target =
        has_motion_template_ &&
        (rect_iou(box, motion_template_box_) > 0.02f ||
         center_distance <= continuity_radius);
    // Detector width/height may change when a weapon cuts into one box edge.
    // Keep the already confirmed appearance scale for a continuous target;
    // correlation failure, rather than box geometry alone, decides when to
    // reacquire a new template scale.
    const int spacing =
        continuous_target ? motion_template_spacing_ : detected_spacing;

    std::array<float, kMotionPatchSamples> patch{};
    if (!continuous_target) {
        reset_motion_anchor();
        if (!sample_motion_patch(
                frame, expected_x, expected_y, spacing, patch)) {
            return;
        }
        std::copy(patch.begin(), patch.end(), motion_template_.begin());
        motion_template_box_ = box;
        motion_anchor_x_ = expected_x;
        motion_anchor_y_ = expected_y;
        motion_template_spacing_ = spacing;
        has_motion_template_ = true;
        detection.has_motion_anchor = true;
        detection.motion_anchor_x = expected_x;
        detection.motion_anchor_y = expected_y;
        detection.motion_anchor_score = 0.35f;
        return;
    }

    const int search_radius = std::clamp(
        static_cast<int>(std::lround(std::min(width, height) * 0.20f)),
        4, 12);
    const float search_center_x = motion_anchor_x_;
    const float search_center_y = motion_anchor_y_;
    float best_score = -1.0f;
    int best_dx = 0;
    int best_dy = 0;
    std::array<float, kMotionPatchSamples> best_patch{};
    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            if (!sample_motion_patch(
                    frame,
                    search_center_x + static_cast<float>(dx),
                    search_center_y + static_cast<float>(dy),
                    spacing,
                    patch)) {
                continue;
            }
            const float score = normalized_patch_correlation(
                motion_template_, patch);
            if (score <= best_score) continue;
            best_score = score;
            best_dx = dx;
            best_dy = dy;
            best_patch = patch;
        }
    }
    if (best_score < 0.50f) {
        ++motion_anchor_misses_;
        if (motion_anchor_misses_ >= 2) {
            reset_motion_anchor();
        }
        return;
    }

    motion_anchor_misses_ = 0;
    motion_anchor_x_ = search_center_x + static_cast<float>(best_dx);
    motion_anchor_y_ = search_center_y + static_cast<float>(best_dy);
    motion_template_box_ = box;
    detection.has_motion_anchor = true;
    detection.motion_anchor_x = motion_anchor_x_;
    detection.motion_anchor_y = motion_anchor_y_;
    detection.motion_anchor_score = std::clamp(
        (best_score - 0.40f) / 0.50f, 0.0f, 1.0f);

    if (best_score >= 0.65f) {
        constexpr float kTemplateUpdateAlpha = 0.12f;
        for (std::size_t index = 0;
             index < motion_template_.size(); ++index) {
            motion_template_[index] =
                motion_template_[index] *
                    (1.0f - kTemplateUpdateAlpha) +
                best_patch[index] * kTemplateUpdateAlpha;
        }
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

    if (target != nullptr) {
        clear_auto_fire_state();
        return false;
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
    const float aspect_ratio = aspect_ratio_h_over_w(box_w, box_h);
    const float min_aspect = is_wide_low_pose(box_w, box_h) ? kMinWideLowAspectRatio : kMinAspectRatio;
    if (aspect_ratio < min_aspect || aspect_ratio > kMaxAspectRatio) {
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
    std::uint32_t source_detection_index,
    const std::optional<std::pair<float, float>>& last_target_center,
    const pipeline_contract::UserAimIntent* intent) const {
    const Rect box = to_rect(detection);
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return std::nullopt;
    }
    // The production cod_combined_single_cls model has exactly one semantic
    // target class at index 0. Unknown/multi-class outputs must fail closed;
    // a green friendly marker is an independent hard rejection.
    if (detection.class_id != 0 || detection.is_friendly) {
        return std::nullopt;
    }

    const auto point = target_point(box);
    Candidate observed;
    observed.has_source_detection = true;
    observed.source_detection_index = source_detection_index;
    observed.target_x = point.first;
    observed.target_y = point.second;
    observed.conf = detection.conf;
    observed.color_bonus = detection.color_bonus;
    observed.has_cue = detection.has_cue_point;
    observed.cue_x = detection.cue_x;
    observed.cue_y = detection.cue_y;
    observed.cue_score = detection.cue_score;
    observed.body_box = box;
    observed.aim_region = fallback_aim_region(box);
    observed.fire_zone = fire_zone(box);
    observed.source = "observed";

    const bool tracking_candidate = tracking_distance(
        observed.target_x,
        observed.target_y,
        last_target_center).has_value();
    if (!passes_geometry_gate(box_w, box_h, tracking_candidate)) {
        return std::nullopt;
    }
    const IntentScore pickup_intent_score = score_intent(
        observed,
        screen_center_x_,
        screen_center_y_,
        frame_width_,
        frame_height_,
        intent);
    const bool intent_supported_pickup =
        !tracking_candidate
        && source_equals(pickup_intent_score.decision, "applied_direction")
        && pickup_intent_score.bonus >= kIntentPickupScoreThreshold
        && detection.conf >= kIntentPickupConfidenceThreshold;
    if (!passes_confidence_gate(detection.conf, tracking_candidate, detection.color_bonus > 0.0f)
        && !intent_supported_pickup) {
        return std::nullopt;
    }

    const bool active_match = active_target_matches_candidate(observed);
    if (is_color_checked_without_enemy_cue(detection)
        && candidate_matches_expired_marker_region(observed)) {
        return std::nullopt;
    }
    const bool active_had_enemy_evidence = active_target_.has_value()
        && (active_generation_had_enemy_evidence_
            || candidate_has_enemy_evidence(active_target_->candidate));
    const TargetEvidence evidence = evaluate_target_evidence(
        detection,
        box_w,
        box_h,
        tracking_candidate,
        active_match,
        active_had_enemy_evidence);
    if (evidence.reject) {
        return std::nullopt;
    }
    observed.live_score = evidence.live_score;
    observed.corpse_risk = evidence.corpse_risk;
    observed.uncertainty = evidence.uncertainty;
    if (evidence.weak_only) {
        observed.source = "weak_observed";
    }

    return observed;
}

std::optional<VisionTargetSelector::Candidate> VisionTargetSelector::build_weak_association_candidate(
    const Detection& detection,
    std::uint32_t source_detection_index) const {
    if (!active_target_.has_value() || !last_target_center_.has_value()) {
        return std::nullopt;
    }
    if (detection.class_id != 0 || detection.is_friendly
        || detection.conf < kWeakAssociationConfidenceThreshold
        || detection.conf >= kTrackingConfidenceThreshold) {
        return std::nullopt;
    }

    const Rect box = to_rect(detection);
    const float box_w = rect_width(box);
    const float box_h = rect_height(box);
    if (box_w <= 0.0f || box_h <= 0.0f) {
        return std::nullopt;
    }

    const auto point = target_point(box);
    Candidate weak;
    weak.has_source_detection = true;
    weak.source_detection_index = source_detection_index;
    weak.target_x = point.first;
    weak.target_y = point.second;
    weak.conf = detection.conf;
    weak.color_bonus = detection.color_bonus;
    weak.has_cue = detection.has_cue_point;
    weak.cue_x = detection.cue_x;
    weak.cue_y = detection.cue_y;
    weak.cue_score = detection.cue_score;
    weak.body_box = box;
    weak.aim_region = fallback_aim_region(box);
    weak.fire_zone = fire_zone(box);
    weak.source = "associated_weak";

    if (!passes_geometry_gate(box_w, box_h, true)) {
        return std::nullopt;
    }
    if (!tracking_distance(weak.target_x, weak.target_y, last_target_center_).has_value()) {
        return std::nullopt;
    }
    if (!active_target_matches_candidate(weak)) {
        return std::nullopt;
    }
    if (is_color_checked_without_enemy_cue(detection)
        && candidate_matches_expired_marker_region(weak)) {
        return std::nullopt;
    }
    const bool active_had_enemy_evidence = active_target_.has_value()
        && (active_generation_had_enemy_evidence_
            || candidate_has_enemy_evidence(active_target_->candidate));
    const TargetEvidence evidence = evaluate_target_evidence(
        detection,
        box_w,
        box_h,
        true,
        true,
        active_had_enemy_evidence);
    if (evidence.reject) {
        return std::nullopt;
    }
    weak.live_score = evidence.live_score;
    weak.corpse_risk = evidence.corpse_risk;
    weak.uncertainty = evidence.uncertainty;
    return weak;
}

void VisionTargetSelector::build_candidates(
    const DetectionBatch& batch,
    const std::optional<std::pair<float, float>>& last_target_center,
    const pipeline_contract::UserAimIntent* intent) {
    if (enemy_marker_loss_grace_expired(batch.captured_at_ns)) {
        active_marker_expired_ = true;
    }
    candidate_scratch_.clear();
    candidate_scratch_.reserve(batch.detections.size());
    for (std::size_t index = 0; index < batch.detections.size(); ++index) {
        const auto candidate = build_candidate(
            batch.detections[index],
            static_cast<std::uint32_t>(index),
            last_target_center,
            intent);
        if (candidate.has_value()) {
            candidate_scratch_.push_back(*candidate);
        }
    }
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::select_weak_association(
    const DetectionBatch& batch) const {
    if (!active_target_.has_value()) {
        return std::nullopt;
    }

    std::optional<std::pair<float, Candidate>> best;
    for (std::size_t index = 0; index < batch.detections.size(); ++index) {
        const auto candidate = build_weak_association_candidate(
            batch.detections[index],
            static_cast<std::uint32_t>(index));
        if (!candidate.has_value()) {
            continue;
        }
        const float distance = point_distance(
            {candidate->target_x, candidate->target_y},
            {active_target_->candidate.target_x, active_target_->candidate.target_y});
        if (!best.has_value()
            || distance < best->first
            || (std::fabs(distance - best->first) < 0.001f
                && candidate->conf > best->second.conf)) {
            best = std::make_pair(distance, *candidate);
        }
    }

    if (!best.has_value()) {
        return std::nullopt;
    }
    return target_from_candidate(best->second, active_target_->score);
}

float VisionTargetSelector::crosshair_distance(float x, float y) const {
    return std::hypot(x - screen_center_x_, y - screen_center_y_);
}

bool VisionTargetSelector::candidate_within_pickup_envelope(
    const Candidate& candidate) const {
    const float normalized_height = rect_height(candidate.body_box) /
        std::max(1.0f, frame_height_);
    const float radius = pipeline_contract::target_scaled_pickup_radius(
        pickup_base_radius_px_, normalized_height);
    return crosshair_distance(candidate.target_x, candidate.target_y) <= radius;
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

bool VisionTargetSelector::prefer_candidate(
    const std::optional<ScoredCandidate>& current,
    const ScoredCandidate& challenger) const {
    if (!current.has_value()) {
        return true;
    }
    const float current_effective_distance = crosshair_distance(
        current->candidate.target_x,
        current->candidate.target_y);
    const float challenger_effective_distance = crosshair_distance(
        challenger.candidate.target_x,
        challenger.candidate.target_y);

    if (challenger_effective_distance <
        (current_effective_distance - crosshair_priority_margin_)) {
        return true;
    }
    if (current_effective_distance <
        (challenger_effective_distance - crosshair_priority_margin_)) {
        return false;
    }
    return challenger.score > current->score;
}

VisionTargetSelector::ScoredCandidate VisionTargetSelector::score_candidate(
    const Candidate& candidate,
    const std::optional<std::pair<float, float>>& last_target_center,
    const pipeline_contract::UserAimIntent* intent) const {
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
    }
    const float normalized_height = rect_height(candidate.body_box) /
        std::max(1.0f, static_cast<float>(frame_height_));
    score += normalized_height * kTargetHeightScoreScale;
    score += candidate.live_score * kTargetValidityScoreScale;
    score -= candidate.corpse_risk * kCorpseRiskScoreScale;
    score -= candidate.uncertainty * kTargetValidityScoreScale;
    if (source_equals(candidate.source, "weak_observed")) {
        score -= kWeakObservedScorePenalty;
    }

    const std::optional<float> distance = tracking_distance(
        candidate.target_x,
        candidate.target_y,
        last_target_center);
    score += tracking_bonus_for_distance(distance);
    const IntentScore intent_score = score_intent(
        candidate,
        screen_center_x_,
        screen_center_y_,
        frame_width_,
        frame_height_,
        intent);
    score += intent_score.bonus;

    ScoredCandidate scored;
    scored.candidate = candidate;
    scored.score = score;
    scored.has_tracking_distance = distance.has_value();
    scored.tracking_distance = distance.value_or(0.0f);
    scored.intent_applied = intent_score.applied;
    scored.intent_decision = intent_score.decision;
    scored.intent_score = intent_score.bonus;
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

VisionTargetSelector::TargetState VisionTargetSelector::target_from_scored_candidate(
    const ScoredCandidate& scored) const {
    TargetState target = target_from_candidate(scored.candidate, scored.score);
    target.intent_applied = scored.intent_applied;
    target.intent_decision = scored.intent_decision;
    target.intent_score = scored.intent_score;
    return target;
}

bool VisionTargetSelector::boxes_match(const Rect& lhs, const Rect& rhs) const {
    if (rect_iou(lhs, rhs) >= kActiveTargetIouThreshold) {
        return true;
    }

    const auto lhs_center = rect_center(lhs);
    const auto rhs_center = rect_center(rhs);
    const float lhs_w = rect_width(lhs);
    const float rhs_w = rect_width(rhs);
    const float lhs_h = rect_height(lhs);
    const float rhs_h = rect_height(rhs);
    const float allowed_dx = std::max(8.0f, std::max(lhs_w, rhs_w) * kActiveTargetCenterXRatio);
    const float allowed_dy = std::max(8.0f, std::max(lhs_h, rhs_h) * kActiveTargetCenterYRatio);
    if (std::fabs(lhs_center.first - rhs_center.first) <= allowed_dx
        && std::fabs(lhs_center.second - rhs_center.second) <= allowed_dy) {
        return true;
    }

    const float overlap_x = std::max(0.0f, std::min(lhs.right, rhs.right) - std::max(lhs.left, rhs.left));
    const float min_width = std::min(lhs_w, rhs_w);
    const float overlap_x_ratio = min_width > 0.0f ? (overlap_x / min_width) : 0.0f;
    const float vertical_reacquire_dy = std::max(8.0f, std::max(lhs_h, rhs_h) * 0.75f);
    return overlap_x_ratio >= 0.70f
        && std::fabs(lhs_center.first - rhs_center.first) <= (allowed_dx * 0.75f)
        && std::fabs(lhs_center.second - rhs_center.second) <= vertical_reacquire_dy;
}

bool VisionTargetSelector::targets_match(const TargetState& lhs, const TargetState& rhs) const {
    if (boxes_match(lhs.candidate.body_box, rhs.candidate.body_box)) {
        return true;
    }
    return point_distance(
        {lhs.candidate.target_x, lhs.candidate.target_y},
        {rhs.candidate.target_x, rhs.candidate.target_y}) <= pickup_confirm_radius_;
}

bool VisionTargetSelector::active_target_matches_candidate(const Candidate& candidate) const {
    if (!active_target_.has_value()) {
        return false;
    }

    if (boxes_match(active_target_->candidate.body_box, candidate.body_box)) {
        return true;
    }

    return point_distance(
        {candidate.target_x, candidate.target_y},
        {active_target_->candidate.target_x, active_target_->candidate.target_y}) <= pickup_confirm_radius_;
}

bool VisionTargetSelector::candidate_matches_expired_marker_region(
    const Candidate& candidate) const {
    if (!active_marker_expired_ || !active_target_.has_value()) {
        return false;
    }
    return point_distance(
        {candidate.target_x, candidate.target_y},
        {active_target_->candidate.target_x, active_target_->candidate.target_y})
        <= tracking_radius_;
}

bool VisionTargetSelector::candidate_is_wide_low(const Candidate& candidate) const {
    return is_wide_low_pose(rect_width(candidate.body_box), rect_height(candidate.body_box));
}

bool VisionTargetSelector::candidate_has_enemy_evidence(const Candidate& candidate) const {
    return candidate.has_cue || candidate.color_bonus > 0.0f;
}

bool VisionTargetSelector::should_escape_stale_active_match(
    const TargetState& locked,
    const TargetState& challenger) const {
    if (!active_target_.has_value()) {
        return false;
    }
    if (!source_equals(challenger.candidate.source, "observed")) {
        return false;
    }
    if (challenger.candidate.conf < kPickupConfidenceThreshold) {
        return false;
    }
    if (!candidate_is_wide_low(locked.candidate) || candidate_is_wide_low(challenger.candidate)) {
        return false;
    }
    if (candidate_is_wide_low(active_target_->candidate)) {
        return false;
    }

    const float previous_height = rect_height(active_target_->candidate.body_box);
    const float locked_height = rect_height(locked.candidate.body_box);
    if (previous_height <= 0.0f || locked_height >= (previous_height * kStaleActiveHeightRetainRatio)) {
        return false;
    }
    if (candidate_has_enemy_evidence(locked.candidate)) {
        return false;
    }

    const bool challenger_has_enemy_evidence = candidate_has_enemy_evidence(challenger.candidate);
    if (!challenger_has_enemy_evidence
        && (challenger.candidate.conf + kStaleActiveSwitchConfidenceSlack) < locked.candidate.conf) {
        return false;
    }

    const float locked_distance = crosshair_distance(
        locked.candidate.target_x,
        locked.candidate.target_y);
    const float challenger_distance = crosshair_distance(
        challenger.candidate.target_x,
        challenger.candidate.target_y);
    const float radius_scale = challenger_has_enemy_evidence
        ? kStaleActiveCueSwitchRadiusScale
        : kStaleActiveSwitchRadiusScale;
    return challenger_distance <= (locked_distance + (tracking_radius_ * radius_scale));
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::confirm_pickup(
    const TargetState& target,
    bool allow_marked_single_frame_pickup) {
    const bool safe_marked_pickup = allow_marked_single_frame_pickup &&
        source_equals(target.candidate.source, "observed") &&
        candidate_has_enemy_evidence(target.candidate) &&
        target.candidate.conf >= kPickupEnemyConfidenceThreshold &&
        target.candidate.live_score >= 0.80f &&
        target.candidate.corpse_risk < kWeakObservedRiskThreshold &&
        target.candidate.uncertainty <= 0.35f;
    if (safe_marked_pickup) {
        clear_pending();
        return target;
    }
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

void VisionTargetSelector::clear_pending() {
    pending_target_.reset();
    pending_frames_ = 0;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::commit_target(
    const TargetState& target,
    bool allow_marked_single_frame_pickup) {
    const bool begins_new_generation = !active_target_.has_value() ||
        !targets_match(*active_target_, target);
    if (begins_new_generation &&
        !candidate_within_pickup_envelope(target.candidate)) {
        clear_pending();
        return std::nullopt;
    }

    std::optional<TargetState> committed = target;
    if (!active_target_.has_value()) {
        committed = confirm_pickup(target, allow_marked_single_frame_pickup);
        if (!committed.has_value()) {
            return std::nullopt;
        }
    } else {
        clear_pending();
    }

    const bool is_replacement = active_target_.has_value() &&
        !targets_match(*active_target_, *committed);
    const bool begins_confirmed_generation =
        !active_target_.has_value() || is_replacement;
    if (begins_confirmed_generation) {
        // Cue geometry belongs to the confirmed target generation.  Clear it
        // before committing a replacement so a new target's first cue starts
        // from its own person+cue pair rather than the previous target's
        // smoothed offset.
        clear_cue_tracking();
        ++selector_target_generation_;
        selector_target_changed_ = true;
    } else {
        selector_target_changed_ = false;
    }

    if (begins_confirmed_generation) {
        active_generation_had_enemy_evidence_ =
            candidate_has_enemy_evidence(committed->candidate);
        active_marker_expired_ = false;
    } else {
        active_generation_had_enemy_evidence_ =
            active_generation_had_enemy_evidence_
            || candidate_has_enemy_evidence(committed->candidate);
    }

    TargetState stored_target = *committed;
    stored_target.intent_applied = false;
    stored_target.intent_decision = "none";
    stored_target.intent_score = 0.0f;
    active_target_ = stored_target;
    active_identity_miss_frames_ = 0;
    last_target_center_ = {
        active_target_->candidate.target_x,
        active_target_->candidate.target_y,
    };
    return committed;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::select_single_candidate(
    const Candidate& candidate,
    const pipeline_contract::UserAimIntent* intent) const {
    if (!active_target_matches_candidate(candidate) &&
        !candidate_within_pickup_envelope(candidate)) {
        return std::nullopt;
    }
    TargetState selected = target_from_candidate(
        candidate,
        candidate.color_bonus +
            (candidate.conf * kConfidenceScoreScale));
    const auto intent_score = score_intent(
        candidate,
        screen_center_x_,
        screen_center_y_,
        frame_width_,
        frame_height_,
        intent);
    selected.intent_applied = intent_score.applied;
    selected.intent_decision = intent_score.decision;
    selected.intent_score = intent_score.bonus;
    return selected;
}

std::pair<std::optional<VisionTargetSelector::TargetState>, std::optional<VisionTargetSelector::TargetState>>
VisionTargetSelector::select_multi_candidate(
    const std::vector<Candidate>& candidates,
    const std::optional<std::pair<float, float>>& last_target_center,
    const pipeline_contract::UserAimIntent* intent) const {
    std::optional<ScoredCandidate> best;
    std::optional<ScoredCandidate> tracked;
    std::optional<ScoredCandidate> best_non_active;
    std::optional<ScoredCandidate> best_intent_non_active;
    std::optional<std::pair<float, ScoredCandidate>> active_match;

    for (const auto& candidate : candidates) {
        const bool matches_active = active_target_matches_candidate(candidate);
        if (!matches_active && !candidate_within_pickup_envelope(candidate)) {
            continue;
        }
        const ScoredCandidate scored = score_candidate(candidate, last_target_center, intent);
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

        if (!matches_active && prefer_candidate(best_non_active, scored)) {
            best_non_active = scored;
        }
        if (!matches_active && scored.intent_applied &&
            scored.intent_score > 0.0f &&
            (!best_intent_non_active.has_value() ||
             scored.intent_score > best_intent_non_active->intent_score ||
             (std::fabs(scored.intent_score -
                        best_intent_non_active->intent_score) < 0.001f &&
              prefer_candidate(best_intent_non_active, scored)))) {
            best_intent_non_active = scored;
        }

        if (matches_active) {
            const float active_distance = point_distance(
                {candidate.target_x, candidate.target_y},
                {active_target_->candidate.target_x, active_target_->candidate.target_y});
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

    if (!active_target_.has_value()
        && tracked.has_value()
        && (best->candidate.target_x != tracked->candidate.target_x
            || best->candidate.target_y != tracked->candidate.target_y)
        && best->score < (tracked->score + kTrackingSwitchMargin)) {
        best = tracked;
    }

    if (active_match.has_value() && best_non_active.has_value()) {
        const TargetState locked = target_from_scored_candidate(active_match->second);
        const TargetState challenger = target_from_scored_candidate(*best_non_active);
        if (should_escape_stale_active_match(locked, challenger)) {
            best = best_non_active;
        }
    }

    const bool identity_selection_intent = intent != nullptr &&
        intent->valid && intent->aiming &&
        (!active_target_.has_value() ||
         intent->purpose ==
            pipeline_contract::UserAimIntentPurpose::HandoverTarget);
    if (identity_selection_intent && active_match.has_value() &&
        best_intent_non_active.has_value()) {
        const float locked_crosshair_distance = crosshair_distance(
            active_match->second.candidate.target_x,
            active_match->second.candidate.target_y);
        const float active_intent_score =
            active_match->second.intent_applied
            ? active_match->second.intent_score
            : 0.0f;
        const float challenger_intent_score =
            best_intent_non_active->intent_score;
        const bool current_target_captured =
            locked_crosshair_distance <= kHandoverCapturedRadiusPx;
        const bool intent_points_away_from_current =
            active_intent_score <=
            challenger_intent_score * kHandoverActiveIntentRatio;
        if (current_target_captured || intent_points_away_from_current) {
            // Crosshair proximity owns ordinary ranking.  A decisive flick
            // owns handover identity so the old near target cannot trap the
            // user's motion before Controller gets a chance to seek B.
            best = best_intent_non_active;
        }
    }

    const std::optional<TargetState> chosen_target = target_from_scored_candidate(*best);
    const std::optional<TargetState> active_match_target = active_match.has_value()
        ? std::optional<TargetState>(target_from_scored_candidate(active_match->second))
        : std::nullopt;
    return {chosen_target, active_match_target};
}

std::pair<std::optional<VisionTargetSelector::TargetState>, std::optional<VisionTargetSelector::TargetState>>
VisionTargetSelector::select_candidate_targets(
    const std::vector<Candidate>& candidates,
    const std::optional<std::pair<float, float>>& last_target_center,
    const pipeline_contract::UserAimIntent* intent) const {
    if (candidates.empty()) {
        return {std::nullopt, std::nullopt};
    }
    if (candidates.size() == 1) {
        const auto chosen = select_single_candidate(candidates.front(), intent);
        const auto active_match = (chosen.has_value() && active_target_matches_candidate(candidates.front()))
            ? chosen
            : std::nullopt;
        return {chosen, active_match};
    }
    return select_multi_candidate(candidates, last_target_center, intent);
}

std::optional<VisionTargetSelector::TargetState>
VisionTargetSelector::resolve_active_target_transition(
    const TargetState& chosen_target,
    const std::optional<TargetState>& active_match_target,
    const pipeline_contract::UserAimIntent* intent) {
    if (!active_target_.has_value()) {
        return chosen_target;
    }

    if (active_match_target.has_value()) {
        if (!targets_match(chosen_target, *active_match_target)) {
            const bool identity_selection_allowed = intent != nullptr &&
                intent->valid && intent->aiming &&
                intent->purpose ==
                    pipeline_contract::UserAimIntentPurpose::HandoverTarget;
            // Liveness invalidation removes authority; it does not silently
            // grant the next-highest scorer a different identity. Publish no
            // target so Controller returns to acquisition/manual authority,
            // unless that authority has now issued an aligned replacement.
            if (should_escape_stale_active_match(*active_match_target, chosen_target)) {
                return identity_selection_allowed && chosen_target.intent_applied
                    ? std::optional<TargetState>(chosen_target)
                    : std::nullopt;
            }
            // While Controller owns I, raw direction is D correction and can
            // never be reused as a competing selector vote. Ordinary score
            // changes likewise cannot replace a still-valid active identity.
            if (!identity_selection_allowed || !chosen_target.intent_applied) {
                TargetState retained = *active_match_target;
                retained.intent_applied = false;
                retained.intent_decision = identity_selection_allowed
                    ? "ignored_unaligned_challenger"
                    : "current_target_correction_only";
                retained.intent_score = chosen_target.intent_score;
                return retained;
            }
            // Boundary-qualified handover already paid its temporal
            // disambiguation cost in Controller. Do not add another frame
            // queue before allowing the selector-owned identity transition.
            return chosen_target;
        }

        active_identity_miss_frames_ = 0;
        return *active_match_target;
    }

    const bool identity_selection_allowed = intent != nullptr &&
        intent->valid && intent->aiming &&
        intent->purpose ==
            pipeline_contract::UserAimIntentPurpose::HandoverTarget &&
        chosen_target.intent_applied;
    if (identity_selection_allowed) {
        return chosen_target;
    }

    // The previous I disappeared. Release it first; a credible replacement
    // still needs a new acquisition/handover intent rather than an automatic
    // score-based switch.
    return std::nullopt;
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::try_external_cue_hold(
    const DetectionBatch& batch) {
    if (!batch.has_external_cue
        || !cue_hold_is_active(batch.captured_at_ns)) {
        return std::nullopt;
    }

    const auto bounds = cue_hold_search_region(batch.captured_at_ns);
    if (!bounds.has_value()
        || !rect_contains_point(*bounds, batch.external_cue_x, batch.external_cue_y)) {
        return std::nullopt;
    }

    Candidate held = active_target_->candidate;
    const float cue_dx = batch.external_cue_x - last_cue_point_->first;
    const float cue_dy = batch.external_cue_y - last_cue_point_->second;
    held.target_x = batch.external_cue_x + last_target_offset_from_cue_->first;
    held.target_y = batch.external_cue_y + last_target_offset_from_cue_->second;
    if (!cue_reconstructed_target_is_reasonable(held.target_x, held.target_y)) {
        clear_cue_tracking();
        return std::nullopt;
    }
    held.body_box = shift_rect(active_target_->candidate.body_box, cue_dx, cue_dy);
    held.aim_region = shift_rect(active_target_->candidate.aim_region, cue_dx, cue_dy);
    held.fire_zone = shift_rect(active_target_->candidate.fire_zone, cue_dx, cue_dy);
    held.has_cue = true;
    held.cue_x = batch.external_cue_x;
    held.cue_y = batch.external_cue_y;
    held.cue_score = batch.external_cue_score;
    held.source = "cue_hold";
    held.has_source_detection = false;

    last_cue_point_ = std::make_pair(batch.external_cue_x, batch.external_cue_y);
    if (batch.captured_at_ns != 0) {
        last_cue_observation_ns_ = batch.captured_at_ns;
    }
    cue_hold_frames_ += 1;
    return target_from_candidate(held, active_target_->score);
}

std::optional<VisionTargetSelector::TargetState> VisionTargetSelector::try_cue_hold(
    const ColorFrameView& frame,
    std::uint64_t observation_ns) {
    if (!cue_hold_is_active(observation_ns)) {
        return std::nullopt;
    }

    const auto bounds = cue_hold_search_region(observation_ns);
    if (!bounds.has_value()) {
        return std::nullopt;
    }

    const YellowCueObservation cue = scan_yellow_window(
        *bounds,
        frame,
        last_cue_point_->first,
        last_cue_point_->second);
    if (!cue.found) {
        return std::nullopt;
    }

    Candidate held = active_target_->candidate;
    const float cue_dx = cue.cue_x - last_cue_point_->first;
    const float cue_dy = cue.cue_y - last_cue_point_->second;
    held.target_x = cue.cue_x + last_target_offset_from_cue_->first;
    held.target_y = cue.cue_y + last_target_offset_from_cue_->second;
    if (!cue_reconstructed_target_is_reasonable(held.target_x, held.target_y)) {
        clear_cue_tracking();
        return std::nullopt;
    }
    held.body_box = shift_rect(active_target_->candidate.body_box, cue_dx, cue_dy);
    held.aim_region = shift_rect(active_target_->candidate.aim_region, cue_dx, cue_dy);
    held.fire_zone = shift_rect(active_target_->candidate.fire_zone, cue_dx, cue_dy);
    held.has_cue = true;
    held.cue_x = cue.cue_x;
    held.cue_y = cue.cue_y;
    held.cue_score = cue.score;
    held.source = "cue_hold";
    held.has_source_detection = false;

    last_cue_point_ = std::make_pair(cue.cue_x, cue.cue_y);
    if (observation_ns != 0) {
        last_cue_observation_ns_ = observation_ns;
    }
    cue_hold_frames_ += 1;
    return target_from_candidate(held, active_target_->score);
}

bool VisionTargetSelector::cue_reconstructed_target_is_reasonable(
    float target_x,
    float target_y) const {
    if (!active_target_.has_value() ||
        !std::isfinite(target_x) || !std::isfinite(target_y)) {
        return false;
    }
    const float residual = std::hypot(
        target_x - active_target_->candidate.target_x,
        target_y - active_target_->candidate.target_y);
    return std::isfinite(residual) &&
        residual <= kCueMaxReconstructedTargetResidualPx;
}

bool VisionTargetSelector::cue_hold_is_active(std::uint64_t observation_ns) const {
    if (!active_target_.has_value()
        || !last_cue_point_.has_value()
        || !last_target_offset_from_cue_.has_value()
        || cue_tracking_generation_ == 0
        || cue_tracking_generation_ != selector_target_generation_) {
        return false;
    }
    if (last_direct_cue_observation_ns_ == 0 ||
        last_cue_observation_ns_ == 0 || observation_ns == 0) {
        return cue_hold_frames_ < kMaxCueHoldFallbackFrames;
    }
    if (observation_ns < last_direct_cue_observation_ns_ ||
        observation_ns < last_cue_observation_ns_) {
        return false;
    }
    return observation_ns - last_direct_cue_observation_ns_
            <= kCueHoldMaxDurationNs &&
        observation_ns - last_cue_observation_ns_
            <= kCueHoldEvidenceGapNs;
}

bool VisionTargetSelector::enemy_marker_loss_grace_expired(
    std::uint64_t observation_ns) const {
    if (!active_target_.has_value() || !active_generation_had_enemy_evidence_) {
        return false;
    }
    if (last_direct_cue_observation_ns_ == 0 ||
        last_cue_observation_ns_ == 0 || observation_ns == 0) {
        return cue_hold_frames_ >= kMaxCueHoldFallbackFrames;
    }
    if (observation_ns < last_direct_cue_observation_ns_ ||
        observation_ns < last_cue_observation_ns_) {
        return true;
    }
    return observation_ns - last_direct_cue_observation_ns_
            > kCueHoldMaxDurationNs ||
        observation_ns - last_cue_observation_ns_
            > kCueHoldEvidenceGapNs;
}

std::optional<VisionTargetSelector::FrameRegion> VisionTargetSelector::cue_hold_search_region(
    std::uint64_t observation_ns) const {
    if (!cue_hold_is_active(observation_ns)) {
        return std::nullopt;
    }

    const int frame_width = static_cast<int>(frame_width_);
    const int frame_height = static_cast<int>(frame_height_);
    const int search_radius = kCueHoldSearchRadius;
    const int cue_x = static_cast<int>(std::round(last_cue_point_->first));
    const int cue_y = static_cast<int>(std::round(last_cue_point_->second));
    IntRect bounds{
        std::max(0, cue_x - search_radius),
        std::max(0, cue_y - search_radius),
        std::min(frame_width, cue_x + search_radius + 1),
        std::min(frame_height, cue_y + search_radius + 1),
    };
    if ((bounds.right - bounds.left) < 4 || (bounds.bottom - bounds.top) < 4) {
        return std::nullopt;
    }
    return bounds;
}

void VisionTargetSelector::update_cue_tracking(
    const TargetState& target,
    std::uint64_t observation_ns) {
    if (!target.candidate.has_cue) {
        if (selector_target_changed_) {
            clear_cue_tracking();
        } else if (active_generation_had_enemy_evidence_) {
            cue_hold_frames_ = std::min(
                cue_hold_frames_ + 1,
                kMaxCueHoldFallbackFrames);
        }
        return;
    }
    // Only an observed person+cue pair may establish or refresh geometry. A
    // cue-held target is an output continuation and must never teach the
    // offset that will be used to reconstruct the next point.
    if (!target.candidate.has_source_detection) {
        return;
    }

    const std::pair<float, float> cue_point = {
        target.candidate.cue_x,
        target.candidate.cue_y,
    };
    std::pair<float, float> new_offset = {
        target.candidate.target_x - target.candidate.cue_x,
        target.candidate.target_y - target.candidate.cue_y,
    };
    if (last_target_offset_from_cue_.has_value()) {
        new_offset.first =
            last_target_offset_from_cue_->first
            + ((new_offset.first - last_target_offset_from_cue_->first) * kCueOffsetSmoothingAlpha);
        new_offset.second =
            last_target_offset_from_cue_->second
            + ((new_offset.second - last_target_offset_from_cue_->second) * kCueOffsetSmoothingAlpha);
    }

    last_cue_point_ = cue_point;
    last_target_offset_from_cue_ = new_offset;
    cue_tracking_generation_ = selector_target_generation_;
    last_direct_cue_observation_ns_ = observation_ns;
    last_cue_observation_ns_ = observation_ns;
    active_marker_expired_ = false;
    cue_hold_frames_ = 0;
}

VisionResult VisionTargetSelector::finalize_selected_target(
    const TargetState& chosen_target,
    float boxes_seen,
    bool single_credible_candidate,
    std::uint64_t observation_ns) {
    // Direct observations are already current-frame measurements. Historical
    // jump rejection and point smoothing made the selector output a plausible
    // but stale coordinate, which then looked like controller latency.
    const auto committed = commit_target(
        chosen_target,
        single_credible_candidate);
    if (!committed.has_value()) {
        return empty_result(boxes_seen);
    }
    update_cue_tracking(*committed, observation_ns);
    VisionResult result = result_from_target(*committed, boxes_seen);
    result.auto_fire = update_auto_fire(&*committed);
    return result;
}

VisionResult VisionTargetSelector::select(const DetectionBatch& batch) {
    VisionResult result = select_impl(batch, nullptr, nullptr);
    result.preprocess_mode = batch.preprocess_mode;
    result.detections = batch.detections;
    return result;
}

VisionResult VisionTargetSelector::select(
    const DetectionBatch& batch,
    const pipeline_contract::UserAimIntent& intent) {
    VisionResult result = select_impl(batch, nullptr, &intent);
    result.user_aim_intent = intent;
    if (intent.valid) {
        result.intent_id = intent.intent_id;
    }
    result.preprocess_mode = batch.preprocess_mode;
    result.detections = batch.detections;
    return result;
}

VisionResult VisionTargetSelector::select_with_frame(
    const DetectionBatch& batch,
    const ColorFrameView& frame) {
    DetectionBatch annotated = annotate_colors(batch, frame);
    VisionResult result = select_impl(annotated, &frame, nullptr);
    if (result.selector_target_changed) {
        // A selector-confirmed generation change is the identity boundary for
        // the appearance anchor.  Do not correlate the replacement against the
        // previous person's patch; ordinary reconstruction and frame-local
        // source-id churn keep the existing template.
        reset_motion_anchor();
    }
    if (result.has_selected_detection &&
        result.selected_detection_index < annotated.detections.size()) {
        update_selected_motion_anchor(
            annotated.detections[result.selected_detection_index], frame);
    } else if (!result.has_target) {
        reset_motion_anchor();
    }
    result.preprocess_mode = batch.preprocess_mode;
    result.detections = std::move(annotated.detections);
    return result;
}

VisionResult VisionTargetSelector::select_with_frame(
    const DetectionBatch& batch,
    const ColorFrameView& frame,
    const pipeline_contract::UserAimIntent& intent) {
    DetectionBatch annotated = annotate_colors(batch, frame);
    VisionResult result = select_impl(annotated, &frame, &intent);
    if (result.selector_target_changed) {
        // See the frame-only overload: only a confirmed selector generation
        // change may retire the prior person's appearance anchor.
        reset_motion_anchor();
    }
    if (result.has_selected_detection &&
        result.selected_detection_index < annotated.detections.size()) {
        update_selected_motion_anchor(
            annotated.detections[result.selected_detection_index], frame);
    } else if (!result.has_target) {
        reset_motion_anchor();
    }
    result.user_aim_intent = intent;
    if (intent.valid) {
        result.intent_id = intent.intent_id;
    }
    result.preprocess_mode = batch.preprocess_mode;
    result.detections = std::move(annotated.detections);
    return result;
}

VisionResult VisionTargetSelector::select_impl(
    const DetectionBatch& batch,
    const ColorFrameView* frame,
    const pipeline_contract::UserAimIntent* intent) {
    selector_target_changed_ = false;
    const float boxes_seen = static_cast<float>(batch.detections.size());
    const auto last_target_center = last_target_center_;
    build_candidates(batch, last_target_center, intent);
    const auto& candidates = candidate_scratch_;
    if (candidates.empty()) {
        clear_pending();
        if (active_marker_expired_) {
            VisionResult result = empty_result(boxes_seen);
            clear_auto_fire_state();
            result.auto_fire = false;
            return result;
        }
        const auto weak_association = select_weak_association(batch);
        if (weak_association.has_value()) {
            active_target_ = *weak_association;
            active_identity_miss_frames_ = 0;
            active_generation_had_enemy_evidence_ =
                active_generation_had_enemy_evidence_
                || candidate_has_enemy_evidence(active_target_->candidate);
            last_target_center_ = {
                active_target_->candidate.target_x,
                active_target_->candidate.target_y,
            };
            update_cue_tracking(*active_target_, batch.captured_at_ns);
            VisionResult result = result_from_target(*active_target_, boxes_seen);
            clear_auto_fire_state();
            result.auto_fire = false;
            return result;
        }
        const auto external_cue_hold = try_external_cue_hold(batch);
        if (external_cue_hold.has_value()) {
            active_target_ = *external_cue_hold;
            active_identity_miss_frames_ = 0;
            last_target_center_ = {
                active_target_->candidate.target_x,
                active_target_->candidate.target_y,
            };
            VisionResult result = result_from_target(*active_target_, boxes_seen);
            clear_auto_fire_state();
            result.auto_fire = false;
            return result;
        }
        if (frame != nullptr) {
            const auto cue_region = cue_hold_search_region(batch.captured_at_ns);
            if (cue_region.has_value() && !frame_covers(*cue_region, *frame)) {
                // The requested cue ROI was not present, so this frame cannot
                // update the target. Preserve identity internally but expose
                // no old-coordinate actuation authority.
                VisionResult result = empty_result(boxes_seen);
                clear_auto_fire_state();
                result.auto_fire = false;
                return result;
            }
            const auto cue_hold = try_cue_hold(*frame, batch.captured_at_ns);
            if (cue_hold.has_value()) {
                active_target_ = *cue_hold;
                active_identity_miss_frames_ = 0;
                last_target_center_ = {
                    active_target_->candidate.target_x,
                    active_target_->candidate.target_y,
                };
                VisionResult result = result_from_target(*active_target_, boxes_seen);
                clear_auto_fire_state();
                result.auto_fire = false;
                return result;
            }
        }
        if (active_target_.has_value() && active_generation_had_enemy_evidence_) {
            active_identity_miss_frames_ = 0;
            if (last_direct_cue_observation_ns_ == 0 || batch.captured_at_ns == 0) {
                cue_hold_frames_ = std::min(
                    cue_hold_frames_ + 1,
                    kMaxCueHoldFallbackFrames);
            }
            if (enemy_marker_loss_grace_expired(batch.captured_at_ns)) {
                active_marker_expired_ = true;
            }
            VisionResult result = empty_result(boxes_seen);
            clear_auto_fire_state();
            result.auto_fire = false;
            return result;
        }
        if (active_target_.has_value() &&
            active_identity_miss_frames_ <
                kActiveIdentityMissFramesBeforeInvalidation) {
            ++active_identity_miss_frames_;
        } else {
            clear_tracking_state();
        }
        VisionResult result = empty_result(boxes_seen);
        clear_auto_fire_state();
        result.auto_fire = false;
        return result;
    }

    const auto selected = select_candidate_targets(candidates, last_target_center, intent);
    if (!selected.first.has_value()) {
        VisionResult result = empty_result(boxes_seen);
        clear_auto_fire_state();
        result.auto_fire = false;
        return result;
    }

    const bool single_credible_candidate = candidates.size() == 1;
    const auto transition = resolve_active_target_transition(
        *selected.first,
        selected.second,
        intent);
    if (!transition.has_value()) {
        // Identity was invalidated or replacement was not explicitly
        // requested. Current Vision has declined control ownership.
        if (active_target_.has_value()) {
            ++active_identity_miss_frames_;
            if (active_identity_miss_frames_ >=
                kActiveIdentityMissFramesBeforeInvalidation) {
                clear_tracking_state();
            }
        }
        VisionResult result = empty_result(boxes_seen);
        clear_auto_fire_state();
        result.auto_fire = false;
        return result;
    }

    return finalize_selected_target(
        *transition,
        boxes_seen,
        single_credible_candidate,
        batch.captured_at_ns);
}

} // namespace vision_native
