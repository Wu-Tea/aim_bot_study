#include "kalman_tracker.h"

#include "projection_model.h"
#include "tracker_authority.h"

#include <algorithm>

namespace tracking_native {

namespace {

float clamp_unit(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

}  // namespace

KalmanTracker::KalmanTracker(controller_native::NativeTargetTrackerConfig config)
    : config_(config) {}

void KalmanTracker::reset() {
    ego_motion_.reset();
    observed_error_px_ = {};
    velocity_px_per_sec_ = {};
    body_box_px_ = {};
    has_body_box_ = false;
    observed_at_seconds_ = 0.0;
    target_tier_ = "none";
}

void KalmanTracker::ingest(const TrackerObservation& observation) {
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
        const common_native::Vec2f camera = ego_motion_.camera_motion_px();
        const float raw_vx = static_cast<float>(
            (observation.aim_error_px.x - observed_error_px_.x + camera.x) / dt);
        const float raw_vy = static_cast<float>(
            (observation.aim_error_px.y - observed_error_px_.y + camera.y) / dt);
        const float alpha = clamp_unit(config_.velocity_lowpass_alpha);
        velocity_px_per_sec_.x =
            clamp_velocity((velocity_px_per_sec_.x * alpha) + (raw_vx * (1.0f - alpha)));
        velocity_px_per_sec_.y =
            clamp_velocity((velocity_px_per_sec_.y * alpha) + (raw_vy * (1.0f - alpha)));
    } else if (had_previous && is_weak_continuity_observation(observation.target_tier)) {
        decay_velocity_for_weak_observation();
    } else {
        velocity_px_per_sec_ = {};
    }

    observed_error_px_ = observation.aim_error_px;
    body_box_px_ = observation.body_box_px;
    has_body_box_ = observation.has_body_box;
    observed_at_seconds_ = observation.capture_time.value;
    target_tier_ = observation.target_tier;
    ego_motion_.reset();
}

void KalmanTracker::push_control_sample(const TrackerControlSample& sample) {
    ego_motion_.record_stick(
        sample.sticks.final_output.x,
        sample.sticks.final_output.y,
        sample.dt.value,
        config_.reticle_speed_px_per_sec);
}

TrackerSnapshot KalmanTracker::query(const TrackerQuery& query) const {
    TrackerSnapshot snapshot;
    if (observed_at_seconds_ <= 0.0 || query.query_time.value <= 0.0) {
        return snapshot;
    }
    const double age_seconds = std::max(0.0, query.query_time.value - observed_at_seconds_);
    if (age_seconds * 1000.0 > static_cast<double>(std::max(0.0f, config_.max_projection_age_ms))) {
        return snapshot;
    }

    snapshot.has_target = true;
    snapshot.source = TrackerSnapshotSource::Projected;
    snapshot.aim_error_px = project_aim_error(
        observed_error_px_,
        velocity_px_per_sec_,
        ego_motion_.camera_motion_px(),
        age_seconds);
    snapshot.observed_at = {observed_at_seconds_};
    snapshot.projection_age_ms = age_seconds * 1000.0;
    snapshot.assist_authority = common_native::AssistAuthority::AimCoast;
    snapshot.fire_authority = common_native::FireAuthority::None;

    if (has_body_box_) {
        snapshot.has_body_box = true;
        snapshot.body_box_px = translate_box(
            body_box_px_,
            {
                snapshot.aim_error_px.x - observed_error_px_.x,
                snapshot.aim_error_px.y - observed_error_px_.y});
    }
    return snapshot;
}

std::vector<TrackerDebugTrack> KalmanTracker::debug_tracks() const {
    if (observed_at_seconds_ <= 0.0) {
        return {};
    }
    TrackerDebugTrack track;
    track.aim_error_px = observed_error_px_;
    track.velocity_px_per_sec = velocity_px_per_sec_;
    return {track};
}

float KalmanTracker::clamp_velocity(float value) const {
    const float limit = std::max(0.0f, config_.max_target_velocity_px_per_sec);
    if (limit <= 0.0f) {
        return 0.0f;
    }
    return std::max(-limit, std::min(limit, value));
}

void KalmanTracker::decay_velocity_for_weak_observation() {
    const float decay = clamp_unit(config_.weak_observation_velocity_decay);
    velocity_px_per_sec_.x = clamp_velocity(velocity_px_per_sec_.x * decay);
    velocity_px_per_sec_.y = clamp_velocity(velocity_px_per_sec_.y * decay);
}

}  // namespace tracking_native
