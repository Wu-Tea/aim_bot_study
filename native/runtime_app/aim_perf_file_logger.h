#pragma once

#include "controller_native/controller_tick_context.h"
#include "controller_native/virtual_gamepad.h"
#include "perf_logger.h"
#include "vision_native/types.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace runtime_app {

class AimPerfFileLogger {
public:
    AimPerfFileLogger(
        bool enabled,
        std::filesystem::path log_directory,
        unsigned int log_interval_ticks);

    void record_aim_sample(
        unsigned int tick_count,
        bool aiming,
        const PerfSnapshot& snapshot,
        const vision_native::VisionResult* result,
        const controller_native::NativeControllerVisionState* controller_vision_state = nullptr,
        const controller_native::NativeControllerOutputComponents* output_components = nullptr,
        const controller_native::GamepadOutputState* tracker_motion_output = nullptr);

    const std::filesystem::path& log_path() const noexcept;

private:
    bool enabled_ = false;
    unsigned int log_interval_ticks_ = 1;
    std::chrono::steady_clock::time_point started_at_{};
    std::filesystem::path log_directory_;
    std::filesystem::path log_path_;
    std::ofstream output_;
};

}  // namespace runtime_app
