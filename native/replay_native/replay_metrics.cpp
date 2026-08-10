#include "replay_metrics.h"

#include <algorithm>
#include <cmath>

namespace replay_native {

namespace {

double percentile_nearest_rank(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double clamped = std::max(0.0, std::min(100.0, percentile));
    const double rank = std::ceil((clamped / 100.0) * static_cast<double>(values.size()));
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1;
    return values[std::min(index, values.size() - 1)];
}

struct AxisContinuityState {
    bool has_previous = false;
    bool has_previous_delta = false;
    double previous_manual = 0.0;
    double previous_assist = 0.0;
    double previous_final = 0.0;
    double previous_final_delta = 0.0;
    double opposing_run_ms = 0.0;
    double plateau_run_ms = 0.0;
};

void observe_axis_continuity(
    double manual,
    double assist,
    double final_output,
    double tick_ms,
    const ReplayMetricOptions& options,
    AxisContinuityState& state,
    ReplayMetricSummary& summary,
    std::vector<double>& assist_deltas,
    std::vector<double>& final_jerks) {
    const bool strong_manual = std::fabs(manual) >= options.strong_manual_threshold;
    if (strong_manual) {
        ++summary.strong_manual_axis_samples;
    }

    const bool opposing_assist = strong_manual && (manual * assist) < 0.0;
    if (opposing_assist) {
        ++summary.opposing_assist_axis_samples;
        state.opposing_run_ms += tick_ms;
        summary.longest_opposing_assist_run_ms = std::max(
            summary.longest_opposing_assist_run_ms,
            state.opposing_run_ms);
    } else {
        state.opposing_run_ms = 0.0;
    }

    const double gain = strong_manual ? std::fabs(final_output / manual) : 0.0;
    const bool same_direction = (manual * final_output) > 0.0;
    const bool gain_plateau =
        strong_manual &&
        same_direction &&
        std::fabs(gain - options.manual_gain_plateau_ratio) <=
            options.manual_gain_plateau_tolerance;
    if (gain_plateau) {
        ++summary.manual_gain_plateau_axis_samples;
        state.plateau_run_ms += tick_ms;
        summary.longest_manual_gain_plateau_run_ms = std::max(
            summary.longest_manual_gain_plateau_run_ms,
            state.plateau_run_ms);
    } else {
        state.plateau_run_ms = 0.0;
    }

    const bool stable_manual =
        strong_manual &&
        state.has_previous &&
        std::fabs(state.previous_manual) >= options.strong_manual_threshold &&
        (manual * state.previous_manual) > 0.0 &&
        std::fabs(manual - state.previous_manual) <= options.stable_manual_delta_threshold;
    if (stable_manual) {
        const double assist_delta = std::fabs(assist - state.previous_assist);
        assist_deltas.push_back(assist_delta);
        if (assist_delta >= options.assist_step_threshold) {
            ++summary.assist_step_events;
        }

        const double final_delta = final_output - state.previous_final;
        if (state.has_previous_delta) {
            const double final_jerk = std::fabs(final_delta - state.previous_final_delta);
            final_jerks.push_back(final_jerk);
            if (final_jerk >= options.final_jerk_threshold) {
                ++summary.final_jerk_events;
            }
        }
        state.previous_final_delta = final_delta;
        state.has_previous_delta = true;
    } else {
        state.has_previous_delta = false;
        state.previous_final_delta = 0.0;
    }

    state.has_previous = true;
    state.previous_manual = manual;
    state.previous_assist = assist;
    state.previous_final = final_output;
}

}  // namespace

ReplayMetricSummary summarize_replay_metrics(
    const std::vector<NativeReplayFrame>& frames,
    ReplayMetricOptions options) {
    std::vector<double> target_errors;
    std::vector<double> assist_deltas;
    std::vector<double> final_jerks;
    AxisContinuityState x_continuity;
    AxisContinuityState y_continuity;
    double follow_lag_sum = 0.0;
    std::uint64_t follow_lag_count = 0;
    ReplayMetricSummary summary;

    for (const NativeReplayFrame& frame : frames) {
        if (frame.selected_target.has_target) {
            const double target_error = std::hypot(
                static_cast<double>(frame.selected_target.aim_error_px.x),
                static_cast<double>(frame.selected_target.aim_error_px.y));
            target_errors.push_back(target_error);
            follow_lag_sum += target_error;
            ++follow_lag_count;
        }
        const double tick_ms = frame.timing.controller_tick_ms > 0.0
            ? frame.timing.controller_tick_ms
            : 1.0;
        observe_axis_continuity(
            frame.controller.sticks.manual.x,
            frame.controller.sticks.assist.x,
            frame.controller.sticks.final_output.x,
            tick_ms,
            options,
            x_continuity,
            summary,
            assist_deltas,
            final_jerks);
        observe_axis_continuity(
            frame.controller.sticks.manual.y,
            frame.controller.sticks.assist.y,
            frame.controller.sticks.final_output.y,
            tick_ms,
            options,
            y_continuity,
            summary,
            assist_deltas,
            final_jerks);

        if (frame.controller.fire_allowed &&
            frame.selected_target.age_ms > options.max_fire_source_age_ms) {
            ++summary.stale_fire_violations;
        }
    }

    summary.target_error_p50_px = percentile_nearest_rank(target_errors, 50.0);
    summary.target_error_p95_px = percentile_nearest_rank(target_errors, 95.0);
    summary.target_error_p99_px = percentile_nearest_rank(target_errors, 99.0);
    summary.follow_lag_mean_px = follow_lag_count == 0
        ? 0.0
        : follow_lag_sum / static_cast<double>(follow_lag_count);
    summary.assist_delta_p95 = percentile_nearest_rank(assist_deltas, 95.0);
    summary.assist_delta_p99 = percentile_nearest_rank(assist_deltas, 99.0);
    summary.final_jerk_p95 = percentile_nearest_rank(final_jerks, 95.0);
    summary.final_jerk_p99 = percentile_nearest_rank(final_jerks, 99.0);
    summary.bodylock_continuity_defect =
        summary.longest_opposing_assist_run_ms >= options.opposing_assist_defect_ms ||
        summary.longest_manual_gain_plateau_run_ms >=
            options.manual_gain_plateau_defect_ms ||
        summary.assist_delta_p95 >= options.assist_step_threshold ||
        summary.final_jerk_p95 >= options.final_jerk_threshold;
    return summary;
}

}  // namespace replay_native
