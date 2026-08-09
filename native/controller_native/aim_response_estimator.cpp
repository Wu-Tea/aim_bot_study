#include "aim_response_estimator.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
namespace {

pipeline_contract::Vec2f subtract(
    pipeline_contract::Vec2f left,
    pipeline_contract::Vec2f right) noexcept {
    return {left.x - right.x, left.y - right.y};
}

float dot(pipeline_contract::Vec2f left,
          pipeline_contract::Vec2f right) noexcept {
    return left.x * right.x + left.y * right.y;
}

pipeline_contract::Vec2f control_coordinates(
    pipeline_contract::Vec2f screen_rate) noexcept {
    return {screen_rate.x, -screen_rate.y};
}

float smoothstep(float value) noexcept {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

}  // namespace

AimResponseEstimator::AimResponseEstimator(AimResponseEstimatorConfig config)
    : config_(config) {
    free_region_.learned_scale = config.fallback_scale;
    slow_region_.learned_scale = config.fallback_scale;
}

float aim_response_slow_zone_weight(
    pipeline_contract::Vec2f error_px,
    pipeline_contract::Vec2f target_size_px) noexcept {
    if (!pipeline_contract::finite(error_px) ||
        !pipeline_contract::finite(target_size_px) ||
        target_size_px.x <= 0.0f || target_size_px.y <= 0.0f) {
        return 0.0f;
    }
    const float radius_x = std::max(8.0f, target_size_px.x * 0.5f);
    const float radius_y = std::max(12.0f, target_size_px.y * 0.5f);
    const float normalized_radius = std::hypot(
        error_px.x / radius_x,
        error_px.y / radius_y);
    constexpr float kFullSlowRadius = 0.65f;
    constexpr float kFreeResponseRadius = 1.35f;
    return smoothstep(
        (kFreeResponseRadius - normalized_radius) /
        (kFreeResponseRadius - kFullSlowRadius));
}

bool AimResponseEstimator::eligible(
    const AimResponseInterval& interval) const noexcept {
    return interval.target_id != 0 && interval.observed &&
        !interval.manual_ambiguous &&
        std::isfinite(interval.dt_seconds) &&
        interval.dt_seconds >= config_.minimum_interval_seconds &&
        interval.dt_seconds <= config_.maximum_interval_seconds &&
        interval.reliability >= config_.minimum_reliability &&
        std::isfinite(interval.target_acceleration_px_per_sec2) &&
        std::fabs(interval.target_acceleration_px_per_sec2) <=
            config_.maximum_target_acceleration &&
        std::isfinite(interval.average_final_stick.x) &&
        std::isfinite(interval.average_final_stick.y) &&
        std::isfinite(interval.observed_error_rate_px_per_sec.x) &&
        std::isfinite(interval.observed_error_rate_px_per_sec.y) &&
        std::isfinite(interval.slow_zone_weight);
}

bool AimResponseEstimator::update_region(
    RegionState& region,
    const AimResponseInterval& interval) noexcept {
    if (!region.has_previous) {
        region.previous = interval;
        region.has_previous = true;
        return false;
    }

    const pipeline_contract::Vec2f delta_stick = subtract(
        interval.average_final_stick, region.previous.average_final_stick);
    const pipeline_contract::Vec2f delta_rate = control_coordinates(subtract(
        interval.observed_error_rate_px_per_sec,
        region.previous.observed_error_rate_px_per_sec));
    region.previous = interval;
    const float excitation = dot(delta_stick, delta_stick);
    const float minimum_excitation = config_.minimum_command_delta *
        config_.minimum_command_delta;
    if (excitation < minimum_excitation) return false;

    float sample_scale = -dot(delta_rate, delta_stick) / excitation;
    if (!std::isfinite(sample_scale) ||
        sample_scale < config_.minimum_scale ||
        sample_scale > config_.maximum_scale) {
        return false;
    }
    const float relative = std::clamp(
        config_.maximum_relative_sample_change, 0.05f, 1.0f);
    sample_scale = std::clamp(
        sample_scale,
        region.learned_scale * (1.0f - relative),
        region.learned_scale * (1.0f + relative));
    region.learned_scale += std::clamp(config_.scale_alpha, 0.0f, 1.0f) *
        (sample_scale - region.learned_scale);
    region.learned_scale = std::clamp(
        region.learned_scale, config_.minimum_scale, config_.maximum_scale);
    region.confidence += std::clamp(config_.confidence_alpha, 0.0f, 1.0f) *
        (1.0f - region.confidence);
    ++region.accepted_samples;
    return true;
}

bool AimResponseEstimator::update(
    const AimResponseInterval& interval) noexcept {
    if (interval.target_id != target_id_) {
        begin_target(interval.target_id);
    }
    if (!eligible(interval)) {
        free_region_.has_previous = false;
        slow_region_.has_previous = false;
        has_previous_region_ = false;
        return false;
    }
    const bool slow = std::clamp(interval.slow_zone_weight, 0.0f, 1.0f) >= 0.5f;
    RegionState& region = slow ? slow_region_ : free_region_;
    if (!has_previous_region_ || slow != previous_region_was_slow_) {
        region.has_previous = false;
    }
    previous_region_was_slow_ = slow;
    has_previous_region_ = true;
    return update_region(region, interval);
}

void AimResponseEstimator::begin_target(std::uint64_t target_id) noexcept {
    target_id_ = target_id;
    free_region_.has_previous = false;
    slow_region_.has_previous = false;
    has_previous_region_ = false;
}

AimResponseEstimate AimResponseEstimator::estimate(float slow_zone_weight) const noexcept {
    const float free_confidence = std::clamp(
        free_region_.confidence, 0.0f, 1.0f);
    const float free_scale = config_.fallback_scale + free_confidence *
        (free_region_.learned_scale - config_.fallback_scale);
    const float slow_confidence = std::clamp(
        slow_region_.confidence, 0.0f, 1.0f);
    // Until a slowdown-region response has been measured, inherit the learned
    // free-space response. No game-specific attenuation is assumed.
    const float slow_scale = free_scale + slow_confidence *
        (slow_region_.learned_scale - free_scale);
    const float weight = std::clamp(slow_zone_weight, 0.0f, 1.0f);
    const float confidence = free_confidence + weight *
        (std::max(free_confidence, slow_confidence) - free_confidence);
    return {
        free_scale + weight * (slow_scale - free_scale),
        confidence,
        free_region_.accepted_samples + slow_region_.accepted_samples,
    };
}

void AimResponseEstimator::reset() noexcept {
    free_region_ = {};
    slow_region_ = {};
    free_region_.learned_scale = config_.fallback_scale;
    slow_region_.learned_scale = config_.fallback_scale;
    target_id_ = 0;
    previous_region_was_slow_ = false;
    has_previous_region_ = false;
}

}  // namespace controller_native
