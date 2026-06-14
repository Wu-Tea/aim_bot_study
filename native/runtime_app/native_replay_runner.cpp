#include "native_replay_runner.h"

namespace runtime_app {

replay_native::ReplayMetricSummary NativeReplayRunner::summarize(
    const std::vector<replay_native::NativeReplayFrame>& frames,
    replay_native::ReplayMetricOptions options) const {
    return replay_native::summarize_replay_metrics(frames, options);
}

}  // namespace runtime_app
