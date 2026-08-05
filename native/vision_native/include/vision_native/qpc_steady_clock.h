#pragma once

#include <cstdint>

namespace vision_native {

inline constexpr const char* kQpcSteadyClockDomain =
    "qpc_to_steady_calibrated";

// A bounded, auditable mapping between the raw DXGI QPC domain and the
// process steady-clock domain. The sample is bracketed in the implementation
// so uncertainty covers the clock-read interval.
struct QpcSteadyClockCalibration {
    bool valid = false;
    std::uint64_t qpc_at_sample = 0;
    std::uint64_t qpc_frequency = 0;
    std::int64_t steady_ns_at_sample = 0;
    std::uint64_t uncertainty_ns = 0;
    std::uint64_t calibration_id = 0;

    static QpcSteadyClockCalibration capture(
        std::uint64_t qpc_frequency,
        std::uint64_t calibration_id) noexcept;

    static QpcSteadyClockCalibration from_sample(
        std::uint64_t qpc_at_sample,
        std::uint64_t qpc_frequency,
        std::int64_t steady_ns_at_sample,
        std::uint64_t uncertainty_ns,
        std::uint64_t calibration_id) noexcept;

    bool map_qpc_to_steady(
        std::uint64_t qpc,
        std::uint64_t source_qpc_frequency,
        std::uint64_t* steady_ns,
        std::uint64_t* uncertainty_ns_out = nullptr) const noexcept;
};

}  // namespace vision_native
