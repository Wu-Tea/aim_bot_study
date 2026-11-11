#pragma once

#include "pipeline_contract/target_plan.h"

#include <cstdint>
#include <limits>
#include <array>
#include <cstddef>

namespace controller_native {

struct AimResponseInterval {
    // Normalized camera-response command before inverse curve mapping. This
    // keeps learned px/(command*s) units identical to ResponseModelAimSolver.
    pipeline_contract::Vec2f average_final_stick{};
    pipeline_contract::Vec2f observed_error_rate_px_per_sec{};
    std::uint64_t target_id = 0;
    float dt_seconds = 0.0f;
    float reliability = 0.0f;
    float target_acceleration_px_per_sec2 = 0.0f;
    bool observed = false;
    bool manual_ambiguous = false;
};

struct AimResponseEstimate {
    float scale_px_per_stick_second = 500.0f;
    float confidence = 0.0f;
    std::uint32_t accepted_samples = 0;
    float learned_scale = 500.0f;
};

struct AimResponseEstimatorConfig {
    float fallback_scale = 500.0f;
    float minimum_scale = 80.0f;
    // No game-specific ceiling. Eligibility and relative evidence checks own
    // trust; the numeric representation is the only default upper boundary.
    float maximum_scale = std::numeric_limits<float>::max();
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
    AimResponseEstimate estimate() const noexcept;
    void reset() noexcept;

private:
    struct RegionState {
        struct Evidence {
            float excitation = 0;
            float correlation = 0;
            float rate_energy = 0;
        };
        std::array<Evidence, 8> evidence{};
        std::size_t evidence_count = 0;
        std::size_t evidence_next = 0;
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
    std::uint64_t target_id_ = 0;
};

}  // namespace controller_native
