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
    reset();
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
        region.evidence_count = region.evidence_next = 0;
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
    const float correlation_sample = -dot(delta_rate, delta_stick);
    const float rate_energy_sample = dot(delta_rate, delta_rate);
    if (!std::isfinite(excitation) || excitation <= 0.f ||
        !std::isfinite(correlation_sample) || !std::isfinite(rate_energy_sample) ||
        excitation < minimum_excitation) return false;

    // Pool signed evidence BEFORE deciding whether a response is plausible.
    // Accepting positive adjacent slopes while discarding negative ones lets
    // uncorrelated noise/time-misaligned camera motion build false confidence.
    region.evidence[region.evidence_next] = {
        excitation, correlation_sample, rate_energy_sample};
    region.evidence_next = (region.evidence_next+1)%region.evidence.size();
    region.evidence_count = std::min(region.evidence_count+1, region.evidence.size());
    if (region.evidence_count < 4) return false;
    double command_energy=0, correlation=0, rate_energy=0;
    std::array<float, 8> slopes{};
    for (std::size_t i=0;i<region.evidence_count;++i) {
        command_energy+=region.evidence[i].excitation;
        correlation+=region.evidence[i].correlation;
        rate_energy+=region.evidence[i].rate_energy;
        slopes[i]=region.evidence[i].correlation/region.evidence[i].excitation;
    }
    // Measure how much of the observed change the camera model explains in
    // both axes. Confidence follows fit quality, not merely sample count.
    const double explained = correlation*correlation;
    if (correlation <= 0 || rate_energy <= 0) return false;
    std::sort(slopes.begin(),slopes.begin()+region.evidence_count);
    float sample_scale = slopes[region.evidence_count/2];
    // An isolated acceleration/outlier must not bias all subsequent window
    // updates. Down-weight disagreement around the robust slope, including
    // contrary evidence (which was never filtered out of this window).
    const float spread=slopes[region.evidence_count*3/4]-slopes[region.evidence_count/4];
    if (!std::isfinite(sample_scale) ||
        sample_scale < config_.minimum_scale ||
        sample_scale > config_.maximum_scale) {
        return false;
    }
    const float quality=static_cast<float>(std::clamp(
        explained/(command_energy*rate_energy),0.0,1.0)) *
        sample_scale*sample_scale/(sample_scale*sample_scale+spread*spread);
    const float relative = std::clamp(
        config_.maximum_relative_sample_change, 0.05f, 1.0f);
    sample_scale = std::clamp(
        sample_scale,
        region.learned_scale * (1.0f - relative),
        region.learned_scale * (1.0f + relative));
    // Preserve the existing coefficients at the 5 ms reference interval.
    // Accepted observations at a higher cadence must not accelerate learning
    // just because more updates fit into the same elapsed time.
    const float exposure = interval.dt_seconds/.005f;
    const float scale_alpha = 1.f-std::pow(1.f-std::clamp(config_.scale_alpha,0.f,1.f),exposure);
    const float confidence_alpha = 1.f-std::pow(1.f-std::clamp(config_.confidence_alpha,0.f,1.f),exposure);
    region.learned_scale += scale_alpha * quality *
        (sample_scale - region.learned_scale);
    region.learned_scale = std::clamp(
        region.learned_scale, config_.minimum_scale, config_.maximum_scale);
    region.confidence += confidence_alpha *
        (quality - region.confidence);
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
    // A supplied local prior is independent of free-region learning. Without
    // one, preserve the established free-region inheritance policy.
    const float slow_prior = config_.slow_fallback_scale > 0.0f
        ? config_.slow_fallback_scale : free_scale;
    const float slow_scale = slow_prior + slow_confidence *
        (slow_region_.learned_scale - slow_prior);
    const float weight = std::clamp(slow_zone_weight, 0.0f, 1.0f);
    const float confidence = free_confidence + weight *
        (std::max(free_confidence, slow_confidence) - free_confidence);
    return {
        free_scale + weight * (slow_scale - free_scale),
        confidence,
        free_region_.accepted_samples + slow_region_.accepted_samples,
    };
}

AimResponseEstimate AimResponseEstimator::learning_region(bool slow) const noexcept {
    const auto& region = slow ? slow_region_ : free_region_;
    return {estimate(slow ? 1.0f : 0.0f).scale_px_per_stick_second,
        region.confidence, region.accepted_samples, region.learned_scale};
}

void AimResponseEstimator::reset() noexcept {
    free_region_ = {};
    slow_region_ = {};
    free_region_.learned_scale = config_.fallback_scale;
    slow_region_.learned_scale = config_.slow_fallback_scale > 0.0f
        ? config_.slow_fallback_scale : config_.fallback_scale;
    target_id_ = 0;
    previous_region_was_slow_ = false;
    has_previous_region_ = false;
}

}  // namespace controller_native
