#include "legacy_projection_tracker.h"

#include "tracker_authority.h"

#include <algorithm>

namespace tracking_native {

namespace {

float clamp_unit(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

}  // namespace

LegacyProjectionTracker::LegacyProjectionTracker(
    pipeline_contract::TargetTrackerConfig config)
    : config_(config) {}

void LegacyProjectionTracker::reset() {
    observed_error_px_ = {};
    target_velocity_px_per_sec_ = {};
    camera_motion_since_observation_px_ = {};
    observed_at_seconds_ = 0.0;
    target_tier_ = "none";
    last_aim_error_px_ = {};
    last_body_box_px_ = {};
    last_has_body_box_ = false;
}

void LegacyProjectionTracker::ingest(const TrackerObservation& observation) {
    if (!observation.has_target || observation.capture_time.value <= 0.0) {
        reset();
        return;
    }

    const bool had_previous = observed_at_seconds_ > 0.0;
    if (had_previous &&
        observation.capture_time.value > observed_at_seconds_ &&
        is_strong_observation(target_tier_) &&
        is_strong_observation(observation.target_tier)) {
        const double dt = observation.capture_time.value - observed_at_seconds_;
        const float raw_vx = static_cast<float>(
            (observation.aim_error_px.x - observed_error_px_.x +
             camera_motion_since_observation_px_.x) /
            dt);
        const float raw_vy = static_cast<float>(
            (observation.aim_error_px.y - observed_error_px_.y +
             camera_motion_since_observation_px_.y) /
            dt);
        const float alpha = clamp_unit(config_.velocity_lowpass_alpha);
        target_velocity_px_per_sec_.x = clamp_velocity(
            (target_velocity_px_per_sec_.x * alpha) + (raw_vx * (1.0f - alpha)));
        target_velocity_px_per_sec_.y = clamp_velocity(
            (target_velocity_px_per_sec_.y * alpha) + (raw_vy * (1.0f - alpha)));
    } else if (had_previous && is_weak_continuity_observation(observation.target_tier)) {
        decay_velocity_for_weak_observation();
    } else {
        target_velocity_px_per_sec_ = {};
    }

    observed_error_px_ = observation.aim_error_px;
    observed_at_seconds_ = observation.capture_time.value;
    target_tier_ = observation.target_tier;
    camera_motion_since_observation_px_ = {};
    last_aim_error_px_ = observation.aim_error_px;
    last_body_box_px_ = observation.body_box_px;
    last_has_body_box_ = observation.has_body_box;
}

void LegacyProjectionTracker::push_control_sample(const TrackerControlSample& sample) {
    if (observed_at_seconds_ <= 0.0 || sample.dt.value <= 0.0) {
        return;
    }
    const float pixels_per_stick =
        std::max(0.0f, config_.reticle_speed_px_per_sec) *
        static_cast<float>(sample.dt.value);
    camera_motion_since_observation_px_.x +=
        sample.sticks.final_output.x * pixels_per_stick;
    camera_motion_since_observation_px_.y +=
        -sample.sticks.final_output.y * pixels_per_stick;
}

TrackerSnapshot LegacyProjectionTracker::query(const TrackerQuery& query) const {
    TrackerSnapshot snapshot;
    if (observed_at_seconds_ <= 0.0 || query.query_time.value <= 0.0) {
        return snapshot;
    }
    const double age_seconds =
        std::max(0.0, query.query_time.value - observed_at_seconds_);
    if (age_seconds * 1000.0 >
        static_cast<double>(std::max(0.0f, config_.max_projection_age_ms))) {
        return snapshot;
    }

    snapshot.has_target = true;
    snapshot.source = TrackerSnapshotSource::Projected;
    snapshot.aim_error_px = {
        observed_error_px_.x +
            (target_velocity_px_per_sec_.x * static_cast<float>(age_seconds)) -
            camera_motion_since_observation_px_.x,
        observed_error_px_.y +
            (target_velocity_px_per_sec_.y * static_cast<float>(age_seconds)) -
            camera_motion_since_observation_px_.y};
    snapshot.observed_at = {observed_at_seconds_};
    snapshot.projection_age_ms = age_seconds * 1000.0;
    snapshot.assist_authority = common_native::AssistAuthority::AimCoast;
    snapshot.fire_authority = common_native::FireAuthority::None;

    if (last_has_body_box_) {
        const float shift_x = snapshot.aim_error_px.x - last_aim_error_px_.x;
        const float shift_y = snapshot.aim_error_px.y - last_aim_error_px_.y;
        snapshot.has_body_box = true;
        snapshot.body_box_px = {
            last_body_box_px_.x + shift_x,
            last_body_box_px_.y + shift_y,
            last_body_box_px_.w,
            last_body_box_px_.h};
    }

    return snapshot;
}

std::vector<TrackerDebugTrack> LegacyProjectionTracker::debug_tracks() const {
    return {};
}

float LegacyProjectionTracker::clamp_velocity(float value) const {
    const float limit = std::max(0.0f, config_.max_target_velocity_px_per_sec);
    if (limit <= 0.0f) {
        return 0.0f;
    }
    return std::max(-limit, std::min(limit, value));
}

void LegacyProjectionTracker::decay_velocity_for_weak_observation() {
    const float decay = clamp_unit(config_.weak_observation_velocity_decay);
    target_velocity_px_per_sec_.x = clamp_velocity(target_velocity_px_per_sec_.x * decay);
    target_velocity_px_per_sec_.y = clamp_velocity(target_velocity_px_per_sec_.y * decay);
}

}  // namespace tracking_native
