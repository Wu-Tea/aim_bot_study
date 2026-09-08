#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace mouse_native {

enum class MouseEmergencyExitResult {
    ReleaseAndStopRequested = 0,
    ReleaseFailedButStopRequested = 1,
    AlreadyRequested = 2,
};

// Owns the only required shutdown ordering rule: stop intercepting the
// physical mouse before asking the application to exit.  Both callbacks must
// be safe to invoke from the independent hotkey thread.
class MouseEmergencyExit {
public:
    using Callback = std::function<void()>;

    MouseEmergencyExit(Callback release_interception, Callback request_stop);

    MouseEmergencyExitResult request() noexcept;
    bool requested() const noexcept;

private:
    Callback release_interception_{};
    Callback request_stop_{};
    std::atomic_bool requested_{false};
};

// Global keyboard-control watcher.  It owns its own Win32 message-loop thread,
// so neither calibration nor the escape key can be missed while Vision or a
// controller tick is busy. start() returns only after both hotkeys have either
// succeeded or failed.
class MouseEmergencyHotkey {
public:
    using Callback = std::function<void()>;
    using CalibrationCallback = std::function<void(bool right_button_down)>;

    MouseEmergencyHotkey();
    ~MouseEmergencyHotkey();

    MouseEmergencyHotkey(const MouseEmergencyHotkey&) = delete;
    MouseEmergencyHotkey& operator=(const MouseEmergencyHotkey&) = delete;

    bool start(
        Callback emergency_callback,
        CalibrationCallback calibration_callback = {},
        bool register_emergency = true);
    void stop() noexcept;
    bool running() const noexcept;
    unsigned long last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mouse_native
