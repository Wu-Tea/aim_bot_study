#include "mouse_native/mouse_win32_debug_transport.h"
#include "mouse_native/mouse_auto_fire_button.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

namespace mouse_native {
namespace {

constexpr wchar_t kWindowClassName[] = L"CodMouseWin32DebugTransportWindow";
constexpr ULONG_PTR kInjectedExtraInfo =
    static_cast<ULONG_PTR>(0x434F444Du);  // "CODM", preserved by RAWMOUSE
constexpr ULONG_PTR kInjectedFireExtraInfo = static_cast<ULONG_PTR>(0x434F4446u);

std::int32_t take_bounded(std::atomic<std::int64_t>* value) noexcept {
    if (value == nullptr) return 0;
    const std::int64_t captured = value->exchange(0, std::memory_order_acq_rel);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        captured,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
}

}  // namespace

struct MouseWin32DebugTransport::Impl {
    static std::atomic<Impl*> active_hook_owner;

    std::thread worker{};
    std::mutex start_mutex{};
    std::condition_variable start_cv{};
    bool start_finished = false;
    bool start_succeeded = false;

    std::atomic_bool running{false};
    std::atomic_bool intercepting{false};
    std::atomic_ulong last_error{ERROR_SUCCESS};
    std::atomic<std::int64_t> pending_dx{0};
    std::atomic<std::int64_t> pending_dy{0};
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> blocked_moves{0};
    std::atomic<std::uint64_t> submitted_reports{0};
    std::atomic<std::uint64_t> passed_replacements{0};
    std::atomic<ULONGLONG> consumer_heartbeat_ms{0};
    MouseAutoFireButton fire_button{};
    std::atomic_bool auto_fire_requested{false};
    std::atomic<std::uint64_t> auto_fire_downs{0};
    std::atomic<std::uint64_t> auto_fire_ups{0};
    std::atomic_bool right_button_down{false};
    std::atomic<DWORD> worker_thread_id{0};

    HHOOK hook = nullptr;
    HWND window = nullptr;
    HINSTANCE module = nullptr;

    void finish_start(bool succeeded, DWORD error) {
        {
            std::lock_guard<std::mutex> lock(start_mutex);
            start_succeeded = succeeded;
            start_finished = true;
            last_error.store(error, std::memory_order_release);
        }
        start_cv.notify_all();
    }

    void fail_start(DWORD error) {
        worker_thread_id.store(0, std::memory_order_release);
        finish_start(false, error);
    }

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param) {
        Impl* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr && message == WM_INPUT) {
            self->accept_raw_input(reinterpret_cast<HRAWINPUT>(l_param));
            // DefWindowProc performs the required foreground Raw Input cleanup.
            return DefWindowProcW(window, message, w_param, l_param);
        }
        if (self != nullptr && message == WM_TIMER) {
            if (self->intercepting.load(std::memory_order_acquire) &&
                GetTickCount64() - self->consumer_heartbeat_ms.load(
                    std::memory_order_acquire) > 100) {
                self->last_error.store(ERROR_TIMEOUT, std::memory_order_release);
                self->intercepting.store(false, std::memory_order_release);
                (void)self->update_auto_fire(false);
            }
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }

    static LRESULT CALLBACK hook_proc(
        int code,
        WPARAM w_param,
        LPARAM l_param) {
        Impl* self = active_hook_owner.load(std::memory_order_acquire);
        if (code != HC_ACTION || self == nullptr) {
            return CallNextHookEx(nullptr, code, w_param, l_param);
        }

        const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
        if (event->dwExtraInfo == kInjectedFireExtraInfo) {
            if (w_param == WM_LBUTTONDOWN) {
                if (!self->fire_button.accept_down(self->intercepting.load(),
                        self->auto_fire_requested.load())) return 1;
                self->auto_fire_downs.fetch_add(1);
            } else if (w_param == WM_LBUTTONUP) {
                // Cleanup Up remains legal after disarm, but can release only
                // our synthetic press, never a button held by the user.
                if (!self->fire_button.accept_up()) return 1;
                self->auto_fire_ups.fetch_add(1);
            } else {
                return 1;
            }
            return CallNextHookEx(nullptr, code, w_param, l_param);
        }
        if (event->dwExtraInfo == kInjectedExtraInfo) {
            // Reject queued replacement movement after emergency release too.
            if (!self->intercepting.load(std::memory_order_acquire)) return 1;
            self->passed_replacements.fetch_add(1, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, w_param, l_param);
        }
        if ((event->flags & LLMHF_INJECTED) != 0) {
            // Controller and calibration output is the replacement path. It
            // must pass and must never be reclassified as physical input.
            return CallNextHookEx(nullptr, code, w_param, l_param);
        }

        switch (w_param) {
        case WM_LBUTTONDOWN:
            self->fire_button.observe_physical(true);
            break;
        case WM_LBUTTONUP:
            self->fire_button.observe_physical(false);
            break;
        case WM_RBUTTONDOWN:
            self->right_button_down.store(true, std::memory_order_release);
            break;
        case WM_RBUTTONUP:
            self->right_button_down.store(false, std::memory_order_release);
            break;
        case WM_MOUSEMOVE:
            if (self->intercepting.load(std::memory_order_acquire)) {
                self->blocked_moves.fetch_add(1, std::memory_order_relaxed);
                return 1;
            }
            break;
        default:
            break;
        }
        return CallNextHookEx(nullptr, code, w_param, l_param);
    }

