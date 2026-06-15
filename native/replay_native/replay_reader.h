#pragma once

#include "replay_schema.h"

#include <filesystem>
#include <vector>

namespace replay_native {

struct ReplayReadResult {
    std::vector<NativeReplayFrame> frames;
    std::uint64_t unsupported_lines = 0;
};

ReplayReadResult read_replay_jsonl(const std::filesystem::path& path);

}  // namespace replay_native
