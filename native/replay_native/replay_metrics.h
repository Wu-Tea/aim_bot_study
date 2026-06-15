#pragma once

#include "replay_schema.h"

#include <cstdint>
#include <vector>

namespace replay_native {

struct ReplayMetricOptions {
    double max_fire_source_age_ms = 50.0;
};

struct ReplayMetricSummary {
    double target_error_p50_px = 0.0;
    double target_error_p95_px = 0.0;
    double target_error_p99_px = 0.0;
    double projection_age_p95_ms = 0.0;
    double follow_lag_mean_px = 0.0;
    std::uint64_t predicted_only_fire_violations = 0;
    std::uint64_t stale_fire_violations = 0;
};

ReplayMetricSummary summarize_replay_metrics(
    const std::vector<NativeReplayFrame>& frames,
    ReplayMetricOptions options = {});

}  // namespace replay_native
