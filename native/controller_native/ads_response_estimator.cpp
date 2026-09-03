#include "ads_response_estimator.h"

#include <algorithm>
#include <array>
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

AdsResponseEstimator::AdsResponseEstimator(AimResponseEstimatorConfig config)
    : config_(config) {
    free_region_.learned_scale = config.fallback_scale;
    slow_region_.learned_scale = config.fallback_scale;
}

bool AdsResponseEstimator::eligible(
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

bool AdsResponseEstimator::update_region(
    RegionState& region,
    const AimResponseInterval& interval,
    bool slowdown_region) noexcept {
    const float minimum_excitation = config_.minimum_command_delta *
        config_.minimum_command_delta;
    std::array<float, kExcitationHistoryCapacity> candidate_scales{};
    std::size_t candidate_count = 0;
    for (std::size_t index = 0; index < region.history_count; ++index) {
        const auto& anchor = region.history[index];
        const auto delta_stick = subtract(
            interval.average_final_stick, anchor.average_final_stick);
        auto regression_command = delta_stick;
        auto regression_rate = control_coordinates(subtract(
            interval.observed_error_rate_px_per_sec,
            anchor.observed_error_rate_px_per_sec));
        float response_offset = 0.0f;
        if (slowdown_region) {
            const float current_weight = std::clamp(
                interval.slow_zone_weight, 0.0f, 1.0f);
            const float anchor_weight = std::clamp(
                anchor.slow_zone_weight, 0.0f, 1.0f);
            regression_command = subtract(
                {interval.average_final_stick.x * current_weight,
                 interval.average_final_stick.y * current_weight},
                {anchor.average_final_stick.x * anchor_weight,
                 anchor.average_final_stick.y * anchor_weight});
            response_offset = estimate(0.0f).scale_px_per_stick_second;
            regression_rate = {
                regression_rate.x + response_offset * delta_stick.x,
                regression_rate.y + response_offset * delta_stick.y,
            };
        }

        const float excitation = dot(regression_command, regression_command);
        if (excitation < minimum_excitation) continue;
        const float candidate_scale = response_offset -
            dot(regression_rate, regression_command) / excitation;
        if (std::isfinite(candidate_scale) &&
            candidate_scale >= config_.minimum_scale &&
            candidate_scale <= config_.maximum_scale) {
            candidate_scales[candidate_count++] = candidate_scale;
        }
    }

    region.history[region.history_next] = interval;
    region.history_next =
        (region.history_next + 1) % kExcitationHistoryCapacity;
    region.history_count = std::min(
        region.history_count + 1, kExcitationHistoryCapacity);

    // One long-span pair can confuse target acceleration with camera
    // response. Require several independently anchored slopes to agree.
    if (candidate_count < 3) return false;
    std::sort(
        candidate_scales.begin(),
        candidate_scales.begin() + candidate_count);
    const float sample_scale = candidate_scales[candidate_count / 2];
    const float lower_quartile = candidate_scales[candidate_count / 4];
    const float upper_quartile =
        candidate_scales[(candidate_count * 3) / 4];
    if (upper_quartile - lower_quartile > sample_scale * 0.20f) {
        return false;
    }

    const float relative = std::clamp(
        config_.maximum_relative_sample_change, 0.05f, 1.0f);
    const float bounded_sample_scale = std::clamp(
        sample_scale,
        region.learned_scale * (1.0f - relative),
        region.learned_scale * (1.0f + relative));
    const float evidence_count = static_cast<float>(
        std::min<std::size_t>(candidate_count, 4));
    const float scale_alpha = 1.0f - std::pow(
        1.0f - std::clamp(config_.scale_alpha, 0.0f, 1.0f),
        evidence_count);
    const float confidence_alpha = 1.0f - std::pow(
        1.0f - std::clamp(config_.confidence_alpha, 0.0f, 1.0f),
        evidence_count);
    region.learned_scale += scale_alpha *
        (bounded_sample_scale - region.learned_scale);
    region.learned_scale = std::clamp(
        region.learned_scale, config_.minimum_scale, config_.maximum_scale);
    region.confidence += confidence_alpha * (1.0f - region.confidence);
    ++region.accepted_samples;
    return true;
}

bool AdsResponseEstimator::update(
    const AimResponseInterval& interval) noexcept {
    if (interval.target_id != target_id_) {
        begin_target(interval.target_id);
    }
    if (!eligible(interval)) {
        clear_histories();
        has_previous_region_ = false;
        return false;
    }

    // Start endpoint identification as soon as the spatial response blend is
    // nonzero; waiting for a 0.5 region switch discards most ADS arrival data.
    const bool slow = std::clamp(
        interval.slow_zone_weight, 0.0f, 1.0f) > 0.0f;
    RegionState& region = slow ? slow_region_ : free_region_;
    if (!has_previous_region_ || slow != previous_region_was_slow_) {
        region.history_count = 0;
        region.history_next = 0;
    }
    previous_region_was_slow_ = slow;
    has_previous_region_ = true;
    return update_region(region, interval, slow);
}

void AdsResponseEstimator::begin_target(std::uint64_t target_id) noexcept {
    target_id_ = target_id;
    clear_histories();
    has_previous_region_ = false;
}

AimResponseEstimate AdsResponseEstimator::estimate(
    float slow_zone_weight) const noexcept {
    const float free_confidence = std::clamp(
        free_region_.confidence, 0.0f, 1.0f);
    const float free_scale = config_.fallback_scale + free_confidence *
        (free_region_.learned_scale - config_.fallback_scale);
    const float slow_confidence = std::clamp(
        slow_region_.confidence, 0.0f, 1.0f);
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

void AdsResponseEstimator::reset() noexcept {
    free_region_ = {};
    slow_region_ = {};
    free_region_.learned_scale = config_.fallback_scale;
    slow_region_.learned_scale = config_.fallback_scale;
    target_id_ = 0;
    previous_region_was_slow_ = false;
    has_previous_region_ = false;
}

void AdsResponseEstimator::clear_histories() noexcept {
    free_region_.history_count = 0;
    free_region_.history_next = 0;
    slow_region_.history_count = 0;
    slow_region_.history_next = 0;
}

}  // namespace controller_native
