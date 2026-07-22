#include "blind_window_metrics.h"

#include <algorithm>
#include <cmath>

namespace controller_native::blind_window {
namespace {

double dot(Vec2d left, Vec2d right) noexcept {
    return left.x * right.x + left.y * right.y;
}

double length(Vec2d value) noexcept {
    return std::hypot(value.x, value.y);
}

Vec2d normalized(Vec2d value) noexcept {
    const double magnitude = length(value);
    if (!(magnitude > 0.0) || !std::isfinite(magnitude)) return {};
    return {value.x / magnitude, value.y / magnitude};
}

bool finite_value(double value) noexcept {
    return std::isfinite(value);
}

}  // namespace

bool finite(const BlindWindowMetrics& metrics) noexcept {
    return finite_value(metrics.blind_duration_ms) &&
        finite_value(metrics.stale_ai_impulse_stick_ms) &&
        finite_value(metrics.harmful_ai_motion_px) &&
        finite_value(metrics.harmful_pending_at_reveal_px) &&
        finite_value(metrics.future_burden_40_px_ms) &&
        finite_value(metrics.future_burden_80_px_ms) &&
        finite_value(metrics.future_burden_160_px_ms) &&
        finite_value(metrics.reverse_correction_80_stick_ms) &&
        finite_value(metrics.post_cross_area_px_ms) &&
        finite_value(metrics.user_fight_stick_ms) &&
        finite_value(metrics.far_error_closing_speed_px_per_sec) &&
        finite_value(metrics.output_total_variation) &&
        finite_value(metrics.p95_output_delta);
}

double harmful_pending_at_reveal(
    Vec2d error_px,
    Vec2d pending_reticle_motion_px) noexcept {
    const double error_distance = length(error_px);
    if (!(error_distance > 0.0) || !std::isfinite(error_distance)) {
        return length(pending_reticle_motion_px);
    }
    const double closing = dot(
        normalized(error_px), pending_reticle_motion_px);
    return std::max(0.0, -closing) +
        std::max(0.0, closing - error_distance);
}

double future_error_burden(
    const BlindWindowTrace& trace,
    int start_ms,
    int horizon_ms) noexcept {
    if (horizon_ms <= 0) return 0.0;
    const int start_us = start_ms * 1'000;
    const int end_us = start_us + horizon_ms * 1'000;
    double burden_px_ms = 0.0;
    for (const BlindTraceFrame& frame : trace.frames) {
        if (frame.now_us < start_us || frame.now_us >= end_us) continue;
        burden_px_ms += length(frame.true_error_px);
    }
    return burden_px_ms;
}

double user_fight_area(
    const BlindWindowTrace& trace,
    double drift_floor) noexcept {
    double area_stick_ms = 0.0;
    for (const BlindTraceFrame& frame : trace.frames) {
        const double manual = length(frame.manual_stick);
        const double ai = length(frame.ai_stick);
        if (manual < drift_floor || !(ai > 0.0)) continue;
        const double cosine = std::clamp(
            dot(frame.manual_stick, frame.ai_stick) / (manual * ai),
            -1.0, 1.0);
        area_stick_ms += std::min(manual, ai) * std::max(0.0, -cosine);
    }
    return area_stick_ms;
}

}  // namespace controller_native::blind_window
