#pragma once

#include "pipeline_contract/target_plan.h"

#include <cstdint>

namespace controller_native {

struct AimResponseInterval {
    pipeline_contract::Vec2f average_final_stick{};
    pipeline_contract::Vec2f observed_error_rate_px_per_sec{};
    std::uint64_t target_id = 0;
    float dt_seconds = 0.0f;
    float reliability = 0.0f;
    float target_acceleration_px_per_sec2 = 0.0f;
    // 0 is outside the target-centered slowdown region and 1 is fully inside.
    // The estimator, rather than a fixed multiplier, owns the measured response
    // difference between those regions.
    float slow_zone_weight = 0.0f;
    bool observed = false;
    bool manual_ambiguous = false;
};

struct AimResponseEstimate {
    float scale_px_per_stick_second = 500.0f;
    float confidence = 0.0f;
    std::uint32_t accepted_samples = 0;
};

float aim_response_slow_zone_weight(
    pipeline_contract::Vec2f error_px,
    pipeline_contract::Vec2f target_size_px) noexcept;

struct AimResponseEstimatorConfig {
    float fallback_scale = 500.0f;
    float minimum_scale = 80.0f;
    // High-sensitivity games measured in pixels at the capture resolution can
    // legitimately exceed 2,000 px/(stick*s). The prior 1,200 ceiling made
    // those plants unidentifiable even with a correctly aligned sample.
    float maximum_scale = 4000.0f;
    float minimum_reliability = 0.75f;
    float minimum_command_delta = 0.04f;
    float maximum_target_acceleration = 800.0f;
    float minimum_interval_seconds = 0.005f;
    float maximum_interval_seconds = 0.030f;
    float scale_alpha = 0.18f;
    float confidence_alpha = 0.08f;
    float maximum_relative_sample_change = 0.50f;
};

class AimResponseEstimator {
public:
    explicit AimResponseEstimator(AimResponseEstimatorConfig config = {});

    bool update(const AimResponseInterval& interval) noexcept;
    void begin_target(std::uint64_t target_id) noexcept;
    AimResponseEstimate estimate(float slow_zone_weight = 0.0f) const noexcept;
    void reset() noexcept;

private:
    struct RegionState {
        AimResponseInterval previous{};
        float learned_scale = 500.0f;
        float confidence = 0.0f;
        std::uint32_t accepted_samples = 0;
        bool has_previous = false;
    };

    bool eligible(const AimResponseInterval& interval) const noexcept;
    bool update_region(
        RegionState& region,
        const AimResponseInterval& interval) noexcept;

    AimResponseEstimatorConfig config_{};
    RegionState free_region_{};
    RegionState slow_region_{};
    std::uint64_t target_id_ = 0;
    bool previous_region_was_slow_ = false;
    bool has_previous_region_ = false;
};

}  // namespace controller_native
