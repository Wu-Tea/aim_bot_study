#pragma once

#include <cstdint>

namespace vision_native {

struct PresentSteadyInterval {
    bool valid = false;
    std::uint64_t previous_present_ns = 0;
    std::uint64_t current_present_ns = 0;
    std::uint64_t previous_calibration_id = 0;
    std::uint64_t current_calibration_id = 0;
    std::uint64_t previous_uncertainty_ns = 0;
    std::uint64_t current_uncertainty_ns = 0;
};

enum class PresentOutputJoinReason : std::uint8_t {
    Eligible,
    InvalidClock,
    InvalidInterval,
    MissingOutputTimestamp,
    OutputAfterCurrentPresent,
    OutputTooOldForInterval,
};

struct PresentOutputJoinDecision {
    bool eligible = false;
    PresentOutputJoinReason reason = PresentOutputJoinReason::InvalidClock;
};

// Timestamp-only shadow contract. It deliberately consumes no result-ready
// timestamp: current_result_ns is a processing-stage diagnostic, never an
// effect/present boundary.
PresentOutputJoinDecision evaluate_present_output_join(
    const PresentSteadyInterval& interval,
    std::uint64_t output_applied_ns,
    std::uint64_t minimum_response_delay_ns,
    std::uint64_t maximum_response_delay_ns) noexcept;

}  // namespace vision_native
