#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace runtime_app {
struct RuntimeLearningRegion { float effective = 500, learned = 500, confidence = 0; std::uint32_t samples = 0; };
enum RuntimeControlStatus : std::uint32_t {
    Idle = 0, Pending = 1, Applied = 2, RestartRequired = 3, Rejected = 4
};

struct RuntimeControlSnapshot {
    std::uint32_t pid = 0;
    RuntimeControlStatus status = Idle;
    std::uint64_t created = 0, request_id = 0, completed_id = 0, revision = 0, sampled_at_ms = 0;
    RuntimeLearningRegion regions[4]{};
    char fire_output[8]{}, manual_fire_input[8]{}, message[512]{};
};
struct RuntimeControlMemory {
    volatile LONG sequence = 0;
    std::uint32_t protocol = 1;
    volatile LONG64 requested_id = 0;
    RuntimeControlSnapshot snapshot;
};
static_assert(offsetof(RuntimeControlMemory, snapshot) == 16);
static_assert(sizeof(RuntimeControlSnapshot) == 640);

} // namespace runtime_app
