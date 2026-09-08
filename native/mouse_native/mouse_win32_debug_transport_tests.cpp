#include "mouse_native/mouse_win32_debug_transport.h"
#include "runtime_app/runtime_timing.h"

#include <Windows.h>

#include <chrono>
#include <atomic>
#include <future>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int source_magnitude(const mouse_native::MouseWin32DebugSource& source) {
    return std::abs(source.counts.dx) + std::abs(source.counts.dy);
}

// Register before the transport hook, so our transport processes its output
// first and this independent receiver consumes the test click before any app.
class FireReceiver {
public:
    std::atomic<unsigned> downs{0}, ups{0}, moves{0};
    std::atomic_bool capture_movement{false};
    std::atomic<std::int64_t> first_move_ns{0}, last_move_ns{0};
    FireReceiver() {
        owner = this;
        std::promise<bool> ready;
        auto status = ready.get_future();
        worker = std::thread([this, signal = std::move(ready)]() mutable {
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            thread_id.store(GetCurrentThreadId());
            const HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, callback, GetModuleHandleW(nullptr), 0);
            signal.set_value(hook != nullptr);
            if (!hook) return;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            UnhookWindowsHookEx(hook);
        });
        if (!status.get()) {
            worker.join();
            owner = nullptr;
            throw std::runtime_error("independent fire receiver could not install; no click probe started");
        }
    }
    ~FireReceiver() {
        PostThreadMessageW(thread_id.load(), WM_QUIT, 0, 0);
        if (worker.joinable()) worker.join();
        owner = nullptr;
    }
private:
    static inline FireReceiver* owner = nullptr;
    std::atomic<DWORD> thread_id{0};
    std::thread worker;
    static LRESULT CALLBACK callback(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && owner &&
            reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->dwExtraInfo == 0x434F4446u) {
            if (message == WM_LBUTTONDOWN) ++owner->downs;
            if (message == WM_LBUTTONUP) ++owner->ups;
            return 1;
        }
        if (code == HC_ACTION && owner && owner->capture_movement.load() &&
            message == WM_MOUSEMOVE &&
            reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->dwExtraInfo == 0x434F444Du) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (owner->moves.load() == 0) owner->first_move_ns.store(ns);
            owner->last_move_ns.store(ns);
            ++owner->moves;
            return 1; // Cadence probe does not move the user's desktop cursor.
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
};

}  // namespace

void test_movement_output_cadence(mouse_native::MouseWin32DebugTransport& transport,
                                  FireReceiver& receiver) {
    // Independent receiver AFTER the production interception hook. This checks
    // OS hook delivery, not USB polling or a game's WM_INPUT/WM_MOUSEMOVE queue.
    receiver.capture_movement.store(true);
    runtime_app::HighResolutionTimerPeriod timer_period(1);
    (void)runtime_app::set_current_thread_priority(runtime_app::RuntimeThreadPriority::AboveNormal);
    runtime_app::AbsoluteDeadlineState deadlines(
        std::chrono::steady_clock::now(), std::chrono::milliseconds(1));
    const auto reports_before = transport.stats().submitted_reports;
    const auto passed_before = transport.stats().passed_replacements;
    std::uint64_t skipped = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        (void)transport.read_source();
        require_true(transport.submit_final({i % 2 ? -1 : 1, 0}), "cadence output submission failed");
        runtime_app::sleep_until_precise(deadlines.next_deadline());
        skipped += deadlines.advance_after_tick(std::chrono::steady_clock::now());
    }
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    while (receiver.moves.load() < 2000 && std::chrono::steady_clock::now() < timeout) {
        (void)transport.read_source();
        std::this_thread::yield();
    }
    const double seconds = (receiver.last_move_ns.load() - receiver.first_move_ns.load()) / 1e9;
    const double hz = seconds > 0 ? (receiver.moves.load() - 1) / seconds : 0;
    const auto stats = transport.stats();
    receiver.capture_movement.store(false);
    std::cout << "[MouseWin32DebugTransportTests] independent_hook_received=" << receiver.moves
        << "/2000 output_hz=" << hz << " skipped_deadlines=" << skipped << '\n';
    require_true(receiver.moves == 2000 && stats.submitted_reports - reports_before == 2000 &&
        stats.passed_replacements - passed_before == 2000, "every movement must reach the independent receiver");
    require_true(hz >= 950 && hz <= 1050, "output cadence must stay within 5 percent of 1000 Hz on this host");
}

