#include "mouse_native/mouse_runtime_supervisor.h"
#include "mouse_link/fakerinput_output.h"

#include <Windows.h>

#include <atomic>
#include <iostream>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace mouse_native {
namespace {
constexpr LONG kMagic = 0x4d525332;
constexpr int kExitHotkey = 0x4d53;
constexpr DWORD kForcedExit = 74;
LONG read(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }
ULONGLONG read(volatile LONG64& value) {
    return static_cast<ULONGLONG>(InterlockedCompareExchange64(&value, 0, 0));
}
struct Shared {
    LONG magic = kMagic;
    DWORD parent = 0;
    DWORD heartbeat_ms = 350, grace_ms = 500;
    bool simulation = false;
    volatile LONG attached = 0, stop = 0, armed = 0, ever_armed = 0;
    volatile LONG completed = 0, error = 0;
    alignas(8) volatile LONG64 parent_heartbeat = 0, progress = 0;
};
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Mapping {
    Handle handle;
    Shared* state = nullptr;
    ~Mapping() { if (state) UnmapViewOfFile(state); }
    bool open(const std::wstring& name, bool create) {
        handle.value = create
            ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
            : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!handle.value || (create && GetLastError() == ERROR_ALREADY_EXISTS)) return false;
        state = static_cast<Shared*>(MapViewOfFile(handle.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        return state != nullptr;
    }
};
void fail(Shared& state, DWORD error) {
    InterlockedCompareExchange(&state.error, static_cast<LONG>(error), 0);
    InterlockedExchange(&state.stop, 1);
}
std::wstring executable() {
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return count && count < path.size() ? std::wstring(path.data(), count) : L"";
}
bool launch(const std::wstring& command, PROCESS_INFORMATION& process) {
    const auto exe = executable();
    if (exe.empty()) return false;
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(0);
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    start.wShowWindow = SW_HIDE;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    return CreateProcessW(exe.c_str(), writable.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &start, &process) != FALSE;
}
bool launch_cleanup(HANDLE owner, bool simulation, PROCESS_INFORMATION& process) {
    // The helper owns a real reference to the old process object, including
    // when its startup occurs after the old process has already terminated.
    Handle inherited;
    if (!DuplicateHandle(GetCurrentProcess(), owner, GetCurrentProcess(), &inherited.value,
            SYNCHRONIZE, TRUE, 0)) return false;
    const std::wstring command = L"\"" + executable() + L"\" --mouse-supervisor-cleanup " +
        (simulation ? L"1 " : L"0 ") +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(inherited.value));
    return launch(command, process);
}
bool terminate_and_confirm(HANDLE process, DWORD timeout) {
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return true;
    if (!TerminateProcess(process, kForcedExit) && WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    return WaitForSingleObject(process, timeout) == WAIT_OBJECT_0;
}
std::atomic<std::atomic_bool*> console_stop{nullptr};
std::atomic<HANDLE> console_done{nullptr};
BOOL WINAPI console_handler(DWORD event) {
    auto* stop = console_stop.load();
    if (!stop) return FALSE;
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT &&
        event != CTRL_SHUTDOWN_EVENT && event != CTRL_LOGOFF_EVENT) return FALSE;
    stop->store(true);
    const HANDLE done = console_done.load();
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && done)
        WaitForSingleObject(done, 4000);
    return TRUE;
}
struct ConsoleScope {
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool registered = false;
    explicit ConsoleScope(std::atomic_bool& stop) {
        console_stop = &stop;
        console_done = done.value;
        registered = SetConsoleCtrlHandler(console_handler, TRUE) != FALSE;
    }
    ~ConsoleScope() {
        SetEvent(done.value);
        if (registered) SetConsoleCtrlHandler(console_handler, FALSE);
        console_stop = nullptr;
        console_done = nullptr;
    }
};
}  // namespace

int run_mouse_supervisor_cleanup(bool simulation, std::uintptr_t owner_process_handle) {
    if (!owner_process_handle) return 2;
    Handle owner{reinterpret_cast<HANDLE>(owner_process_handle)};
    // Covers enumeration, HID IO/cancellation and destructor hangs, including
    // the orphan helper created when no live parent remains to supervise it.
    Handle finished{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!finished.value) return 2;
    std::thread bound([&] {
        if (WaitForSingleObject(finished.value, 1500) != WAIT_OBJECT_0) {
            // A diagnostic pipe can itself block. The exit code carries the
            // timeout; this last recovery thread performs no console IO.
            TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
        }
    });
    int result = 2;
    if (WaitForSingleObject(owner.value, 1000) == WAIT_OBJECT_0) {
        if (simulation) result = 0;
        else {
            virtual_mouse::FakerOutput output;
            if (output.open() && output.send({})) result = 0;
            else std::cerr << "[MouseSupervisor] neutral cleanup failed, error=" << output.error() << '\n';
            output.close();
        }
    } else {
        std::cerr << "[MouseSupervisor] old input owner has not exited; neutral output forbidden\n";
    }
    SetEvent(finished.value);
    bound.join();
    return result;
}

