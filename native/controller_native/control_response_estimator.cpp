#include "control_response_estimator.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

ControlResponseEstimator::ControlResponseEstimator(ControlResponseEstimatorConfig config)
    : config_(config) {}

void ControlResponseEstimator::begin_ads_epoch(std::uint64_t epoch) noexcept {
    if (estimate_.ads_epoch == epoch) return;
    estimate_.ads_epoch = epoch;
    estimate_.confidence *= config_.epoch_confidence_scale;
    estimate_.accepted_samples = 0;
}

bool ControlResponseEstimator::update(const ControlResponseSample& sample) noexcept {
    const bool eligible = sample.clean_window && !sample.ambiguous &&
        std::isfinite(sample.left_stick_axis) &&
        std::isfinite(sample.isolated_response_px_per_second) &&
        std::fabs(sample.left_stick_axis) >= config_.minimum_excitation;
    if (!eligible) {
        estimate_.confidence *= config_.frozen_confidence_decay;
        return false;
    }

    const float measured_scale = std::clamp(
        sample.isolated_response_px_per_second / sample.left_stick_axis,
        -config_.max_scale_px_per_stick_second,
        config_.max_scale_px_per_stick_second);
    if (estimate_.accepted_samples == 0 && estimate_.scale_px_per_stick_second == 0.0f) {
        estimate_.scale_px_per_stick_second = measured_scale;
    } else {
        estimate_.scale_px_per_stick_second += config_.scale_alpha *
            (measured_scale - estimate_.scale_px_per_stick_second);
    }
    estimate_.confidence += config_.confidence_alpha * (1.0f - estimate_.confidence);
    ++estimate_.accepted_samples;
    return true;
}

ControlResponseEstimate ControlResponseEstimator::estimate() const noexcept {
    return estimate_;
}

void ControlResponseEstimator::reset() noexcept {
    estimate_ = {};
}

}  // namespace controller_native
