#pragma once

#include "frame_rate_counter.h"
#include <Windows.h>
#include <cstddef>

namespace runtime_app {

// A separate pagefile-backed memory mapping keeps the 640-byte reload/learning
// ABI unchanged. No file backs this channel and no samples are persisted.
enum FrameRateState : std::uint32_t { NotStarted = 0, Sampling = 1, Stopped = 2 };
struct RuntimeFrameRateSnapshot {
    std::uint32_t pid = 0;
    FrameRateState state = NotStarted;
    std::uint64_t created = 0, sampled_at_ms = 0;
    FrameRateCounts counts;
};
struct RuntimeFrameRateMemory {
    volatile LONG sequence = 0;
    std::uint32_t protocol = 1;
    RuntimeFrameRateSnapshot snapshot;
};
static_assert(sizeof(RuntimeFrameRateSnapshot) == 104);
static_assert(sizeof(RuntimeFrameRateMemory) == 112);
static_assert(offsetof(RuntimeFrameRateMemory, snapshot) == 8);
static_assert(offsetof(RuntimeFrameRateSnapshot, counts) == 24);

} // namespace runtime_app
