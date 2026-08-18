#include "bodylock_target_motion_observer.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

float median(float a, float b, float c) noexcept {
    return a + b + c - std::min({a, b, c}) - std::max({a, b, c});
}

}  // namespace

BodylockTargetMotionObserver::BodylockTargetMotionObserver(
    BodylockTargetMotionObserverConfig config) noexcept
    : config_(config) {}

bool BodylockTargetMotionObserver::update(
    const BodylockTargetMotionObservation& observation) noexcept {
    if (observation.target_id == 0 ||
        !observation.direct_person_observation ||
        !std::isfinite(observation.capture_seconds) ||
        !std::isfinite(observation.interval_seconds) ||
        observation.interval_seconds < config_.minimum_interval_seconds ||
        observation.interval_seconds > config_.maximum_interval_seconds ||
        !pipeline_contract::finite(
            observation.observed_screen_rate_px_per_sec) ||
        !pipeline_contract::finite(observation.average_delivered_stick) ||
        !std::isfinite(observation.response_px_per_stick_second) ||
        observation.response_px_per_stick_second <
            config_.minimum_response_px_per_stick_second ||
        observation.response_px_per_stick_second >
            config_.maximum_response_px_per_stick_second ||
        !std::isfinite(observation.reliability)) {
        return false;
    }

    if (target_id_ != observation.target_id) {
        begin_target(observation.target_id);
    }
    if (last_capture_seconds_ > 0.0 &&
        observation.capture_seconds <= last_capture_seconds_) {
        return false;
    }

    const float response = observation.response_px_per_stick_second;
    // Screen Y grows downward while positive XInput Y moves the camera up.
    // Removing the causally aligned camera work therefore uses opposite signs
    // on Y and X:
    //   target_x = screen_x + R * stick_x
    //   target_y = screen_y - R * stick_y
    pipeline_contract::Vec2f measured_motion_stick{
        observation.average_delivered_stick.x +
            observation.observed_screen_rate_px_per_sec.x / response,
        observation.average_delivered_stick.y -
            observation.observed_screen_rate_px_per_sec.y / response,
    };
    const float maximum_motion = std::max(
        0.0f, config_.maximum_target_motion_stick);
    measured_motion_stick.x = std::clamp(
        measured_motion_stick.x, -maximum_motion, maximum_motion);
    measured_motion_stick.y = std::clamp(
        measured_motion_stick.y, -maximum_motion, maximum_motion);

    measurements_[measurement_write_index_] = measured_motion_stick;
    measurement_write_index_ =
        (measurement_write_index_ + 1) % measurements_.size();
    measurement_count_ = std::min(
        measurements_.size(), measurement_count_ + 1);
    ++accepted_samples_;
    last_capture_seconds_ = observation.capture_seconds;
    last_aligned_delivered_stick_ = observation.average_delivered_stick;
    const float evidence = std::clamp(observation.reliability, 0.0f, 1.0f);
    confidence_ += 0.25f * (evidence - confidence_);

    const std::size_t minimum_samples = std::clamp<std::size_t>(
        config_.minimum_samples, 1, measurements_.size());
    if (measurement_count_ < minimum_samples) return true;

    const auto robust = robust_measurement();
    if (!initialized_) {
        filtered_motion_stick_ = robust;
        initialized_ = true;
        return true;
    }
    filtered_motion_stick_.x = slew_axis(
        filtered_motion_stick_.x,
        robust.x,
        observation.interval_seconds);
    filtered_motion_stick_.y = slew_axis(
        filtered_motion_stick_.y,
        robust.y,
        observation.interval_seconds);
    return true;
}

BodylockTargetMotionEstimate BodylockTargetMotionObserver::estimate(
    std::uint64_t target_id,
    double now_seconds) const noexcept {
    BodylockTargetMotionEstimate result{};
    result.accepted_samples = accepted_samples_;
    if (!initialized_ || target_id == 0 || target_id != target_id_ ||
        !std::isfinite(now_seconds) || last_capture_seconds_ <= 0.0 ||
        now_seconds < last_capture_seconds_ ||
        now_seconds - last_capture_seconds_ >
            static_cast<double>(std::max(
                0.0f, config_.maximum_hold_seconds))) {
        return result;
    }
    result.target_motion_stick = filtered_motion_stick_;
    result.aligned_delivered_stick = last_aligned_delivered_stick_;
    result.confidence = std::clamp(confidence_, 0.0f, 1.0f);
    result.valid = pipeline_contract::finite(result.target_motion_stick) &&
        pipeline_contract::finite(result.aligned_delivered_stick);
    return result;
}

void BodylockTargetMotionObserver::begin_target(
    std::uint64_t target_id) noexcept {
    measurements_ = {};
    measurement_count_ = 0;
    measurement_write_index_ = 0;
    filtered_motion_stick_ = {};
    last_aligned_delivered_stick_ = {};
    target_id_ = target_id;
    last_capture_seconds_ = 0.0;
    confidence_ = 0.0f;
    accepted_samples_ = 0;
    initialized_ = false;
}

void BodylockTargetMotionObserver::reset() noexcept {
    begin_target(0);
}

pipeline_contract::Vec2f
BodylockTargetMotionObserver::robust_measurement() const noexcept {
    if (measurement_count_ == 0) return {};
    if (measurement_count_ == 1) return measurements_[0];
    if (measurement_count_ == 2) {
        return {
            0.5f * (measurements_[0].x + measurements_[1].x),
            0.5f * (measurements_[0].y + measurements_[1].y),
        };
    }
    return {
        median(
            measurements_[0].x,
            measurements_[1].x,
            measurements_[2].x),
        median(
            measurements_[0].y,
            measurements_[1].y,
            measurements_[2].y),
    };
}

float BodylockTargetMotionObserver::slew_axis(
    float current,
    float target,
    float dt_seconds) const noexcept {
    const bool reversing = current * target < 0.0f;
    const bool decaying = reversing || std::fabs(target) < std::fabs(current);
    const float rate = std::max(
        0.0f,
        decaying
            ? config_.decay_slew_stick_per_second
            : config_.rise_slew_stick_per_second);
    const float effective_target = reversing ? 0.0f : target;
    const float step = rate * std::max(0.0f, dt_seconds);
    return current + std::clamp(
        effective_target - current, -step, step);
}

}  // namespace controller_native
