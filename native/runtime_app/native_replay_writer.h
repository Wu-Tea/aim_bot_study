#pragma once

#include "../replay_native/replay_schema.h"

#include <filesystem>
#include <fstream>

namespace runtime_app {

class NativeReplayWriter {
public:
    NativeReplayWriter(bool enabled, std::filesystem::path path);

    bool write_frame(const replay_native::NativeReplayFrame& frame);
    const std::filesystem::path& path() const noexcept;

private:
    bool enabled_ = false;
    std::filesystem::path path_;
    std::ofstream output_;
};

}  // namespace runtime_app
