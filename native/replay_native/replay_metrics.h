#pragma once

#include "replay_schema.h"

#include <cstdint>
#include <vector>

namespace replay_native {

struct ReplayMetricOptions {
    double max_fire_source_age_ms = 50.0;
    double strong_manual_threshold = 0.45;
    double stable_manual_delta_threshold = 0.02;
    double manual_gain_plateau_ratio = 0.55;
    double manual_gain_plateau_tolerance = 0.015;
    double opposing_assist_defect_ms = 16.0;
    double manual_gain_plateau_defect_ms = 8.0;
    double assist_step_threshold = 0.10;
    double final_jerk_threshold = 0.15;
};

struct ReplayMetricSummary {
    double target_error_p50_px = 0.0;
    double target_error_p95_px = 0.0;
    double target_error_p99_px = 0.0;
    double projection_age_p95_ms = 0.0;
    double follow_lag_mean_px = 0.0;
    std::uint64_t predicted_only_fire_violations = 0;
    std::uint64_t stale_fire_violations = 0;
    std::uint64_t strong_manual_axis_samples = 0;
    std::uint64_t opposing_assist_axis_samples = 0;
    std::uint64_t manual_gain_plateau_axis_samples = 0;
    std::uint64_t assist_step_events = 0;
    std::uint64_t final_jerk_events = 0;
    double longest_opposing_assist_run_ms = 0.0;
    double longest_manual_gain_plateau_run_ms = 0.0;
    double assist_delta_p95 = 0.0;
    double assist_delta_p99 = 0.0;
    double final_jerk_p95 = 0.0;
    double final_jerk_p99 = 0.0;
    bool bodylock_continuity_defect = false;
};

ReplayMetricSummary summarize_replay_metrics(
    const std::vector<NativeReplayFrame>& frames,
    ReplayMetricOptions options = {});

}  // namespace replay_native
