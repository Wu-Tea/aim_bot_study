#include "native_replay_writer.h"

#include <filesystem>
#include <utility>

namespace runtime_app {

NativeReplayWriter::NativeReplayWriter(bool enabled, std::filesystem::path path)
    : enabled_(enabled),
      path_(std::move(path)) {
    if (!enabled_ || path_.empty()) {
        enabled_ = false;
        return;
    }
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    output_.open(path_, std::ios::out | std::ios::trunc);
    enabled_ = output_.is_open();
}

bool NativeReplayWriter::write_frame(const replay_native::NativeReplayFrame& frame) {
    if (!enabled_ || !output_.is_open()) {
        return false;
    }
    output_ << "{\"frame_id\":" << frame.frame_id
            << ",\"target\":" << (frame.selected_target.has_target ? "true" : "false")
            << ",\"dx\":" << frame.selected_target.aim_error_px.x
            << ",\"dy\":" << frame.selected_target.aim_error_px.y
            << ",\"final_x\":" << frame.controller.sticks.final_output.x
            << ",\"final_y\":" << frame.controller.sticks.final_output.y
            << "}\n";
    return true;
}

const std::filesystem::path& NativeReplayWriter::path() const noexcept {
    return path_;
}

}  // namespace runtime_app
