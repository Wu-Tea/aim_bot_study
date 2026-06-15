#pragma once

#include "../replay_native/replay_metrics.h"
#include "../replay_native/replay_schema.h"

#include <vector>

namespace runtime_app {

class NativeReplayRunner {
public:
    replay_native::ReplayMetricSummary summarize(
        const std::vector<replay_native::NativeReplayFrame>& frames,
        replay_native::ReplayMetricOptions options = {}) const;
};

}  // namespace runtime_app
