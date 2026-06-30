#pragma once

#include "controller_native/native_gamepad_controller.h"
#include "controller_native/runtime_config.h"
#include "controller_native/sdl_gamepad_reader.h"
#include "controller_native/virtual_gamepad.h"
#include "controller_native/weapon_recognizer.h"
#include "controller_native/xinput_reader.h"
#include "aim_perf_file_logger.h"
#include "downward_diagnostics.h"
#include "fusion_channel_publisher.h"
#include "perf_logger.h"
#include "vision_native/vision_engine.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace runtime_app {

class RuntimeLoop {
public:
    RuntimeLoop(controller_native::RuntimeConfig config, bool perf_log, unsigned int max_ticks = 0);

    int run();
    void request_stop();

private:
    void run_once();
    controller_native::PhysicalGamepadState read_physical_gamepad();
    bool should_poll_vision(std::chrono::steady_clock::time_point now) const;
    void update_recoil_recognizer_schedule(
        const controller_native::PhysicalGamepadState& physical,
        std::chrono::steady_clock::time_point now);
    void poll_due_recoil_recognizer(std::chrono::steady_clock::time_point now);
    bool should_stop_requested() const;
    bool is_aiming(const controller_native::PhysicalGamepadState& physical) const;

    controller_native::RuntimeConfig config_;
    PerfLogger perf_logger_;
    AimPerfFileLogger aim_perf_file_logger_;
    DownwardPullDiagnostics downward_diagnostics_;
    bool perf_log_ = false;
    bool gamepad_perf_log_ = false;
    std::unique_ptr<controller_native::SdlGamepadReader> sdl_input_reader_;
    controller_native::XInputReader input_reader_;
    controller_native::NativeGamepadController controller_;
    controller_native::VirtualGamepad virtual_gamepad_;
    std::unique_ptr<controller_native::NativeRecoilWeaponRuntimeRecognizer> recoil_weapon_recognizer_;
    controller_native::RecoilWeaponSwitchCaptureScheduler recoil_switch_scheduler_;
    std::unique_ptr<vision_native::VisionEngine> vision_engine_;
    vision_native::VisionResult latest_vision_result_;
    bool has_latest_vision_result_ = false;
    bool latest_vision_aiming_ = false;
    std::uint64_t latest_result_timestamp_ns_ = 0;
    std::uint64_t latest_controller_consume_started_ns_ = 0;
    unsigned int selected_xinput_user_index_ = 0;
    std::chrono::steady_clock::time_point last_vision_poll_at_{};
    std::atomic_bool stop_requested_{false};
    unsigned int tick_count_ = 0;
    unsigned int max_ticks_ = 0;

    // fusion visual overlay
    FusionChannelPublisher fusion_publisher_;
    bool fusion_enabled_ = false;
};

}  // namespace runtime_app
