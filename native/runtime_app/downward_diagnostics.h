#pragma once

#include "controller_native/native_gamepad_controller.h"
#include "controller_native/virtual_gamepad.h"
#include "controller_native/xinput_reader.h"
#include "vision_native/types.h"

#include <filesystem>

namespace runtime_app {

struct DownwardPullDiagnosticsConfig {
    bool enabled = false;
    std::filesystem::path output_path =
        "artifacts/diagnostics/gamepad_downward_pull.jsonl";
    int downward_delta_threshold = 6000;
};

class DownwardPullDiagnostics {
public:
    explicit DownwardPullDiagnostics(DownwardPullDiagnosticsConfig config = {});

    static DownwardPullDiagnostics from_environment();

    bool record_if_triggered(
        const controller_native::PhysicalGamepadState& physical,
        const controller_native::GamepadOutputState& output,
        const controller_native::NativeControllerStageTraceBuffer& traces,
        const vision_native::VisionResult* latest_result,
        bool is_aiming) const;

private:
    DownwardPullDiagnosticsConfig config_;
};

}  // namespace runtime_app
