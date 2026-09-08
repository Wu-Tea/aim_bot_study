#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mouse_native {

struct MouseRuntimeSupervisorOptions {
    bool register_hotkey = true;
    // Only the no-device regression executable sets this. It never opens HID.
    bool simulation = false;
    unsigned long heartbeat_timeout_ms = 350;
    unsigned long graceful_stop_ms = 500;
    unsigned long process_exit_timeout_ms = 1000;
    // Regression-only stop request, through the same path as the F12 message.
    unsigned long simulated_stop_after_ms = 0;
};

// original_command_line includes argv[0], normally GetCommandLineW(). The
// launched worker receives the same arguments plus --mouse-supervisor NAME.
int run_mouse_runtime_supervisor(
    const std::wstring& original_command_line,
    const MouseRuntimeSupervisorOptions& options = {});

// Private child entry: --mouse-supervisor-cleanup SIMULATION OWNER_HANDLE.
// OWNER_HANDLE is an inherited SYNCHRONIZE process handle, never a PID. No HID
// is opened until that process object is truly signaled. Zero is invalid.
int run_mouse_supervisor_cleanup(
    bool simulation, std::uintptr_t owner_process_handle);

class MouseRuntimeSupervisorWorker {
public:
    using EmergencyRelease = std::function<void()>;
    MouseRuntimeSupervisorWorker();
    ~MouseRuntimeSupervisorWorker();
    MouseRuntimeSupervisorWorker(const MouseRuntimeSupervisorWorker&) = delete;
    MouseRuntimeSupervisorWorker& operator=(const MouseRuntimeSupervisorWorker&) = delete;

    // Attach before initialization. The callback must be thread-safe and only
    // release transport ownership / request application stop, not touch the
    // controller. A separate guard bounds a callback or main-thread hang.
    bool attach(const std::wstring& mapping, EmergencyRelease emergency_release);
    // Publish true immediately BEFORE attempting physical arm, after model
    // initialization. Publish false only AFTER transport release has completed.
    void mark_armed(bool armed) noexcept;
    // Only the main thread publishes progress, after a full controller cycle.
    void heartbeat() noexcept;
    bool stop_requested() const noexcept;
    // Call only after the transport's release and close operations completed.
    void shutdown_completed() noexcept;
    unsigned long last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mouse_native
