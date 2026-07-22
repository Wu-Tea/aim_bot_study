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

BlindWindowMetrics evaluate_blind_window(
    const BlindFixture& fixture,
    const BlindWindowTrace& trace,
    const BlindSchedule& schedule) noexcept {
    BlindWindowMetrics metrics;
    metrics.blind_duration_ms = std::max(
        0, schedule.next_result_at_us - schedule.event_at_us) / 1'000.0;
    if (trace.frames.empty()) return metrics;

    const BlindTraceFrame* reveal = nullptr;
    const BlindTraceFrame* pre_reveal = nullptr;
    Vec2d previous_output{};
    std::vector<double> output_deltas;
    int stable_reacquire_ms = 0;
    int reacquire_start_ms = -1;

    for (const BlindTraceFrame& frame : trace.frames) {
        if (frame.now_us < schedule.next_result_at_us) pre_reveal = &frame;
        if (reveal == nullptr && frame.now_us >= schedule.next_result_at_us) {
            reveal = &frame;
        }
        const double output_delta = length({
            frame.final_stick.x - previous_output.x,
            frame.final_stick.y - previous_output.y});
        metrics.output_total_variation += output_delta;
        output_deltas.push_back(output_delta);
        previous_output = frame.final_stick;

        if (frame.now_us >= schedule.next_result_at_us) {
            if (length(frame.true_error_px) <= 8.0) {
                if (stable_reacquire_ms == 0) {
                    reacquire_start_ms = frame.now_us / 1'000;
                }
                ++stable_reacquire_ms;
                if (stable_reacquire_ms >= 30 &&
                    metrics.reveal_to_reacquire_ms < 0) {
                    metrics.reveal_to_reacquire_ms =
                        reacquire_start_ms - schedule.next_result_at_us / 1'000;
                }
            } else {
                stable_reacquire_ms = 0;
                reacquire_start_ms = -1;
            }
        }
    }

    if (reveal != nullptr) {
        metrics.harmful_pending_at_reveal_px =
            harmful_pending_at_reveal(
                reveal->true_error_px,
                reveal->scheduled_pending_reticle_motion_px);
    }
    metrics.future_burden_40_px_ms = future_error_burden(
        trace, schedule.next_result_at_us / 1'000, 40);
    metrics.future_burden_80_px_ms = future_error_burden(
        trace, schedule.next_result_at_us / 1'000, 80);
    metrics.future_burden_160_px_ms = future_error_burden(
        trace, schedule.next_result_at_us / 1'000, 160);
    metrics.user_fight_stick_ms = user_fight_area(trace, 0.03);

    const Vec2d stale_direction = pre_reveal == nullptr
        ? Vec2d{} : normalized(pre_reveal->ai_stick);
    const int reverse_end_us = schedule.next_result_at_us + 80'000;
    for (const BlindTraceFrame& frame : trace.frames) {
        if (frame.now_us >= schedule.event_at_us &&
            frame.now_us < schedule.next_result_at_us) {
            metrics.stale_ai_impulse_stick_ms += std::max(
                0.0, dot(frame.ai_stick, stale_direction));
            const Vec2d ai_reticle_velocity = {
                fixture.right_response_px_per_stick_second[0] * frame.ai_stick.x +
                    fixture.right_response_px_per_stick_second[1] * frame.ai_stick.y,
                fixture.right_response_px_per_stick_second[2] * frame.ai_stick.x +
                    fixture.right_response_px_per_stick_second[3] * frame.ai_stick.y,
            };
            const Vec2d error_axis = normalized(frame.true_error_px);
            metrics.harmful_ai_motion_px += std::max(
                0.0, -dot(error_axis, ai_reticle_velocity)) * 0.001;
        }
        if (frame.now_us >= schedule.next_result_at_us &&
            frame.now_us < reverse_end_us) {
            metrics.reverse_correction_80_stick_ms += std::max(
                0.0, -dot(frame.final_stick, stale_direction));
        }
        if (frame.max_controller_source_time_us > frame.now_us) {
            ++metrics.future_dependency_violations;
        }
    }

    if (!output_deltas.empty()) {
        std::sort(output_deltas.begin(), output_deltas.end());
        const std::size_t index = static_cast<std::size_t>(
            std::floor(0.95 * static_cast<double>(output_deltas.size() - 1)));
        metrics.p95_output_delta = output_deltas[index];
    }
    return metrics;
}

}  // namespace controller_native::blind_window
