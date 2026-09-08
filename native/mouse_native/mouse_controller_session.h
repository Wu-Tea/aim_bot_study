#pragma once

#include "mouse_native/mouse_controller_runtime_core.h"
#include "mouse_native/mouse_emergency_exit.h"
#include "mouse_native/mouse_relay_client.h"
#include "mouse_native/mouse_win32_debug_transport.h"
#include "mouse_native/mouse_relay_test_mode.h"
#include "mouse_native/mouse_interception_transport.h"
#include "mouse_native/mouse_virtual_hid_transport.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>

namespace mouse_native {

enum class MouseControllerTransport : std::uint8_t {
    KmdfVhf = 0,
    Win32Debug = 1,
    Interception = 2,
    VirtualHid = 3,
};

struct MouseControllerSessionTickResult {
    MouseControllerRuntimeTickResult runtime{};
    MouseSourceCounts source_counts{};
    std::uint64_t through_source_sequence = 0;
    std::uint64_t report_sequence = 0;
    bool relay_ready = false;
    bool output_delivered = false;
    bool transport_failed = false;
    bool calibration_requested = false;
    bool calibration_started = false;
};

// Native user-mode composition boundary. The existing runtime supplies Vision
// snapshots and calls tick at its controller rate; this class owns only mouse
// transport, button reduction, calibration delivery and emergency release.
class MouseControllerSession {
public:
    using StopCallback = std::function<void()>;

    explicit MouseControllerSession(
        MouseControllerFacadeConfig controller_config = {},
        MouseSensitivityCalibratorConfig calibration_config = {},
        MouseControllerTransport transport = MouseControllerTransport::VirtualHid,
        MouseRelayTestMode relay_test_mode = MouseRelayTestMode::None,
        MouseCodDefaultConfig default_config = {},
        int mouse_device = 0,
        std::wstring mouse_hardware = {},
        bool external_emergency_owner = false,
        std::unique_ptr<MouseVirtualHidTransport> virtual_transport = {});
    ~MouseControllerSession();

    MouseControllerSession(const MouseControllerSession&) = delete;
    MouseControllerSession& operator=(const MouseControllerSession&) = delete;

    bool prepare(std::uint64_t lease_id, StopCallback request_application_stop);
    bool arm();
    void shutdown() noexcept;
    // Safe from the supervisor guard: touches only serialized transport state.
    // The main thread still owns controller reset and full session shutdown.
    void emergency_release() noexcept;

    void submit_vision_snapshot(
        const controller_native::ControllerVisionSnapshot& snapshot);
    void set_default_view_height(int height_px) noexcept;
    MouseCalibrationRequestResult begin_calibration(
        bool right_button_down,
        std::uint64_t now_ns);
    bool take_calibration_hotkey(bool* right_button_down) noexcept;
    // Queued at the keyboard boundary; virtual HID resolves physical ADS only
    // after reading the next input window, before producing its one final.
    void request_calibration() noexcept;
    MouseControllerSessionTickResult tick(
        double now_seconds,
        std::uint64_t tick_id);

    bool prepared() const noexcept;
    bool armed() const noexcept;
    bool right_button_down() const noexcept;
    bool left_button_down() const noexcept;
    const MouseControllerRuntimeCore& runtime() const noexcept;
    const MouseRelayClient& relay() const noexcept;
    MouseWin32TransportStats transport_stats() const noexcept;
    unsigned long transport_error() const noexcept;
    MouseVirtualHidTransport::WindowInfo transport_window() const noexcept;

private:
    static void apply_button_flags(
        std::uint32_t flags,
        bool* left_button_down,
        bool* right_button_down) noexcept;
    bool deliver_calibration_output(const MouseRuntimeOutput& output) noexcept;
    bool submit_final_output(
        std::uint64_t report_sequence,
        MouseSourceCounts counts) noexcept;
    bool refresh_heartbeat(std::uint64_t now_ns) noexcept;
    void transport_failure() noexcept;
    bool using_packet_transport() const noexcept;
    MouseUserTransport& packet_transport() noexcept;
    const MouseUserTransport& packet_transport() const noexcept;

    MouseControllerRuntimeCore runtime_;
    MouseControllerTransport transport_ = MouseControllerTransport::VirtualHid;
    MouseRelayTestMode relay_test_mode_ = MouseRelayTestMode::None;
    MouseRelayClient relay_;
    MouseWin32DebugTransport win32_debug_;
    MouseInterceptionTransport interception_;
    std::unique_ptr<MouseVirtualHidTransport> virtual_hid_;
    int mouse_device_ = 0;
    std::wstring mouse_hardware_;
    bool external_emergency_owner_ = false;
    MouseEmergencyHotkey emergency_hotkey_;
    std::unique_ptr<MouseEmergencyExit> emergency_exit_{};
    // 0 = none, 1 = hipfire, 2 = ADS. Written only by the registered-hotkey
    // thread and consumed by the runtime thread.
    std::atomic<unsigned int> pending_calibration_mode_{0};
    bool prepared_ = false;
    bool left_button_down_ = false;
    bool right_button_down_ = false;
    std::uint64_t last_source_sequence_ = 0;
    std::uint64_t next_report_sequence_ = 1;
    std::uint64_t last_heartbeat_ns_ = 0;
};

}  // namespace mouse_native