    void accept_raw_input(HRAWINPUT handle) noexcept {
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied = GetRawInputData(
            handle,
            RID_INPUT,
            &input,
            &size,
            sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1) || input.header.dwType != RIM_TYPEMOUSE) {
            return;
        }

        const RAWMOUSE& mouse = input.data.mouse;
        // SendInput may produce WM_INPUT with a null device handle, but some
        // real/forwarded mouse sources can also be device-less. Filter our own
        // replacement by its explicit marker instead of discarding every null
        // source.
        if (mouse.ulExtraInformation == static_cast<ULONG>(kInjectedExtraInfo) ||
            mouse.ulExtraInformation == static_cast<ULONG>(kInjectedFireExtraInfo)) {
            return;
        }
        if (!intercepting.load(std::memory_order_acquire) ||
            (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
            return;
        }
        pending_dx.fetch_add(mouse.lLastX, std::memory_order_relaxed);
        pending_dy.fetch_add(mouse.lLastY, std::memory_order_relaxed);
        sequence.fetch_add(1, std::memory_order_release);
    }

    void thread_main() {
        worker_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
        module = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &Impl::window_proc;
        window_class.hInstance = module;
        window_class.lpszClassName = kWindowClassName;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            fail_start(GetLastError());
            return;
        }

        window = CreateWindowExW(
            0,
            kWindowClassName,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            module,
            this);
        if (window == nullptr) {
            fail_start(GetLastError());
            return;
        }

        RAWINPUTDEVICE device{};
        device.usUsagePage = 0x01;
        device.usUsage = 0x02;
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = window;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            const DWORD error = GetLastError();
            DestroyWindow(window);
            window = nullptr;
            fail_start(error);
            return;
        }

        Impl* expected = nullptr;
        if (!active_hook_owner.compare_exchange_strong(expected, this)) {
            DestroyWindow(window);
            window = nullptr;
            fail_start(ERROR_BUSY);
            return;
        }
        hook = SetWindowsHookExW(WH_MOUSE_LL, &Impl::hook_proc, module, 0);
        if (hook == nullptr) {
            const DWORD error = GetLastError();
            expected = this;
            (void)active_hook_owner.compare_exchange_strong(expected, nullptr);
            DestroyWindow(window);
            window = nullptr;
            fail_start(error);
            return;
        }

        if (SetTimer(window, 1, 20, nullptr) == 0) {
            const DWORD error = GetLastError();
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            expected = this;
            (void)active_hook_owner.compare_exchange_strong(expected, nullptr);
            DestroyWindow(window);
            window = nullptr;
            fail_start(error);
            return;
        }

        running.store(true, std::memory_order_release);
        finish_start(true, ERROR_SUCCESS);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        intercepting.store(false, std::memory_order_release);
        (void)update_auto_fire(false);
        expected = this;
        (void)active_hook_owner.compare_exchange_strong(expected, nullptr);
        if (hook != nullptr) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
        }
        if (window != nullptr) {
            DestroyWindow(window);
            window = nullptr;
        }
        running.store(false, std::memory_order_release);
        worker_thread_id.store(0, std::memory_order_release);
    }

    bool update_auto_fire(bool pressed) noexcept {
        auto_fire_requested.store(pressed);
        if (pressed && !intercepting.load()) {
            auto_fire_requested.store(false);
            return false;
        }
        const auto edge = fire_button.next_edge(pressed);
        if (edge == MouseAutoFireEdge::None) return true;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = edge == MouseAutoFireEdge::Down
            ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        input.mi.dwExtraInfo = kInjectedFireExtraInfo;
        if (SendInput(1, &input, sizeof(input)) != 1) {
            last_error.store(GetLastError());
            return false;
        }
        return true;
    }

    bool inject(MouseSourceCounts counts) noexcept {
        if (!running.load(std::memory_order_acquire) ||
            !intercepting.load(std::memory_order_acquire)) return false;
        if (counts.dx == 0 && counts.dy == 0) return true;

        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = counts.dx;
        input.mi.dy = counts.dy;
        // Preserve discrete replacement events even when the receiving app drains
        // its message queue less often than our 1 ms output loop.
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
        input.mi.dwExtraInfo = kInjectedExtraInfo;
        if (SendInput(1, &input, sizeof(input)) != 1) {
            last_error.store(GetLastError(), std::memory_order_release);
            return false;
        }
        submitted_reports.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
};

