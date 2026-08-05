#include "vision_native/present_interval_contract.h"

#include <limits>

namespace vision_native {
namespace {

bool checked_add(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t* out) noexcept {
    if (out == nullptr || lhs > std::numeric_limits<std::uint64_t>::max() - rhs)
        return false;
    *out = lhs + rhs;
    return true;
}

}  // namespace

PresentOutputJoinDecision evaluate_present_output_join(
    const PresentSteadyInterval& interval,
    std::uint64_t output_applied_ns,
    std::uint64_t minimum_response_delay_ns,
    std::uint64_t maximum_response_delay_ns) noexcept {
    PresentOutputJoinDecision decision;
    if (!interval.valid || interval.previous_present_ns == 0 ||
        interval.current_present_ns <= interval.previous_present_ns) {
        decision.reason = interval.valid
            ? PresentOutputJoinReason::InvalidInterval
            : PresentOutputJoinReason::InvalidClock;
        return decision;
    }
    // A steady interval is only reconstructible when both endpoints retain
    // their own calibration provenance.  Never admit a join from a partially
    // identified interval, even if the numeric timestamps look monotonic.
    if (interval.previous_calibration_id == 0 ||
        interval.current_calibration_id == 0) {
        decision.reason = PresentOutputJoinReason::InvalidClock;
        return decision;
    }
    if (output_applied_ns == 0) {
        decision.reason = PresentOutputJoinReason::MissingOutputTimestamp;
        return decision;
    }
    if (maximum_response_delay_ns < minimum_response_delay_ns) {
        decision.reason = PresentOutputJoinReason::InvalidInterval;
        return decision;
    }
    std::uint64_t earliest_effect_ns = 0;
    std::uint64_t latest_effect_ns = 0;
    if (!checked_add(output_applied_ns, minimum_response_delay_ns,
                     &earliest_effect_ns) ||
        !checked_add(output_applied_ns, maximum_response_delay_ns,
                     &latest_effect_ns)) {
        decision.reason = PresentOutputJoinReason::InvalidInterval;
        return decision;
    }
    if (earliest_effect_ns > interval.current_present_ns) {
        decision.reason = PresentOutputJoinReason::OutputAfterCurrentPresent;
        return decision;
    }
    if (latest_effect_ns < interval.previous_present_ns) {
        decision.reason = PresentOutputJoinReason::OutputTooOldForInterval;
        return decision;
    }
    decision.eligible = true;
    decision.reason = PresentOutputJoinReason::Eligible;
    return decision;
}

}  // namespace vision_native
