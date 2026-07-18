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

}  // namespace

AimResponseEstimator::AimResponseEstimator(AimResponseEstimatorConfig config)
    : config_(config), learned_scale_(config.fallback_scale) {}

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
        std::isfinite(interval.observed_error_rate_px_per_sec.y);
}

bool AimResponseEstimator::update(
    const AimResponseInterval& interval) noexcept {
    if (interval.target_id != target_id_) {
        begin_target(interval.target_id);
    }
    if (!eligible(interval)) {
        has_previous_ = false;
        return false;
    }
    if (!has_previous_) {
        previous_ = interval;
        has_previous_ = true;
        return false;
    }

    const pipeline_contract::Vec2f delta_stick = subtract(
        interval.average_final_stick, previous_.average_final_stick);
    const pipeline_contract::Vec2f delta_rate = control_coordinates(subtract(
        interval.observed_error_rate_px_per_sec,
        previous_.observed_error_rate_px_per_sec));
    previous_ = interval;
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
        learned_scale_ * (1.0f - relative),
        learned_scale_ * (1.0f + relative));
    learned_scale_ += std::clamp(config_.scale_alpha, 0.0f, 1.0f) *
        (sample_scale - learned_scale_);
    learned_scale_ = std::clamp(
        learned_scale_, config_.minimum_scale, config_.maximum_scale);
    confidence_ += std::clamp(config_.confidence_alpha, 0.0f, 1.0f) *
        (1.0f - confidence_);
    ++accepted_samples_;
    return true;
}

void AimResponseEstimator::begin_target(std::uint64_t target_id) noexcept {
    target_id_ = target_id;
    has_previous_ = false;
}

AimResponseEstimate AimResponseEstimator::estimate() const noexcept {
    const float confidence = std::clamp(confidence_, 0.0f, 1.0f);
    return {
        config_.fallback_scale + confidence *
            (learned_scale_ - config_.fallback_scale),
        confidence,
        accepted_samples_,
    };
}

void AimResponseEstimator::reset() noexcept {
    previous_ = {};
    target_id_ = 0;
    learned_scale_ = config_.fallback_scale;
    confidence_ = 0.0f;
    accepted_samples_ = 0;
    has_previous_ = false;
}

}  // namespace controller_native