int run_mouse_runtime_supervisor(const std::wstring& original_command_line,
        const MouseRuntimeSupervisorOptions& options) {
    if (original_command_line.empty() || !options.heartbeat_timeout_ms || !options.graceful_stop_ms ||
        !options.process_exit_timeout_ms || (options.simulated_stop_after_ms && !options.simulation)) return 2;
    Mapping mapping;
    const auto name = L"Local\\CodMouseRuntime_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64());
    if (!mapping.open(name, true)) return 2;
    new (mapping.state) Shared{};
    auto& state = *mapping.state;
    state.parent = GetCurrentProcessId(); state.simulation = options.simulation;
    state.heartbeat_ms = options.heartbeat_timeout_ms; state.grace_ms = options.graceful_stop_ms;
    InterlockedExchange64(&state.parent_heartbeat, GetTickCount64());
    std::atomic_bool stop{false};
    ConsoleScope console(stop);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    if (!console.registered || (options.register_hotkey &&
        !RegisterHotKey(nullptr, kExitHotkey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12))) {
        std::cerr << "[MouseSupervisor] independent exit control unavailable; worker was not started\n";
        return 2;
    }
    struct HotkeyScope {
        bool registered;
        ~HotkeyScope() { if (registered) UnregisterHotKey(nullptr, kExitHotkey); }
    } hotkey{options.register_hotkey};
    PROCESS_INFORMATION worker_info{};
    if (!launch(original_command_line + L" --mouse-supervisor \"" + name + L"\"", worker_info)) {
        std::cerr << "[MouseSupervisor] worker launch failed, error=" << GetLastError() << '\n';
        return 2;
    }
    Handle worker{worker_info.hProcess}, worker_thread{worker_info.hThread};
    const auto started = GetTickCount64();
    ULONGLONG stop_at = 0;
    bool forced = false, confirmed = false;
    while (true) {
        const auto now = GetTickCount64();
        InterlockedExchange64(&state.parent_heartbeat, now);
        const auto wait = WaitForSingleObject(worker.value, 0);
        if (wait == WAIT_OBJECT_0) { confirmed = true; break; }
        if (wait == WAIT_FAILED) { fail(state, GetLastError()); break; }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if ((message.message == WM_HOTKEY && message.wParam == kExitHotkey) || message.message == WM_QUIT)
                stop.store(true);
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (options.simulated_stop_after_ms && now - started >= options.simulated_stop_after_ms) stop.store(true);
        if (stop.load()) InterlockedExchange(&state.stop, 1);
        // Initialization can take minutes. Only successful main-thread cycles
        // advance this clock once physical ownership may have begun.
        if (read(state.armed) && now - read(state.progress) > state.heartbeat_ms)
            fail(state, ERROR_TIMEOUT);
        if (read(state.stop)) {
            if (!stop_at) stop_at = now;
            if (now - stop_at >= state.grace_ms) {
                forced = true;
                confirmed = terminate_and_confirm(worker.value, options.process_exit_timeout_ms);
                break;
            }
        }
        Sleep(5);
    }
    if (!confirmed) {
        std::cerr << "[MouseSupervisor] worker process object did not terminate; neutral output forbidden, recovery unverified\n";
        return 2;
    }
    DWORD worker_exit = 0;
    if (!GetExitCodeProcess(worker.value, &worker_exit)) return 2;
    bool cleanup_ok = true;
    if (read(state.ever_armed)) {
        PROCESS_INFORMATION cleanup_info{};
        cleanup_ok = launch_cleanup(worker.value, options.simulation, cleanup_info);
        if (cleanup_ok) {
            Handle cleanup{cleanup_info.hProcess}, cleanup_thread{cleanup_info.hThread};
            if (WaitForSingleObject(cleanup.value, 1800) != WAIT_OBJECT_0) {
                cleanup_ok = false;
                const bool helper_ended = terminate_and_confirm(cleanup.value, options.process_exit_timeout_ms);
                std::cerr << "[MouseSupervisor] cleanup timeout; helper_exit_confirmed=" << helper_ended << '\n';
            } else {
                DWORD code = 0;
                cleanup_ok = GetExitCodeProcess(cleanup.value, &code) && code == 0;
            }
        }
    }
    std::cout << "[MouseSupervisor] worker_exit=" << worker_exit << " forced=" << forced
        << " worker_exit_confirmed=1 neutral_cleanup=" << (cleanup_ok ? "complete" : "failed")
        << " simulation=" << options.simulation << '\n';
    if (!cleanup_ok || forced || read(state.error)) return 2;
    return static_cast<int>(worker_exit);
}

