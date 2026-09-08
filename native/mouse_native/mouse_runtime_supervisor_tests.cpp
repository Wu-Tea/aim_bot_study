#include "mouse_native/mouse_runtime_supervisor.h"

#include <Windows.h>

#include <atomic>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Evidence { volatile LONG worker_pid = 0, release_count = 0, armed = 0; };
struct Mapping {
    HANDLE handle = nullptr;
    Evidence* data = nullptr;
    explicit Mapping(const std::wstring& name, bool create) {
        handle = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Evidence), name.c_str())
                        : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (handle) data = static_cast<Evidence*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Evidence)));
        if (!data) throw std::runtime_error("test evidence mapping failed");
        if (create) new (data) Evidence{};
    }
    ~Mapping() { if (data) UnmapViewOfFile(data); if (handle) CloseHandle(handle); }
};
LONG read(volatile LONG& value) { return InterlockedCompareExchange(&value, 0, 0); }
std::wstring exe() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return path;
}
struct Process {
    PROCESS_INFORMATION info{};
    explicit Process(const std::wstring& arguments, bool inherit = false) {
        const auto command = L"\"" + exe() + L"\" " + arguments;
        std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(0);
        STARTUPINFOW start{}; start.cb = sizeof(start);
        if (!CreateProcessW(exe().c_str(), writable.data(), nullptr, nullptr, inherit,
                CREATE_NO_WINDOW, nullptr, nullptr, &start, &info)) throw std::runtime_error("test process launch failed");
    }
    ~Process() {
        if (WaitForSingleObject(info.hProcess, 0) != WAIT_OBJECT_0) {
            TerminateProcess(info.hProcess, 98); WaitForSingleObject(info.hProcess, 2000);
        }
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
    DWORD wait(DWORD milliseconds = 6000) {
        if (WaitForSingleObject(info.hProcess, milliseconds) != WAIT_OBJECT_0) throw std::runtime_error("test process deadline");
        DWORD result = 0; GetExitCodeProcess(info.hProcess, &result); return result;
    }
};
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int fake_worker(const std::wstring& scenario, const std::wstring& evidence_name, const std::wstring& name) {
    Mapping evidence(evidence_name, false);
    std::atomic_bool stop{false};
    mouse_native::MouseRuntimeSupervisorWorker worker;
    require(worker.attach(name, [&] {
        InterlockedIncrement(&evidence.data->release_count);
        stop.store(true);
        if (scenario == L"cleanup-hang" || scenario == L"orphan-hang" ||
            scenario == L"scope-hang" || scenario == L"orphan-scope-hang") Sleep(INFINITE);
    }), "worker attach failed");
    InterlockedExchange(&evidence.data->worker_pid, static_cast<LONG>(GetCurrentProcessId()));
    if (scenario == L"startup") Sleep(650); // Longer than armed heartbeat budget.
    worker.mark_armed(true);
    InterlockedExchange(&evidence.data->armed, 1);
    if (scenario == L"scope-release" || scenario == L"scope-hang") return 0;
    if (scenario == L"hang" || scenario == L"cleanup-hang" || scenario == L"orphan-hang") Sleep(INFINITE);
    if (scenario == L"crash") TerminateProcess(GetCurrentProcess(), 73);
    const auto start = GetTickCount64();
    while (!stop.load() && !worker.stop_requested()) {
        worker.heartbeat();
        if ((scenario == L"normal" || scenario == L"startup") && GetTickCount64() - start > 100) break;
        Sleep(5);
    }
    if (scenario == L"orphan-scope-hang") return 0;
    worker.mark_armed(false);
    worker.shutdown_completed();
    return 0;
}
int test_parent(const std::wstring& scenario, const std::wstring& evidence_name) {
    mouse_native::MouseRuntimeSupervisorOptions options;
    options.simulation = true;
    options.register_hotkey = false;
    if (scenario == L"stop") options.simulated_stop_after_ms = 100;
    return mouse_native::run_mouse_runtime_supervisor(
        L"\"" + exe() + L"\" --fake-worker " + scenario + L" \"" + evidence_name + L"\"", options);
}
void verify_case(const std::wstring& scenario, DWORD expected, bool expect_release) {
    const auto name = L"Local\\MouseSupervisorEvidence_" + std::to_wstring(GetCurrentProcessId()) + scenario;
    Mapping evidence(name, true);
    Process parent(L"--test-parent " + scenario + L" \"" + name + L"\"");
    require(parent.wait() == expected, "supervisor result did not match expected success/failure");
    require(read(evidence.data->armed) == 1, "scenario never reached fake ownership");
    if (expect_release) require(read(evidence.data->release_count) == 1, "stop did not reach independent release callback exactly once");
    std::wcout << L"PASS " << scenario << L'\n';
}
void verify_parent_death(const std::wstring& scenario) {
    const auto name = L"Local\\MouseSupervisorDeath_" + std::to_wstring(GetCurrentProcessId()) + scenario;
    Mapping evidence(name, true);
    Process parent(L"--test-parent " + scenario + L" \"" + name + L"\"");
    const auto deadline = GetTickCount64() + 3000;
    while (!read(evidence.data->armed) && GetTickCount64() < deadline) Sleep(5);
    require(read(evidence.data->armed) == 1, "orphan worker not ready");
    HANDLE worker = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, static_cast<DWORD>(read(evidence.data->worker_pid)));
    require(worker != nullptr, "cannot observe orphan process object");
    TerminateProcess(parent.info.hProcess, 79);
    parent.wait();
    const auto result = WaitForSingleObject(worker, 3000);
    if (result != WAIT_OBJECT_0) { TerminateProcess(worker, 98); WaitForSingleObject(worker, 1000); }
    CloseHandle(worker);
    require(result == WAIT_OBJECT_0, "worker survived parent death beyond grace");
    require(read(evidence.data->release_count) == 1, "parent death did not attempt release");
    std::wcout << L"PASS parent-death " << scenario << L'\n';
}
void verify_cleanup_waits_for_owner() {
    Process owner(L"--fake-owner");
    HANDLE inherited = nullptr;
    require(DuplicateHandle(GetCurrentProcess(), owner.info.hProcess, GetCurrentProcess(), &inherited,
        SYNCHRONIZE, TRUE, 0) != FALSE, "cannot duplicate owner handle");
    Process cleanup(L"--mouse-supervisor-cleanup 1 " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(inherited)), true);
    CloseHandle(inherited);
    require(WaitForSingleObject(cleanup.info.hProcess, 100) == WAIT_TIMEOUT,
        "cleanup completed while old output owner could still write");
    TerminateProcess(owner.info.hProcess, 0);
    owner.wait();
    require(cleanup.wait() == 0, "simulated cleanup did not run after real owner exit");
    std::cout << "PASS cleanup waits for process object\n";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--mouse-supervisor-cleanup")
            return mouse_native::run_mouse_supervisor_cleanup(std::wstring(argv[2]) == L"1", std::stoull(argv[3]));
        if (argc == 6 && std::wstring(argv[1]) == L"--fake-worker" && std::wstring(argv[4]) == L"--mouse-supervisor")
            return fake_worker(argv[2], argv[3], argv[5]);
        if (argc == 4 && std::wstring(argv[1]) == L"--test-parent") return test_parent(argv[2], argv[3]);
        if (argc == 2 && std::wstring(argv[1]) == L"--fake-owner") { Sleep(INFINITE); return 0; }
        require(argc == 1, "unknown test command");
        verify_case(L"normal", 0, false);
        verify_case(L"startup", 0, false);
        verify_case(L"stop", 0, true);
        verify_case(L"hang", 2, true);
        verify_case(L"cleanup-hang", 2, true);
        verify_case(L"crash", 73, false);
        verify_case(L"scope-release", 0, true);
        verify_case(L"scope-hang", 2, true);
        verify_parent_death(L"orphan");
        verify_parent_death(L"orphan-hang");
        verify_parent_death(L"orphan-scope-hang");
        verify_cleanup_waits_for_owner();
        std::cout << "Mouse runtime supervisor: 12 no-device cases passed; no hotkey registration, driver, or HID IO\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
