#include "mouse_native/mouse_emergency_exit.h"

#include <Windows.h>

#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace mouse_native {
namespace {

constexpr int kEmergencyHotkeyId = 0x4D43;
constexpr int kCalibrationHotkeyId = 0x4D44;

}  // namespace

MouseEmergencyExit::MouseEmergencyExit(
    Callback release_interception,
    Callback request_stop)
    : release_interception_(std::move(release_interception)),
      request_stop_(std::move(request_stop)) {}

MouseEmergencyExitResult MouseEmergencyExit::request() noexcept {
    if (requested_.exchange(true)) {
        return MouseEmergencyExitResult::AlreadyRequested;
    }

    bool release_failed = false;
    try {
        if (release_interception_) {
            release_interception_();
        }
    } catch (...) {
        release_failed = true;
    }

    // A broken release callback must not prevent the normal application stop
    // request.  A kernel-backed transport still has owner cleanup/heartbeat as
    // its last recovery path when the process closes.
    try {
        if (request_stop_) {
            request_stop_();
        }
    } catch (...) {
        // The escape thread must never terminate through a user callback.
    }
    return release_failed
        ? MouseEmergencyExitResult::ReleaseFailedButStopRequested
        : MouseEmergencyExitResult::ReleaseAndStopRequested;
}

bool MouseEmergencyExit::requested() const noexcept {
    return requested_.load();
}

struct MouseEmergencyHotkey::Impl {
    std::mutex mutex{};
    std::thread thread{};
    Callback emergency_callback{};
    CalibrationCallback calibration_callback{};
    std::atomic_bool running{false};
    std::atomic<DWORD> thread_id{0};
    std::atomic<DWORD> last_error{ERROR_SUCCESS};
    bool register_emergency = true;
};

MouseEmergencyHotkey::MouseEmergencyHotkey()
    : impl_(std::make_unique<Impl>()) {}

MouseEmergencyHotkey::~MouseEmergencyHotkey() {
    stop();
}

bool MouseEmergencyHotkey::start(
    Callback emergency_callback,
    CalibrationCallback calibration_callback,
    bool register_emergency) {
    if ((register_emergency && !emergency_callback) ||
        (!register_emergency && !calibration_callback)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->thread.joinable() || impl_->running.load()) {
        return false;
    }

    impl_->emergency_callback = std::move(emergency_callback);
    impl_->calibration_callback = std::move(calibration_callback);
    impl_->register_emergency = register_emergency;
    impl_->last_error.store(ERROR_SUCCESS);
    std::promise<bool> registration;
    std::future<bool> registered = registration.get_future();

    impl_->thread = std::thread([this, ready = std::move(registration)]() mutable {
        MSG message{};
        // Force creation of this thread's message queue before publishing the
        // thread id used by stop().
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        impl_->thread_id.store(GetCurrentThreadId());

        const UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
        if (impl_->register_emergency &&
            !RegisterHotKey(nullptr, kEmergencyHotkeyId, modifiers, VK_F12)) {
            impl_->last_error.store(GetLastError());
            impl_->thread_id.store(0);
            ready.set_value(false);
            return;
        }

        if (impl_->calibration_callback &&
            !RegisterHotKey(nullptr, kCalibrationHotkeyId, modifiers, VK_F11)) {
            impl_->last_error.store(GetLastError());
            if (impl_->register_emergency) UnregisterHotKey(nullptr, kEmergencyHotkeyId);
            impl_->thread_id.store(0);
            ready.set_value(false);
            return;
        }

        impl_->running.store(true);
        ready.set_value(true);
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.message != WM_HOTKEY) continue;

            if (static_cast<int>(message.wParam) == kCalibrationHotkeyId) {
                try {
                    const bool right_button_down =
                        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
                    impl_->calibration_callback(right_button_down);
                } catch (...) {
                    // Keep the independent safety watcher alive even if the
                    // request sink fails.
                }
                continue;
            }

            if (static_cast<int>(message.wParam) == kEmergencyHotkeyId) {
                try {
                    impl_->emergency_callback();
                } catch (...) {
                    // Never let a callback exception kill the watcher thread.
                }
                break;
            }
        }

        impl_->running.store(false);
        if (impl_->calibration_callback) {
            UnregisterHotKey(nullptr, kCalibrationHotkeyId);
        }
        if (impl_->register_emergency) UnregisterHotKey(nullptr, kEmergencyHotkeyId);
        impl_->thread_id.store(0);
    });

    const bool success = registered.get();
    if (!success) {
        impl_->thread.join();
        impl_->emergency_callback = {};
        impl_->calibration_callback = {};
    }
    return success;
}

void MouseEmergencyHotkey::stop() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->thread.joinable()) {
        impl_->running.store(false);
        impl_->emergency_callback = {};
        impl_->calibration_callback = {};
        return;
    }

    const DWORD id = impl_->thread_id.load();
    if (id != 0) {
        PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    impl_->thread.join();
    impl_->running.store(false);
    impl_->emergency_callback = {};
    impl_->calibration_callback = {};
}

bool MouseEmergencyHotkey::running() const noexcept {
    return impl_->running.load();
}

unsigned long MouseEmergencyHotkey::last_error() const noexcept {
    return static_cast<unsigned long>(impl_->last_error.load());
}

}  // namespace mouse_native
