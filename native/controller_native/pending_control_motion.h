#pragma once

#include "causal_mix_evaluator.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace controller_native {

struct DeliveredPreRecoilSample {
    double delivered_at_seconds = 0.0;
    pipeline_contract::Vec2f pre_recoil_stick{};
    std::uint64_t target_id = 0;
    bool delivered = false;
    bool output_enabled = true;
};

struct PendingControlMotionEstimate {
    std::array<pipeline_contract::Vec2f, kCausalMixHorizonCount>
        camera_displacement_px{};
    bool valid = false;
};

pipeline_contract::Vec2f delivered_camera_work_px(
    pipeline_contract::Vec2f control_displacement_px) noexcept;

pipeline_contract::Vec2f remaining_work_after_delivery(
    pipeline_contract::Vec2f error_px,
    pipeline_contract::Vec2f delivered_work_px) noexcept;

class PendingControlMotion {
public:
    bool observe(const DeliveredPreRecoilSample& sample) noexcept;
    PendingControlMotionEstimate estimate(
        double now_seconds,
        float observation_age_ms,
        float response_scale_px_per_stick_second,
        std::uint64_t target_id) const noexcept;
    PendingControlMotionEstimate estimate_between(
        double begin_seconds,
        double end_seconds,
        float response_scale_px_per_stick_second,
        std::uint64_t target_id) const noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t kCapacity = 256;
    const DeliveredPreRecoilSample& at(std::size_t index) const noexcept;

    std::array<DeliveredPreRecoilSample, kCapacity> samples_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

}  // namespace controller_native
