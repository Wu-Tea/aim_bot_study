#pragma once

#include "controller_native/native_gamepad_controller.h"
#include "controller_native/io_recovery_policy.h"
#include "controller_native/runtime_config.h"
#include "controller_native/sdl_gamepad_reader.h"
#include "controller_native/virtual_gamepad.h"
#include "controller_native/xinput_reader.h"
#include "aim_perf_file_logger.h"
#include "downward_diagnostics.h"
#include "fusion_channel_publisher.h"
#include "log_session_manager.h"
#include "perf_logger.h"
#include "runtime_telemetry.h"
#include "telemetry_collectors.h"
#include "vision_service.h"
#include "vision_native/vision_engine.h"
#include "control_learning/causal_online_response_learner.h"
#include "control_learning/pending_motion_model.h"
#include "control_learning/short_horizon_rollout.h"

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
    bool should_stop_requested() const;
    bool is_aiming(const controller_native::PhysicalGamepadState& physical);

    controller_native::RuntimeConfig config_;
    PerfLogger perf_logger_;
    LogSessionManager log_session_manager_;
    RuntimeTelemetry telemetry_;
    TelemetryCollectors telemetry_collectors_;
    AimPerfFileLogger aim_perf_file_logger_;
    DownwardPullDiagnostics downward_diagnostics_;
    bool perf_log_ = false;
    bool gamepad_perf_log_ = false;
    std::unique_ptr<controller_native::SdlGamepadReader> sdl_input_reader_;
    controller_native::IoReconnectThrottle sdl_reconnect_throttle_{
        std::chrono::milliseconds(500)};
    unsigned int sdl_reconnect_count_ = 0;
    controller_native::XInputReader input_reader_;
    controller_native::NativeGamepadController controller_;
    ViewportController viewport_controller_;
    controller_native::AimActivationTracker aim_activation_tracker_;
    controller_native::VirtualGamepad virtual_gamepad_;
    std::unique_ptr<vision_native::VisionEngine> vision_engine_;
    std::unique_ptr<VisionService> vision_service_;
    VisionDeliveryGate vision_delivery_gate_;
    std::unique_ptr<control_learning::CausalOnlineResponseLearner>
        causal_response_learner_;
    pipeline_contract::CommittedCaptureObservation previous_learning_observation_{};
    bool has_previous_learning_observation_ = false;
    vision_native::VisionResult latest_vision_result_;
    bool has_latest_vision_result_ = false;
    bool latest_vision_aiming_ = false;
    std::uint64_t latest_vision_service_sequence_ = 0;
    std::uint64_t latest_result_timestamp_ns_ = 0;
    std::uint64_t latest_vision_publish_ns_ = 0;
    bool latest_vision_publish_available_ = false;
    std::uint64_t latest_controller_submit_complete_ns_ = 0;
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
