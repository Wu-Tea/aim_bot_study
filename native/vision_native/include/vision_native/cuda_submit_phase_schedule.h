#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace vision_native {

// Pure scheduling result for one CUDA submit target.  Phase zero remains the
// natural ASAP baseline.  Non-zero adaptive phases may wrap into the next
// source-present cycle when the target in the current cycle has already
// passed; fixed mode keeps its historical current-cycle-only behaviour.
struct CudaSubmitPhaseSchedule {
    std::uint64_t deadline_ns = 0;
    std::uint64_t wait_ns = 0;
    bool should_wait = false;
    bool cycle_wrapped = false;
};

// Equivalent schedule in an arbitrary monotonic tick domain.  The production
// path uses raw QPC ticks so DXGI LastPresentTime, the scheduling deadline and
// the observed submit boundary never cross a per-frame clock calibration.
struct CudaSubmitPhaseTickSchedule {
    std::uint64_t deadline_tick = 0;
    std::uint64_t wait_ticks = 0;
    std::uint64_t wait_ns = 0;
    bool should_wait = false;
    bool cycle_wrapped = false;
};

inline bool rounded_ticks_for_microseconds(
    std::uint64_t duration_us,
    std::uint64_t tick_frequency,
    std::uint64_t* ticks) noexcept {
    if (ticks == nullptr || tick_frequency == 0) return false;
    const long double scaled =
        static_cast<long double>(duration_us) *
        static_cast<long double>(tick_frequency) / 1'000'000.0L;
    if (!std::isfinite(scaled) || scaled < 0.0L ||
        scaled > static_cast<long double>(
            std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    *ticks = static_cast<std::uint64_t>(std::llround(scaled));
    return duration_us == 0 || *ticks != 0;
}

inline bool nanoseconds_for_ticks(
    std::uint64_t ticks,
    std::uint64_t tick_frequency,
    std::uint64_t* duration_ns) noexcept {
    if (duration_ns == nullptr || tick_frequency == 0) return false;
    const long double scaled =
        static_cast<long double>(ticks) * 1'000'000'000.0L /
        static_cast<long double>(tick_frequency);
    if (!std::isfinite(scaled) || scaled < 0.0L ||
        scaled > static_cast<long double>(
            std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    *duration_ns = static_cast<std::uint64_t>(std::ceil(scaled));
    return true;
}

inline CudaSubmitPhaseTickSchedule schedule_cuda_submit_phase_ticks(
    std::uint64_t source_present_tick,
    std::uint64_t now_tick,
    std::uint64_t tick_frequency,
    std::uint32_t target_phase_us,
    std::uint32_t source_period_us,
    bool allow_cycle_wrap,
    std::uint64_t maximum_wait_ns) noexcept {
    CudaSubmitPhaseTickSchedule result{};
    if (source_present_tick == 0 || now_tick == 0 ||
        tick_frequency == 0 || target_phase_us == 0) {
        return result;
    }

    std::uint64_t phase_ticks = 0;
    if (!rounded_ticks_for_microseconds(
            target_phase_us, tick_frequency, &phase_ticks) ||
        source_present_tick >
            std::numeric_limits<std::uint64_t>::max() - phase_ticks) {
        return result;
    }
    std::uint64_t deadline_tick = source_present_tick + phase_ticks;

    if (deadline_tick <= now_tick && allow_cycle_wrap &&
        source_period_us > 0) {
        std::uint64_t period_ticks = 0;
        if (!rounded_ticks_for_microseconds(
                source_period_us, tick_frequency, &period_ticks) ||
            period_ticks == 0) {
            return result;
        }
        const std::uint64_t elapsed_ticks = now_tick - deadline_tick;
        const std::uint64_t cycles = elapsed_ticks / period_ticks + 1ull;
        if (cycles >
            (std::numeric_limits<std::uint64_t>::max() - deadline_tick) /
                period_ticks) {
            return result;
        }
        deadline_tick += cycles * period_ticks;
        result.cycle_wrapped = true;
    }

    result.deadline_tick = deadline_tick;
    if (deadline_tick <= now_tick) return result;
    result.wait_ticks = deadline_tick - now_tick;
    if (!nanoseconds_for_ticks(
            result.wait_ticks, tick_frequency, &result.wait_ns)) {
        return result;
    }
    result.should_wait = result.wait_ns <= maximum_wait_ns;
    return result;
}

inline CudaSubmitPhaseSchedule schedule_cuda_submit_phase(
    std::uint64_t source_present_ns,
    std::uint64_t now_ns,
    std::uint32_t target_phase_us,
    std::uint32_t source_period_us,
    bool allow_cycle_wrap,
    std::uint64_t maximum_wait_ns) noexcept {
    CudaSubmitPhaseSchedule result{};
    if (source_present_ns == 0 || now_ns == 0 || target_phase_us == 0) {
        return result;
    }

    constexpr std::uint64_t kNsPerUs = 1000ull;
    const std::uint64_t phase_ns =
        static_cast<std::uint64_t>(target_phase_us) * kNsPerUs;
    if (source_present_ns >
        std::numeric_limits<std::uint64_t>::max() - phase_ns) {
        return result;
    }
    std::uint64_t deadline_ns = source_present_ns + phase_ns;

    if (deadline_ns <= now_ns && allow_cycle_wrap && source_period_us > 0) {
        const std::uint64_t period_ns =
            static_cast<std::uint64_t>(source_period_us) * kNsPerUs;
        const std::uint64_t elapsed_ns = now_ns - deadline_ns;
        const std::uint64_t cycles = elapsed_ns / period_ns + 1ull;
        if (cycles >
            (std::numeric_limits<std::uint64_t>::max() - deadline_ns) /
                period_ns) {
            return result;
        }
        deadline_ns += cycles * period_ns;
        result.cycle_wrapped = true;
    }

    result.deadline_ns = deadline_ns;
    if (deadline_ns <= now_ns) return result;
    result.wait_ns = deadline_ns - now_ns;
    result.should_wait = result.wait_ns <= maximum_wait_ns;
    return result;
}

}  // namespace vision_native
