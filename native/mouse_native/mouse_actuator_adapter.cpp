#include "mouse_native/mouse_actuator_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mouse_native {
namespace {

bool valid_normalized(float value) noexcept {
    constexpr float kTolerance = 1.0e-5f;
    return std::isfinite(value) && value >= -1.0f - kTolerance &&
        value <= 1.0f + kTolerance;
}

}  // namespace

MouseActuatorAdapter::MouseActuatorAdapter(MouseActuatorAdapterConfig config)
    : config_(config) {
    if (config_.maximum_abs_counts_per_report <= 0) {
        config_.maximum_abs_counts_per_report = 32'767;
    }
    if (!std::isfinite(config_.minimum_dt_seconds) ||
        config_.minimum_dt_seconds <= 0.0f) {
        config_.minimum_dt_seconds = 0.0001f;
    }
    if (!std::isfinite(config_.maximum_dt_seconds) ||
        config_.maximum_dt_seconds < config_.minimum_dt_seconds) {
        config_.maximum_dt_seconds = 0.0500f;
    }
}

MouseActuationResult MouseActuatorAdapter::adapt(
    float final_u_x,
    float final_u_y,
    float dt_seconds,
    const MouseResponseProfile& profile) noexcept {
    MouseActuationResult result{};
    if (!mouse_native::valid(profile) || !valid_normalized(final_u_x) ||
        !valid_normalized(final_u_y) || !std::isfinite(dt_seconds) ||
        dt_seconds < config_.minimum_dt_seconds ||
        dt_seconds > config_.maximum_dt_seconds) {
        return result;
    }

    if (profile_generation_ != profile.generation) {
        reset();
        profile_generation_ = profile.generation;
    }

    const double desired_x =
        static_cast<double>(final_u_x) *
            static_cast<double>(profile.counts_per_u_second_x) *
            static_cast<double>(dt_seconds) +
        residual_x_;
    const double desired_y =
        -static_cast<double>(final_u_y) *
            static_cast<double>(profile.counts_per_u_second_y) *
            static_cast<double>(dt_seconds) +
        residual_y_;
    if (!std::isfinite(desired_x) || !std::isfinite(desired_y) ||
        desired_x < static_cast<double>(std::numeric_limits<long long>::min()) ||
        desired_x > static_cast<double>(std::numeric_limits<long long>::max()) ||
        desired_y < static_cast<double>(std::numeric_limits<long long>::min()) ||
        desired_y > static_cast<double>(std::numeric_limits<long long>::max())) {
        reset();
        return result;
    }

    const long long rounded_x = std::llround(desired_x);
    const long long rounded_y = std::llround(desired_y);
    const long long limit = config_.maximum_abs_counts_per_report;
    const long long bounded_x = std::clamp(rounded_x, -limit, limit);
    const long long bounded_y = std::clamp(rounded_y, -limit, limit);
    result.saturated = bounded_x != rounded_x || bounded_y != rounded_y;
    result.dx = static_cast<std::int32_t>(bounded_x);
    result.dy = static_cast<std::int32_t>(bounded_y);
    result.valid = true;

    if (result.saturated) {
        // Do not retain an unbounded output tail after a descriptor/report
        // saturation event. The transport owns any future report splitting.
        residual_x_ = 0.0;
        residual_y_ = 0.0;
    } else {
        residual_x_ = desired_x - static_cast<double>(rounded_x);
        residual_y_ = desired_y - static_cast<double>(rounded_y);
    }
    return result;
}

MouseActuationResult MouseActuatorAdapter::adapt_with_native_axes(
    float final_u_x, float final_u_y, float dt_seconds,
    const MouseResponseProfile& profile, MouseSourceCounts source,
    bool native_x, bool native_y) noexcept {
    // An axis returned unchanged by the command owner must preserve integer
    // source counts, not inherit the preceding AI output's rounding tail.
    if (native_x) residual_x_ = 0;
    if (native_y) residual_y_ = 0;
    auto result = adapt(final_u_x, final_u_y, dt_seconds, profile);
    if (result.valid && !result.saturated) {
        if (native_x) { result.dx = source.dx; residual_x_ = 0; }
        if (native_y) { result.dy = source.dy; residual_y_ = 0; }
        result.transparent = native_x && native_y;
    }
    return result;
}

MouseActuationResult MouseActuatorAdapter::passthrough(
    MouseSourceCounts source) noexcept {
    reset();
    MouseActuationResult result{};
    result.dx = source.dx;
    result.dy = source.dy;
    result.valid = true;
    result.transparent = true;
    return result;
}

void MouseActuatorAdapter::reset() noexcept {
    residual_x_ = 0.0;
    residual_y_ = 0.0;
    profile_generation_ = 0;
}

double MouseActuatorAdapter::residual_x() const noexcept {
    return residual_x_;
}

double MouseActuatorAdapter::residual_y() const noexcept {
    return residual_y_;
}

}  // namespace mouse_native