int main(int argc, char** argv) {
    POINT origin{};
    bool physical_input_overlapped_probe = false;
    try {
        FireReceiver fire_receiver;
        if (argc == 2 && std::string(argv[1]) == "--movement-only") {
            mouse_native::MouseWin32DebugTransport transport;
            require_true(transport.start() && transport.enable_interception(), "movement transport failed");
            test_movement_output_cadence(transport, fire_receiver);
            transport.stop();
            return 0;
        }
        require_true(GetCursorPos(&origin) != FALSE, "GetCursorPos failed");
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int right = left + width - 1;
        const int probe_dx = origin.x + 32 <= right ? 16 : -16;

        mouse_native::MouseWin32DebugTransport transport;
        require_true(transport.start(), "debug transport did not start");
        require_true(
            transport.enable_interception(),
            "debug transport did not enable interception");
        (void)transport.read_source();

        require_true(
            transport.submit_calibration({probe_dx, 0}),
            "tagged calibration probe was not submitted");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        POINT after_probe{};
        require_true(GetCursorPos(&after_probe) != FALSE, "probe cursor read failed");
        const auto probe_echo = transport.read_source();
        const auto probe_stats = transport.stats();
        physical_input_overlapped_probe = probe_stats.blocked_moves != 0;
        std::cout << "[MouseWin32DebugTransportTests] probe source=("
            << probe_echo.counts.dx << ',' << probe_echo.counts.dy
            << ") packets=" << probe_echo.sequence
            << " physical_blocked=" << probe_stats.blocked_moves
            << " submitted=" << probe_stats.submitted_reports
            << " passed=" << probe_stats.passed_replacements << '\n';
        require_true(
            (probe_dx > 0 && after_probe.x > origin.x) ||
                (probe_dx < 0 && after_probe.x < origin.x),
            "tagged replacement output was blocked by the physical hook");
        if (!physical_input_overlapped_probe) {
            require_true(source_magnitude(probe_echo) == 0,
                "tagged replacement output echoed without any physical movement");
        }

        require_true(
            transport.submit_calibration({-probe_dx, 0}),
            "tagged calibration return was not submitted");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const auto return_echo = transport.read_source();
        physical_input_overlapped_probe = physical_input_overlapped_probe ||
            transport.stats().blocked_moves != 0;
        if (!physical_input_overlapped_probe) {
            require_true(source_magnitude(return_echo) == 0,
                "tagged return output echoed without any physical movement");
        }

        test_movement_output_cadence(transport, fire_receiver);

        require_true(transport.submit_auto_fire(true), "AutoFire Down submission failed");
        (void)transport.read_source();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require_true(fire_receiver.downs == 1, "independent receiver must see one AutoFire Down");
        require_true(!transport.read_source().left_button_down,
            "AutoFire must not echo into the physical LMB input");
        require_true(transport.submit_auto_fire(true), "held AutoFire update failed");
        require_true(transport.submit_auto_fire(false), "AutoFire Up submission failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require_true(fire_receiver.downs == 1 && fire_receiver.ups == 1,
            "a pulse must produce exactly one Down and one Up");
        require_true(transport.submit_auto_fire(true), "release case pulse failed");
        transport.release_interception();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require_true(fire_receiver.ups == 2, "release must lift the owned automatic press");
        require_true(!transport.submit_auto_fire(true), "release must reject a late automatic press");
        require_true(!transport.submit_final({probe_dx, 0}),
            "released transport must reject late controller output");
        require_true(transport.enable_interception(), "watchdog rearm failed");
        require_true(transport.submit_auto_fire(true), "watchdog case pulse failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        require_true(!transport.intercepting(),
            "a stalled consumer must release physical movement");
        require_true(fire_receiver.downs == 3 && fire_receiver.ups == 3,
            "watchdog must release its automatic button as well as movement");
        require_true(transport.last_error() == ERROR_TIMEOUT,
            "watchdog must expose a transport failure");
        require_true(!transport.submit_calibration({probe_dx, 0}),
            "expired consumer must not inject a late calibration probe");
        transport.stop();
        SetCursorPos(origin.x, origin.y);
        if (physical_input_overlapped_probe) {
            std::cerr << "[MouseWin32DebugTransportTests] INCONCLUSIVE: physical movement overlapped the echo probe; output/release checks passed; rerun with mouse still\n";
            return 2;
        }
        std::cout << "[MouseWin32DebugTransportTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        SetCursorPos(origin.x, origin.y);
        std::cerr << "[MouseWin32DebugTransportTests] FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
