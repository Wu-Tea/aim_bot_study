#pragma once

#include "aim_response_estimator.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace controller_native {

// ADS arrival changes stick demand smoothly, so adjacent samples may never
// cross the response estimator's anti-noise excitation threshold. This
// estimator accumulates several older ADS-only anchors while retaining the
// same eligibility and target-lifecycle boundaries as AimResponseEstimator.
class AdsResponseEstimator {
public:
    explicit AdsResponseEstimator(AimResponseEstimatorConfig config = {});

    bool update(const AimResponseInterval& interval) noexcept;
    void begin_target(std::uint64_t target_id) noexcept;
    AimResponseEstimate estimate(float slow_zone_weight = 0.0f) const noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t kExcitationHistoryCapacity = 24;

    struct RegionState {
        std::array<AimResponseInterval, kExcitationHistoryCapacity> history{};
        float learned_scale = 500.0f;
        float confidence = 0.0f;
        std::uint32_t accepted_samples = 0;
        std::size_t history_count = 0;
        std::size_t history_next = 0;
    };

    bool eligible(const AimResponseInterval& interval) const noexcept;
    bool update_region(
        RegionState& region,
        const AimResponseInterval& interval,
        bool slowdown_region) noexcept;
    void clear_histories() noexcept;

    AimResponseEstimatorConfig config_{};
    RegionState free_region_{};
    RegionState slow_region_{};
    std::uint64_t target_id_ = 0;
    bool previous_region_was_slow_ = false;
    bool has_previous_region_ = false;
};

}  // namespace controller_native
