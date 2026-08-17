#include "target_state_reducers.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

bool valid_region(const common_native::Box2f& region) noexcept {
    return std::isfinite(region.x) && std::isfinite(region.y) &&
        std::isfinite(region.w) && std::isfinite(region.h) &&
        region.w > 1.0f && region.h > 1.0f;
}

pipeline_contract::Vec2f clamp_to_region(
    pipeline_contract::Vec2f point,
    const common_native::Box2f& region) noexcept {
    return {
        std::clamp(point.x, region.x, region.x + region.w),
        std::clamp(point.y, region.y, region.y + region.h)};
}

pipeline_contract::Vec2f normalized_in_region(
    pipeline_contract::Vec2f point,
    const common_native::Box2f& region) noexcept {
    const auto clamped = clamp_to_region(point, region);
    return {
        std::clamp((clamped.x - region.x) / region.w, 0.0f, 1.0f),
        std::clamp((clamped.y - region.y) / region.h, 0.0f, 1.0f)};
}

pipeline_contract::Vec2f point_in_region(
    pipeline_contract::Vec2f normalized,
    const common_native::Box2f& region) noexcept {
    return {
        region.x + region.w * std::clamp(normalized.x, 0.0f, 1.0f),
        region.y + region.h * std::clamp(normalized.y, 0.0f, 1.0f)};
}

float bounded_manual_axis(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
}

}  // namespace

void TargetGeometryReducer::reset() noexcept {
    state_ = {};
}

void TargetGeometryReducer::adopt(
    const pipeline_contract::VisionCandidate& candidate,
    bool cue_continuation) noexcept {
    common_native::Box2f region = candidate.aim_region_px;
    auto source = candidate.aim_region_source;
    if (!candidate.has_aim_region || !valid_region(region)) {
        region = candidate.body_box_px;
        source = pipeline_contract::AimRegionSource::BodyBoxFallback;
    }
    if (!valid_region(region) || !pipeline_contract::finite(candidate.aim_px)) {
        reset();
        state_.source_position = candidate.aim_px;
        return;
    }
    state_.source_position = clamp_to_region(candidate.aim_px, region);
    state_.aim_region = region;
    state_.available = true;
    state_.source = cue_continuation
        ? pipeline_contract::AimRegionSource::CueTranslated
        : source == pipeline_contract::AimRegionSource::None
            ? pipeline_contract::AimRegionSource::VisionGeometry
            : source;
}

void DesiredPointReducer::reset() noexcept {
    state_ = {};
    boundary_seconds_x_ = 0.0f;
    boundary_seconds_y_ = 0.0f;
}

void DesiredPointReducer::adopt_geometry(
    const TargetGeometrySnapshot& geometry,
    bool cue_continuation,
    bool reset_desired_point,
    bool previous_geometry_available) noexcept {
    if (!geometry.available) {
        reset();
        return;
    }
    if (reset_desired_point || !previous_geometry_available) {
        state_.normalized = normalized_in_region(
            geometry.source_position, geometry.aim_region);
        state_.user_active = false;
    } else if (!cue_continuation && !state_.user_active) {
        state_.normalized = normalized_in_region(
            geometry.source_position, geometry.aim_region);
    }
    state_.position = point_in_region(state_.normalized, geometry.aim_region);
    state_.source = state_.user_active
        ? pipeline_contract::DesiredPointSource::UserCorrected
        : cue_continuation
            ? pipeline_contract::DesiredPointSource::CueCarried
            : pipeline_contract::DesiredPointSource::VisionDefault;
}

void DesiredPointReducer::reduce_manual(
    const pipeline_contract::IntentState& intent,
    const TargetGeometrySnapshot& geometry,
    bool target_present,
    bool firing_recently,
    float dt_seconds) noexcept {
    state_.manual_correction_x = false;
    state_.manual_correction_y = false;
    state_.manual_boundary_x = false;
    state_.manual_boundary_y = false;
    state_.manual_exit_requested = false;
    if (!intent.ads || !target_present || !geometry.available ||
        intent.right_purpose !=
            pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget) {
        boundary_seconds_x_ = 0.0f;
        boundary_seconds_y_ = 0.0f;
        return;
    }

    const float manual_x = bounded_manual_axis(intent.filtered_right.x);
    const float manual_y = -bounded_manual_axis(intent.filtered_right.y);
    const float traversal_seconds = std::max(
        0.040f, config_.traversal_ms / 1000.0f);
    const float step_scale = std::max(0.0f, dt_seconds) / traversal_seconds;
    const auto update_axis = [step_scale](
        float manual,
        float& normalized,
        float& boundary_seconds,
        bool& correction,
        bool& boundary,
        float dt) {
        if (manual == 0.0f) {
            boundary_seconds = 0.0f;
            return;
        }
        const float attempted = normalized + manual * step_scale;
        const float clamped = std::clamp(attempted, 0.0f, 1.0f);
        boundary = attempted < -0.0001f || attempted > 1.0001f;
        boundary_seconds = boundary
            ? boundary_seconds + std::max(0.0f, dt)
            : 0.0f;
        normalized = clamped;
        correction = true;
    };
    update_axis(
        manual_x,
        state_.normalized.x,
        boundary_seconds_x_,
        state_.manual_correction_x,
        state_.manual_boundary_x,
        dt_seconds);
    update_axis(
        manual_y,
        state_.normalized.y,
        boundary_seconds_y_,
        state_.manual_correction_y,
        state_.manual_boundary_y,
        dt_seconds);

    const float exit_seconds = std::max(
        0.050f, config_.boundary_exit_ms / 1000.0f);
    const bool firing_downward = firing_recently && manual_y > 0.0f;
    if (firing_downward) boundary_seconds_y_ = 0.0f;
    state_.manual_exit_requested =
        boundary_seconds_x_ >= exit_seconds ||
        (!firing_downward && boundary_seconds_y_ >= exit_seconds);
    if (state_.manual_exit_requested) {
        state_.manual_correction_x = false;
        state_.manual_correction_y = false;
    } else if (state_.manual_correction_x || state_.manual_correction_y) {
        state_.user_active = true;
        state_.source = pipeline_contract::DesiredPointSource::UserCorrected;
    }
    state_.position = point_in_region(state_.normalized, geometry.aim_region);
}

}  // namespace controller_native
