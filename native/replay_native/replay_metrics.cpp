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

bool is_predicted_or_projected_only(const NativeReplayFrame& frame) {
    if (frame.tracker.fire_authority != common_native::FireAuthority::None) {
        return false;
    }
    return frame.tracker.source == tracking_native::TrackerSnapshotSource::Projected ||
        frame.tracker.source == tracking_native::TrackerSnapshotSource::Coast;
}

}  // namespace

ReplayMetricSummary summarize_replay_metrics(
    const std::vector<NativeReplayFrame>& frames,
    ReplayMetricOptions options) {
    std::vector<double> target_errors;
    std::vector<double> projection_ages;
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
        projection_ages.push_back(frame.tracker.projection_age_ms);

        if (frame.controller.fire_allowed && is_predicted_or_projected_only(frame)) {
            ++summary.predicted_only_fire_violations;
        }
        if (frame.controller.fire_allowed &&
            frame.selected_target.age_ms > options.max_fire_source_age_ms) {
            ++summary.stale_fire_violations;
        }
    }

    summary.target_error_p50_px = percentile_nearest_rank(target_errors, 50.0);
    summary.target_error_p95_px = percentile_nearest_rank(target_errors, 95.0);
    summary.target_error_p99_px = percentile_nearest_rank(target_errors, 99.0);
    summary.projection_age_p95_ms = percentile_nearest_rank(projection_ages, 95.0);
    summary.follow_lag_mean_px = follow_lag_count == 0
        ? 0.0
        : follow_lag_sum / static_cast<double>(follow_lag_count);
    return summary;
}

}  // namespace replay_native