std::atomic<MouseWin32DebugTransport::Impl*>
    MouseWin32DebugTransport::Impl::active_hook_owner{nullptr};

MouseWin32DebugTransport::MouseWin32DebugTransport()
    : impl_(std::make_unique<Impl>()) {}

MouseWin32DebugTransport::~MouseWin32DebugTransport() {
    stop();
}

bool MouseWin32DebugTransport::start() {
    stop();
    {
        std::lock_guard<std::mutex> lock(impl_->start_mutex);
        impl_->start_finished = false;
        impl_->start_succeeded = false;
    }
    impl_->worker = std::thread([this]() { impl_->thread_main(); });

    std::unique_lock<std::mutex> lock(impl_->start_mutex);
    impl_->start_cv.wait(lock, [this]() { return impl_->start_finished; });
    const bool succeeded = impl_->start_succeeded;
    lock.unlock();
    if (!succeeded && impl_->worker.joinable()) impl_->worker.join();
    return succeeded;
}

void MouseWin32DebugTransport::stop() noexcept {
    if (!impl_) return;
    release_interception();
    const DWORD thread_id = impl_->worker_thread_id.load(std::memory_order_acquire);
    if (thread_id != 0) PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    if (impl_->worker.joinable() &&
        impl_->worker.get_id() != std::this_thread::get_id()) {
        impl_->worker.join();
    }
    impl_->pending_dx.store(0, std::memory_order_release);
    impl_->pending_dy.store(0, std::memory_order_release);
}

bool MouseWin32DebugTransport::enable_interception() noexcept {
    if (!running()) return false;
    impl_->pending_dx.store(0, std::memory_order_release);
    impl_->pending_dy.store(0, std::memory_order_release);
    impl_->fire_button.observe_physical((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    impl_->right_button_down.store((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    impl_->last_error.store(ERROR_SUCCESS, std::memory_order_release);
    impl_->consumer_heartbeat_ms.store(GetTickCount64(), std::memory_order_release);
    impl_->intercepting.store(true, std::memory_order_release);
    return true;
}

void MouseWin32DebugTransport::release_interception() noexcept {
    if (impl_) {
        impl_->intercepting.store(false, std::memory_order_release);
        (void)impl_->update_auto_fire(false);
    }
}

MouseWin32DebugSource MouseWin32DebugTransport::read_source() noexcept {
    MouseWin32DebugSource result{};
    if (!impl_) return result;
    impl_->consumer_heartbeat_ms.store(GetTickCount64(), std::memory_order_release);
    result.counts.dx = take_bounded(&impl_->pending_dx);
    result.counts.dy = take_bounded(&impl_->pending_dy);
    result.sequence = impl_->sequence.load(std::memory_order_acquire);
    result.left_button_down =
        impl_->fire_button.physical_down();
    result.right_button_down =
        impl_->right_button_down.load(std::memory_order_acquire);
    return result;
}

bool MouseWin32DebugTransport::submit_final(MouseSourceCounts counts) noexcept {
    return impl_ && impl_->inject(counts);
}

bool MouseWin32DebugTransport::submit_calibration(
    MouseSourceCounts counts) noexcept {
    return impl_ && impl_->inject(counts);
}

bool MouseWin32DebugTransport::submit_auto_fire(bool pressed) noexcept {
    return impl_ && impl_->update_auto_fire(pressed);
}

bool MouseWin32DebugTransport::running() const noexcept {
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

bool MouseWin32DebugTransport::intercepting() const noexcept {
    return impl_ && impl_->intercepting.load(std::memory_order_acquire);
}

unsigned long MouseWin32DebugTransport::last_error() const noexcept {
    return impl_ ? impl_->last_error.load(std::memory_order_acquire)
                 : ERROR_INVALID_STATE;
}

MouseWin32TransportStats MouseWin32DebugTransport::stats() const noexcept {
    return {impl_->sequence.load(), impl_->blocked_moves.load(),
        impl_->submitted_reports.load(), impl_->passed_replacements.load(),
        impl_->auto_fire_downs.load(), impl_->auto_fire_ups.load()};
}

}  // namespace mouse_native
