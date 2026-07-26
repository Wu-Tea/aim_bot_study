#include "pending_control_motion.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

const DeliveredPreRecoilSample& PendingControlMotion::at(
    std::size_t index) const noexcept {
    return samples_[(head_ + index) % kCapacity];
}

bool PendingControlMotion::observe(
    const DeliveredPreRecoilSample& sample) noexcept {
    const bool valid = std::isfinite(sample.delivered_at_seconds) &&
        sample.delivered_at_seconds > 0.0 &&
        pipeline_contract::finite(sample.pre_recoil_stick) &&
        sample.target_id != 0 && sample.delivered && sample.output_enabled;
    if (!valid) {
        reset();
        return false;
    }
    if (size_ != 0 &&
        sample.delivered_at_seconds <= at(size_ - 1).delivered_at_seconds) {
        reset();
        return false;
    }
    if (size_ < kCapacity) {
        samples_[(head_ + size_) % kCapacity] = sample;
        ++size_;
    } else {
        samples_[head_] = sample;
        head_ = (head_ + 1) % kCapacity;
    }
    return true;
}

PendingControlMotionEstimate PendingControlMotion::estimate(
    double now_seconds,
    float observation_age_ms,
    float response_scale_px_per_stick_second,
    std::uint64_t target_id) const noexcept {
    PendingControlMotionEstimate result;
    if (size_ == 0 || target_id == 0 || !std::isfinite(now_seconds) ||
        !std::isfinite(observation_age_ms) ||
        !std::isfinite(response_scale_px_per_stick_second) ||
        observation_age_ms <= 0.0f ||
        response_scale_px_per_stick_second <= 0.0f) {
        return result;
    }
    const double begin_seconds =
        now_seconds - static_cast<double>(observation_age_ms) / 1000.0;
    if (begin_seconds < at(0).delivered_at_seconds - 1.0e-9 ||
        now_seconds < at(size_ - 1).delivered_at_seconds) {
        return result;
    }

    pipeline_contract::Vec2f integral{};
    std::size_t held_index = 0;
    while (held_index + 1 < size_ &&
           at(held_index + 1).delivered_at_seconds <= begin_seconds) {
        ++held_index;
    }
    double cursor = begin_seconds;
    while (held_index < size_ && cursor < now_seconds) {
        const auto& held = at(held_index);
        if (held.target_id != target_id) return result;
        const double next_time = held_index + 1 < size_
            ? at(held_index + 1).delivered_at_seconds
            : now_seconds;
        const double end = std::min(now_seconds, next_time);
        if (end > cursor) {
            const float dt = static_cast<float>(end - cursor);
            integral.x += held.pre_recoil_stick.x * dt;
            integral.y += held.pre_recoil_stick.y * dt;
        }
        cursor = end;
        ++held_index;
    }
    if (cursor + 1.0e-9 < now_seconds) return result;

    const pipeline_contract::Vec2f displacement{
        integral.x * response_scale_px_per_stick_second,
        integral.y * response_scale_px_per_stick_second,
    };
    result.camera_displacement_px.fill(displacement);
    result.valid = pipeline_contract::finite(displacement);
    return result;
}

void PendingControlMotion::reset() noexcept {
    head_ = 0;
    size_ = 0;
}

}  // namespace controller_native