struct MouseRuntimeSupervisorWorker::Impl {
    Mapping mapping;
    Handle parent;
    EmergencyRelease release;
    std::thread guard, cleanup;
    std::atomic_bool local_stop{false}, cleanup_done{false}, closing{false};
    std::atomic<DWORD> error{0};
};
MouseRuntimeSupervisorWorker::MouseRuntimeSupervisorWorker() : impl_(std::make_unique<Impl>()) {}
MouseRuntimeSupervisorWorker::~MouseRuntimeSupervisorWorker() {
    // Exception unwinding is not evidence of release. The worker guard must
    // retain its deadline until the thread-safe transport callback finishes.
    if (impl_->guard.joinable() && !read(impl_->mapping.state->completed))
        InterlockedExchange(&impl_->mapping.state->stop, 1);
    impl_->closing.store(true);
    if (impl_->guard.joinable()) impl_->guard.join();
    if (impl_->cleanup.joinable()) impl_->cleanup.join();
}
bool MouseRuntimeSupervisorWorker::attach(const std::wstring& name, EmergencyRelease release) {
    if (impl_->mapping.state || !release || !impl_->mapping.open(name, false) || impl_->mapping.state->magic != kMagic)
        return false;
    auto& state = *impl_->mapping.state;
    impl_->parent.value = OpenProcess(SYNCHRONIZE, FALSE, state.parent);
    if (!impl_->parent.value || WaitForSingleObject(impl_->parent.value, 0) != WAIT_TIMEOUT) return false;
    impl_->release = std::move(release);
    InterlockedExchange64(&state.progress, GetTickCount64());
    InterlockedExchange(&state.attached, 1);
    impl_->guard = std::thread([this, &state] {
        ULONGLONG stop_at = 0;
        while (true) {
            const auto now = GetTickCount64();
            const DWORD parent_wait = WaitForSingleObject(impl_->parent.value, 0);
            if (parent_wait != WAIT_TIMEOUT || now - read(state.parent_heartbeat) > state.heartbeat_ms) {
                impl_->error.store(parent_wait == WAIT_OBJECT_0 ? ERROR_PROCESS_ABORTED : ERROR_TIMEOUT);
                fail(state, impl_->error.load());
            }
            if (read(state.stop) && !stop_at) {
                stop_at = now;
                impl_->local_stop.store(true);
                impl_->cleanup = std::thread([this, &state] {
                    try { impl_->release(); }
                    catch (...) { fail(state, ERROR_GEN_FAILURE); impl_->error.store(ERROR_GEN_FAILURE); }
                    impl_->cleanup_done.store(true);
                });
            }
            if (read(state.completed) && (!stop_at || impl_->cleanup_done.load())) break;
            if (impl_->closing.load() && impl_->cleanup_done.load()) break;
            if (stop_at && now - stop_at >= state.grace_ms) {
                // The parent normally performs recovery. If it died or its
                // heartbeat stopped, give an independent helper a reference to
                // this process before terminating; it cannot race this writer.
                if (parent_wait != WAIT_TIMEOUT || now - read(state.parent_heartbeat) > state.heartbeat_ms) {
                    PROCESS_INFORMATION helper{};
                    if (read(state.ever_armed) && launch_cleanup(GetCurrentProcess(), state.simulation, helper)) {
                        CloseHandle(helper.hThread); CloseHandle(helper.hProcess);
                    } else if (read(state.ever_armed)) fail(state, GetLastError());
                }
                TerminateProcess(GetCurrentProcess(), kForcedExit);
            }
            Sleep(10);
        }
    });
    return true;
}
void MouseRuntimeSupervisorWorker::mark_armed(bool armed) noexcept {
    if (!impl_->mapping.state) return;
    auto& state = *impl_->mapping.state;
    if (armed) { heartbeat(); InterlockedExchange(&state.ever_armed, 1); }
    InterlockedExchange(&state.armed, armed ? 1 : 0);
}
void MouseRuntimeSupervisorWorker::heartbeat() noexcept {
    if (impl_->mapping.state) InterlockedExchange64(&impl_->mapping.state->progress, GetTickCount64());
}
bool MouseRuntimeSupervisorWorker::stop_requested() const noexcept {
    return impl_->local_stop.load() || (impl_->mapping.state && read(impl_->mapping.state->stop));
}
void MouseRuntimeSupervisorWorker::shutdown_completed() noexcept {
    if (impl_->mapping.state) {
        InterlockedExchange(&impl_->mapping.state->armed, 0);
        InterlockedExchange(&impl_->mapping.state->completed, 1);
    }
    // The caller may destroy the session immediately after this returns. Join
    // here so no late parent/stop observation can invoke its callback again.
    if (impl_->guard.joinable()) impl_->guard.join();
    if (impl_->cleanup.joinable()) impl_->cleanup.join();
}
unsigned long MouseRuntimeSupervisorWorker::last_error() const noexcept { return impl_->error.load(); }
}  // namespace mouse_native
