#include "ads_visual_transition.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {
namespace {
float width(const AdsVisualFrame& frame) noexcept { return std::max(0.0f, frame.x2 - frame.x1); }
float height(const AdsVisualFrame& frame) noexcept { return std::max(0.0f, frame.y2 - frame.y1); }
float center_x(const AdsVisualFrame& frame) noexcept { return (frame.x1 + frame.x2) * 0.5f; }
float center_y(const AdsVisualFrame& frame) noexcept { return (frame.y1 + frame.y2) * 0.5f; }
}

AdsVisualTransitionEstimator::AdsVisualTransitionEstimator(AdsVisualTransitionOptions options)
    : options_(options) {
    options_.settle_consecutive_frames = std::max(1u, options_.settle_consecutive_frames);
}

void AdsVisualTransitionEstimator::start(const AdsVisualFrame& anchor) noexcept {
    anchor_ = anchor;
    previous_ = anchor;
    latest_ = AdsVisualEvidence{};
    latest_.frame_id = anchor.frame_id;
    active_ = anchor.live && width(anchor) > 0.0f && height(anchor) > 0.0f;
    stable_frames_ = 0;
}

AdsVisualEvidence AdsVisualTransitionEstimator::observe(const AdsVisualFrame& frame) noexcept {
    if (!active_ || frame.frame_id == 0 || frame.frame_id == previous_.frame_id) {
        AdsVisualEvidence repeated = latest_;
        repeated.accepted_new_frame = false;
        return repeated;
    }

    AdsVisualEvidence result;
    result.frame_id = frame.frame_id;
    result.accepted_new_frame = true;
    const float anchor_w = width(anchor_);
    const float anchor_h = height(anchor_);
    const float previous_scale_x = anchor_w > 0.0f ? width(previous_) / anchor_w : 1.0f;
    const float previous_scale_y = anchor_h > 0.0f ? height(previous_) / anchor_h : 1.0f;
    result.scale_x = anchor_w > 0.0f ? width(frame) / anchor_w : 1.0f;
    result.scale_y = anchor_h > 0.0f ? height(frame) / anchor_h : 1.0f;
    result.offset_x = center_x(frame) - center_x(anchor_);
    result.offset_y = center_y(frame) - center_y(anchor_);
    const float previous_offset_x = center_x(previous_) - center_x(anchor_);
    const float previous_offset_y = center_y(previous_) - center_y(anchor_);
    result.motion_residual_px = std::max(0.0f, frame.motion_residual_px);
    result.visual_progress = std::max(0.0f, std::min(1.0f,
        (std::max(result.scale_x, result.scale_y) - 1.0f) / 0.4f));

    const bool identity_ok = frame.target_track_id == anchor_.target_track_id && high_quality(frame);
    const bool derivative_ok =
        std::fabs(result.scale_x - previous_scale_x) <= options_.max_scale_derivative &&
        std::fabs(result.scale_y - previous_scale_y) <= options_.max_scale_derivative &&
        std::fabs(result.offset_x - previous_offset_x) <= options_.max_offset_derivative_px &&
        std::fabs(result.offset_y - previous_offset_y) <= options_.max_offset_derivative_px;
    const bool residual_ok = result.motion_residual_px <= options_.max_motion_residual_px;
    if (identity_ok && frame.live && derivative_ok && residual_ok) ++stable_frames_;
    else stable_frames_ = 0;

    result.settle_confidence = std::min(1.0f,
        static_cast<float>(stable_frames_) / options_.settle_consecutive_frames);
    result.settled = stable_frames_ >= options_.settle_consecutive_frames;
    result.clean_calibration = result.settled && residual_ok;
    previous_ = frame;
    latest_ = result;
    return result;
}

void AdsVisualTransitionEstimator::observe_timer_only(std::uint64_t) noexcept {}

const AdsVisualEvidence& AdsVisualTransitionEstimator::latest() const noexcept {
    return latest_;
}

void AdsVisualTransitionEstimator::reset() noexcept {
    anchor_ = AdsVisualFrame{};
    previous_ = AdsVisualFrame{};
    latest_ = AdsVisualEvidence{};
    active_ = false;
    stable_frames_ = 0;
}

bool AdsVisualTransitionEstimator::high_quality(const AdsVisualFrame& frame) const noexcept {
    return frame.identity_quality == TargetIdentityQuality::ProductionAssociated ||
        frame.identity_quality == TargetIdentityQuality::StrongGeometricMatch;
}

} // namespace runtime_app
