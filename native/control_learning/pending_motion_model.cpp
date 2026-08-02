#include "control_learning/pending_motion_model.h"

#include <algorithm>
#include <cmath>

namespace control_learning {
namespace {

bool finite_matrix(const ResponseMatrix2d& matrix) noexcept {
    for (const auto& row : matrix.values)
        for (double value : row) if (!std::isfinite(value)) return false;
    return true;
}

Vec2d multiply(const ResponseMatrix2d& matrix,
               pipeline_contract::Vec2f value) noexcept {
    return {
        matrix.values[0][0] * value.x + matrix.values[0][1] * value.y,
        matrix.values[1][0] * value.x + matrix.values[1][1] * value.y};
}

Vec2d add(Vec2d a, Vec2d b) noexcept {
    return {a.x + b.x, a.y + b.y};
}

Vec2d motion(const ControlIntegral& integral,
             const ResponseMatrix2d& right,
             const ResponseMatrix2d& left) noexcept {
    return add(multiply(right, integral.final_right_stick_seconds),
               multiply(left, integral.final_left_stick_seconds));
}

ControlIntegral zero_interval() noexcept {
    ControlIntegral result;
    result.complete = true;
    return result;
}

ControlIntegral integrate_interval(
    const ControlHistory<1024>& history,
    std::uint64_t begin_ns,
    std::uint64_t end_ns) noexcept {
    // A decision made exactly at capture time has no time-area.  Treat that
    // explicit zero-duration interval as complete instead of fabricating a
    // held sample or invalidating an otherwise covered estimate.
    return begin_ns == end_ns
        ? zero_interval()
        : history.integrate(begin_ns, end_ns);
}

bool clean(const ControlIntegral& integral) noexcept {
    return integral.complete && !integral.failed_delivery &&
        !integral.output_disabled && !integral.firing &&
        !integral.recoil_active && !integral.saturated;
}

bool finite(Vec2d value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

}  // namespace

PendingMotionEstimate PendingMotionModel::estimate(
    const PendingMotionRequest& request,
    const ControlHistory<1024>& history) noexcept {
    PendingMotionEstimate result;
    if (request.previous_capture_ns == 0 ||
        request.current_capture_ns <= request.previous_capture_ns ||
        request.decision_ns < request.current_capture_ns ||
        !std::isfinite(request.delay_ms) || request.delay_ms <= 0.0f ||
        !std::isfinite(request.selected_delay_confidence) ||
        !std::isfinite(request.response_confidence) ||
        !finite_matrix(request.right_response) ||
        !finite_matrix(request.left_response) ||
        !request.stable_coordinates_valid || !request.identity_continuous ||
        !request.ads_epoch_continuous) {
        return result;
    }
    const auto delay_ns = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(request.delay_ms) * 1'000'000.0));
    if (request.previous_capture_ns <= delay_ns ||
        request.current_capture_ns <= delay_ns) return result;

    const ControlIntegral realized = integrate_interval(
        history,
        request.previous_capture_ns - delay_ns,
        request.current_capture_ns - delay_ns);
    const ControlIntegral in_flight = integrate_interval(
        history,
        request.current_capture_ns - delay_ns,
        request.current_capture_ns);
    const ControlIntegral scheduled = integrate_interval(
        history, request.current_capture_ns, request.decision_ns);
    result.history_complete = clean(realized) && clean(in_flight) &&
        clean(scheduled);
    if (!result.history_complete) return result;

    result.realized_px = motion(
        realized, request.right_response, request.left_response);
    result.in_flight_px = motion(
        in_flight, request.right_response, request.left_response);
    result.scheduled_px = motion(
        scheduled, request.right_response, request.left_response);
    // Realized work explains the change between adjacent observations.  Only
    // work not yet visible at the current capture may enter the shadow
    // rollout, so pending_total deliberately excludes realized_px.
    result.pending_total_px = add(result.in_flight_px, result.scheduled_px);
    if (!finite(result.realized_px) || !finite(result.in_flight_px) ||
        !finite(result.scheduled_px) || !finite(result.pending_total_px))
        return PendingMotionEstimate{};
    result.confidence = std::clamp(std::min(
        request.selected_delay_confidence, request.response_confidence),
        0.0f, 1.0f);
    result.valid = true;
    return result;
}

}  // namespace control_learning
