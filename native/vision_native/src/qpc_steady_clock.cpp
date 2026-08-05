#include "vision_native/qpc_steady_clock.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <limits>

namespace vision_native {
namespace {

std::uint64_t steady_now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool checked_signed_delta(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::int64_t* out) noexcept {
    if (out == nullptr) return false;
    constexpr std::uint64_t kMaxSigned =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (lhs >= rhs) {
        const std::uint64_t delta = lhs - rhs;
        if (delta > kMaxSigned) return false;
        *out = static_cast<std::int64_t>(delta);
        return true;
    }
    const std::uint64_t delta = rhs - lhs;
    if (delta > kMaxSigned) return false;
    *out = -static_cast<std::int64_t>(delta);
    return true;
}

bool checked_add(
    std::int64_t base,
    std::int64_t delta,
    std::int64_t* out) noexcept {
    if (out == nullptr) return false;
    if (delta > 0 && base > std::numeric_limits<std::int64_t>::max() - delta)
        return false;
    if (delta < 0 && base < std::numeric_limits<std::int64_t>::min() - delta)
        return false;
    *out = base + delta;
    return true;
}

bool qpc_delta_to_ns(
    std::int64_t qpc_delta,
    std::uint64_t frequency,
    std::int64_t* ns_delta) noexcept {
    if (ns_delta == nullptr || frequency == 0) return false;
    const long double scaled =
        (static_cast<long double>(qpc_delta) * 1'000'000'000.0L) /
        static_cast<long double>(frequency);
    if (!std::isfinite(scaled) ||
        scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
        scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min())) {
        return false;
    }
    *ns_delta = static_cast<std::int64_t>(scaled);
    return true;
}

}  // namespace

QpcSteadyClockCalibration QpcSteadyClockCalibration::from_sample(
    std::uint64_t qpc_at_sample,
    std::uint64_t qpc_frequency,
    std::int64_t steady_ns_at_sample,
    std::uint64_t uncertainty_ns,
    std::uint64_t calibration_id) noexcept {
    QpcSteadyClockCalibration value;
    value.qpc_at_sample = qpc_at_sample;
    value.qpc_frequency = qpc_frequency;
    value.steady_ns_at_sample = steady_ns_at_sample;
    value.uncertainty_ns = uncertainty_ns;
    value.calibration_id = calibration_id;
    value.valid = qpc_at_sample != 0 && qpc_frequency != 0 &&
        steady_ns_at_sample >= 0;
    return value;
}

QpcSteadyClockCalibration QpcSteadyClockCalibration::capture(
    std::uint64_t qpc_frequency,
    std::uint64_t calibration_id) noexcept {
    LARGE_INTEGER before{};
    LARGE_INTEGER after{};
    if (qpc_frequency == 0 || !QueryPerformanceCounter(&before)) return {};
    const std::uint64_t steady_ns = steady_now_ns();
    if (!QueryPerformanceCounter(&after) || before.QuadPart <= 0 ||
        after.QuadPart < before.QuadPart) {
        return {};
    }
    const std::uint64_t before_qpc = static_cast<std::uint64_t>(before.QuadPart);
    const std::uint64_t after_qpc = static_cast<std::uint64_t>(after.QuadPart);
    const std::uint64_t span = after_qpc - before_qpc;
    const std::uint64_t midpoint = before_qpc + (span / 2);
    const long double uncertainty =
        (static_cast<long double>(span) * 1'000'000'000.0L) /
        static_cast<long double>(qpc_frequency);
    if (!std::isfinite(uncertainty) || uncertainty < 0.0L ||
        uncertainty > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return {};
    }
    return from_sample(
        midpoint,
        qpc_frequency,
        static_cast<std::int64_t>(steady_ns),
        static_cast<std::uint64_t>(std::ceil(uncertainty / 2.0L)),
        calibration_id);
}

bool QpcSteadyClockCalibration::map_qpc_to_steady(
    std::uint64_t qpc,
    std::uint64_t source_qpc_frequency,
    std::uint64_t* steady_ns,
    std::uint64_t* uncertainty_ns_out) const noexcept {
    if (steady_ns == nullptr || !valid || qpc == 0 ||
        source_qpc_frequency == 0 || source_qpc_frequency != qpc_frequency) {
        return false;
    }
    std::int64_t qpc_delta = 0;
    if (!checked_signed_delta(qpc, qpc_at_sample, &qpc_delta)) return false;
    std::int64_t delta_ns = 0;
    if (!qpc_delta_to_ns(qpc_delta, qpc_frequency, &delta_ns)) return false;
    std::int64_t mapped = 0;
    if (!checked_add(steady_ns_at_sample, delta_ns, &mapped) || mapped < 0) {
        return false;
    }
    *steady_ns = static_cast<std::uint64_t>(mapped);
    if (uncertainty_ns_out != nullptr) *uncertainty_ns_out = uncertainty_ns;
    return true;
}

}  // namespace vision_native
