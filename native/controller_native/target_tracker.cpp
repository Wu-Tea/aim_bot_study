#include "target_tracker.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

float clamp_unit(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

}  // namespace

NativeGamepadTargetTracker::NativeGamepadTargetTracker(NativeTargetTrackerConfig config)
    : config_(std::move(config)) {}

void NativeGamepadTargetTracker::reset() {
    observed_dx_ = 0.0f;
    observed_dy_ = 0.0f;
    observed_at_seconds_ = 0.0;
    target_tier_ = "none";
    target_velocity_x_ = 0.0f;
    target_velocity_y_ = 0.0f;
    camera_dx_since_observation_ = 0.0f;
    camera_dy_since_observation_ = 0.0f;
}

void NativeGamepadTargetTracker::update_observation(
    const NativeTargetTrackerObservation& observation) {
    if (!observation.has_target || observation.observed_at_seconds <= 0.0) {
        reset();
        return;
    }

    const bool had_previous = observed_at_seconds_ > 0.0;
    if (had_previous &&
        observation.observed_at_seconds > observed_at_seconds_ &&
        tracking_native::is_strong_observation(target_tier_) &&
        tracking_native::is_strong_observation(observation.target_tier)) {
        const double dt = observation.observed_at_seconds - observed_at_seconds_;
        const float raw_vx = static_cast<float>(
            (observation.dx - observed_dx_ + camera_dx_since_observation_) / dt);
        const float raw_vy = static_cast<float>(
            (observation.dy - observed_dy_ + camera_dy_since_observation_) / dt);
        const float alpha = clamp_unit(config_.velocity_lowpass_alpha);
        target_velocity_x_ = clamp_velocity((target_velocity_x_ * alpha) + (raw_vx * (1.0f - alpha)));
        target_velocity_y_ = clamp_velocity((target_velocity_y_ * alpha) + (raw_vy * (1.0f - alpha)));
    } else if (
        had_previous &&
        tracking_native::is_weak_continuity_observation(observation.target_tier)) {
        decay_velocity_for_weak_observation();
    } else {
        target_velocity_x_ = 0.0f;
        target_velocity_y_ = 0.0f;
    }

    observed_dx_ = observation.dx;
    observed_dy_ = observation.dy;
    observed_at_seconds_ = observation.observed_at_seconds;
    target_tier_ = observation.target_tier;
    camera_dx_since_observation_ = 0.0f;
    camera_dy_since_observation_ = 0.0f;
}

void NativeGamepadTargetTracker::record_output(
    float right_x,
    float right_y,
    double dt_seconds) {
    if (observed_at_seconds_ <= 0.0 || dt_seconds <= 0.0) {
        return;
    }
    const float pixels_per_stick =
        std::max(0.0f, config_.reticle_speed_px_per_sec) *
        static_cast<float>(dt_seconds);
    camera_dx_since_observation_ += right_x * pixels_per_stick;
    camera_dy_since_observation_ += -right_y * pixels_per_stick;
}

std::optional<NativeTargetProjection> NativeGamepadTargetTracker::project(
    double timestamp_seconds) const {
    if (observed_at_seconds_ <= 0.0 || timestamp_seconds <= 0.0) {
        return std::nullopt;
    }
    const double age_seconds = std::max(0.0, timestamp_seconds - observed_at_seconds_);
    if (age_seconds * 1000.0 > static_cast<double>(std::max(0.0f, config_.max_projection_age_ms))) {
        return std::nullopt;
    }

    NativeTargetProjection projection;
    projection.dx =
        observed_dx_ +
        (target_velocity_x_ * static_cast<float>(age_seconds)) -
        camera_dx_since_observation_;
    projection.dy =
        observed_dy_ +
        (target_velocity_y_ * static_cast<float>(age_seconds)) -
        camera_dy_since_observation_;
    projection.observed_at_seconds = observed_at_seconds_;
    return projection;
}

float NativeGamepadTargetTracker::clamp_velocity(float value) const {
    const float limit = std::max(0.0f, config_.max_target_velocity_px_per_sec);
    if (limit <= 0.0f) {
        return 0.0f;
    }
    return std::max(-limit, std::min(limit, value));
}

void NativeGamepadTargetTracker::decay_velocity_for_weak_observation() {
    const float decay = clamp_unit(config_.weak_observation_velocity_decay);
    target_velocity_x_ = clamp_velocity(target_velocity_x_ * decay);
    target_velocity_y_ = clamp_velocity(target_velocity_y_ * decay);
}

}  // namespace controller_native
