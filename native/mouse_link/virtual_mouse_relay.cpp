#include "fakerinput_output.h"
#include "mouse_native/mouse_interception_transport.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace vm = virtual_mouse;
namespace mn = mouse_native;
namespace {
constexpr LONG kMagic = 0x564d5231;
constexpr LONG kCapacity = 400000;
LONG get(volatile LONG& v) { return InterlockedCompareExchange(&v, 0, 0); }
LONG64 get64(volatile LONG64& v) { return InterlockedCompareExchange64(&v, 0, 0); }
LONGLONG stamp() { LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return v.QuadPart; }
struct Record {
    LONGLONG qpc = 0;
    int kind = 0, mode = 0, x = 0, y = 0, buttons = 0, wheel = 0, hwheel = 0;
};
struct Shared {
    LONG magic = kMagic;
    int source = 0;
    DWORD parent = 0;
    bool simulate = false, hang = false, preflight = false;
    wchar_t hardware[512]{};
    volatile LONG stop = 0, ready = 0, active = 0, mode = 0, error = 0, phase = 0;
    volatile LONG written = 0, diagnostics_truncated = 0;
    volatile LONG64 heartbeat = 0, parent_heartbeat = 0, armed_at = 0, released_at = 0;
    char failure_reason[256]{};
    Record records[kCapacity]{};
};
struct Mapping {
    HANDLE handle = nullptr;
    Shared* state = nullptr;
    bool open(const std::wstring& name, bool create) {
        handle = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), name.c_str())
                        : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!handle || (create && GetLastError() == ERROR_ALREADY_EXISTS)) return false;
        state = static_cast<Shared*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        return state != nullptr;
    }
    ~Mapping() { if (state) UnmapViewOfFile(state); if (handle) CloseHandle(handle); }
};
void record(Shared& s, int kind, vm::Mode mode, int x, int y, int buttons, int wheel, int hwheel = 0) {
    const LONG index = get(s.written);
    if (index >= kCapacity) { InterlockedExchange(&s.diagnostics_truncated, 1); return; }
    s.records[index] = {stamp(), kind, static_cast<int>(mode), x, y, buttons, wheel, hwheel};
    InterlockedExchange(&s.written, index + 1); // Publish only complete records.
}
bool any_button_down() {
    for (int vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    return false;
}
bool looks_physical(const std::wstring& hardware) {
    std::wstring upper = hardware;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    return upper.find(L"HID\\VID_") == 0 && upper.find(L"VID_FE0F") == std::wstring::npos;
}
int worker(const std::wstring& name) {
    Mapping map;
    if (!map.open(name, false) || map.state->magic != kMagic) return 20;
    Shared& s = *map.state;
    InterlockedExchange(&s.phase, 1);
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, s.parent);
    if (!parent) { InterlockedExchange(&s.error, ERROR_INVALID_HANDLE); return 21; }
    auto driver = mn::make_interception_driver();
    vm::FakerOutput output;
    bool captured = false, ownership_started = false;
    std::uint8_t buttons = 0;
    const auto alive = [&] {
        InterlockedExchange64(&s.heartbeat, GetTickCount64());
        if (GetTickCount64() - static_cast<ULONGLONG>(get64(s.parent_heartbeat)) > 350) {
            InterlockedExchange(&s.error, ERROR_TIMEOUT); return false;
        }
        return !get(s.stop) && WaitForSingleObject(parent, 0) == WAIT_TIMEOUT;
    };
    try {
        if (s.simulate) {
            InterlockedExchange(&s.ready, 1);
            if (s.hang) Sleep(INFINITE);
            while (alive()) Sleep(5);
        } else {
            InterlockedExchange(&s.phase, 2);
            if (!driver->open()) throw std::runtime_error("Interception unavailable");
            InterlockedExchange(&s.phase, 3);
            const auto devices = driver->devices();
            const auto source = std::find_if(devices.begin(), devices.end(), [&](const auto& d) {
                return d.id == s.source && d.hardware_id == s.hardware && looks_physical(d.hardware_id);
            });
            if (source == devices.end()) throw std::runtime_error("Source identity changed or is virtual");
            InterlockedExchange(&s.phase, 4);
            if (!output.open()) { InterlockedExchange(&s.error, output.error()); throw std::runtime_error("FakerInput unavailable"); }
            InterlockedExchange(&s.phase, 5);
            if (any_button_down()) { InterlockedExchange(&s.error, ERROR_BUSY); throw std::runtime_error("Release mouse buttons before starting"); }
            if (s.preflight) {
                InterlockedExchange(&s.ready, 1);
                while (alive()) Sleep(5);
            } else {
            // Parent has already registered the independent Raw Input receiver.
            InterlockedExchange(&s.phase, 6);
            if (!driver->capture(s.source, true)) throw std::runtime_error("Cannot enable source filter");
            captured = ownership_started = true;
            InterlockedExchange(&s.phase, 7);
            if (any_button_down()) { InterlockedExchange(&s.error, ERROR_BUSY); throw std::runtime_error("A button changed during activation"); }
            InterlockedExchange64(&s.armed_at, stamp());
            InterlockedExchange(&s.active, 1);
            InterlockedExchange(&s.ready, 1);
            InterlockedExchange(&s.phase, 8);
            while (alive()) {
                int source_id = 0;
                mn::MouseDevicePacket packet{};
                const int status = driver->receive(source_id, packet);
                if (status < 0) throw std::runtime_error("Source read failed");
                if (!status) { driver->wait_for_input(5); continue; }
                const auto mode = static_cast<vm::Mode>(get(s.mode));
                if (source_id != s.source) throw std::runtime_error("Unexpected captured device");
                vm::Pending pending;
                if (!vm::translate({packet.state, packet.flags, packet.rolling, packet.x, packet.y}, mode, buttons, pending)) {
                    InterlockedExchange(&s.error, ERROR_NOT_SUPPORTED);
                    // This entire packet is still unconsumed. Restore it once
                    // to the original channel as capture is being abandoned.
                    driver->capture(s.source, false); captured = false;
                    output.send({});
                    driver->send(source_id, packet);
                    throw std::runtime_error("Unsupported source report; native input restored");
                }
                record(s, 1, mode, packet.x, packet.y, packet.state, packet.rolling);
                buttons = pending.buttons;
                while (!pending.empty()) {
                    if (!alive()) break;
                    const auto report = pending.next(output.axis_limit());
                    if (!output.send(report)) { InterlockedExchange(&s.error, output.error()); throw std::runtime_error("Virtual output failed"); }
                    record(s, 2, mode, report.x, report.y, report.buttons, report.wheel, report.hwheel);
                }
            }
            }
        }
    } catch (const std::exception& error) {
        strncpy_s(s.failure_reason, error.what(), _TRUNCATE);
        if (!get(s.error)) InterlockedExchange(&s.error, ERROR_GEN_FAILURE);
    }
    InterlockedExchange(&s.phase, 9);
    if (captured && !driver->capture(s.source, false)) InterlockedExchange(&s.error, ERROR_WRITE_FAULT);
    // Normal active packets were sent only to FakerInput. On shutdown, cancel
    // queued gestures and release virtual buttons; the next native edge/move
    // belongs to the physical device. Do not replay already transformed motion.
    InterlockedExchange(&s.phase, 10);
    driver->close();
    InterlockedExchange(&s.phase, 11);
    if (ownership_started) {
        if (!output.send({})) InterlockedExchange(&s.error, output.error());
        else record(s, 5, vm::Mode::Pass, 0, 0, 0, 0);
    }
    output.close();
    InterlockedExchange64(&s.released_at, stamp());
    InterlockedExchange(&s.active, 0);
    CloseHandle(parent);
    InterlockedExchange(&s.phase, 12);
    return get(s.error) ? 22 : 0;
}
struct Options {
    int source = 0, seconds = 0;
    std::wstring hardware;
    vm::Mode mode = vm::Mode::Pass;
    bool list = false, smoke = false, hang = false, acceptance = false, preflight = false, exit_pending_test = false, input_check = false;
    std::filesystem::path output;
};
struct App {
    Mapping mapping;
    HANDLE process = nullptr;
    HWND window = nullptr;
    HANDLE physical_raw = nullptr, virtual_raw = nullptr;
    std::vector<Record> received;
    std::wstring hardware, physical_path, virtual_path;
    bool read_error = false, forced = false, quit = false, truncated = false;
    bool test_surface = false;
    bool kernel_exit_pending = false;
    bool await_start = false, begin_requested = false;
    bool input_check = false;
    bool hotkey_exit = false;
    Shared& state() { return *mapping.state; }
    ~App() { if (process) CloseHandle(process); }
};
HANDLE raw_handle(const std::wstring& path) {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return nullptr;
    std::vector<RAWINPUTDEVICELIST> list(count);
    const UINT found = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (found == UINT(-1)) return nullptr;
    for (UINT i = 0; i < found; ++i) {
        UINT size = 0;
        if (list[i].dwType != RIM_TYPEMOUSE || GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, nullptr, &size) == UINT(-1)) continue;
        std::vector<wchar_t> name(size + 1);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, name.data(), &size) != UINT(-1)
                && _wcsicmp(path.c_str(), name.data()) == 0) return list[i].hDevice;
    }
    return nullptr;
}
LRESULT CALLBACK receiver(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app && app->test_surface && message == WM_PAINT) {
        PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
        RECT bounds{}; GetClientRect(window, &bounds);
        FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(30, 30, 30));
        const int mode = get(app->state().mode);
        const std::wstring stage = app->await_start ? L"准备好后按空格开始（当前没有接管鼠标）" : !get(app->state().ready) ? L"正在准备鼠标测试" : app->input_check ? L"按键和滚轮：原样转写" :
            mode == 0 ? L"1 / 3：正常移动" : mode == 1 ? L"2 / 3：光标停住（预期行为）" : L"3 / 3：反向移动";
        const std::wstring instructions = stage + (app->input_check ?
            L"\n\n先在窗口中央小幅移动几下鼠标。\n依次点按两个侧键，向上和向下各滚动几格。\n12 秒后自动恢复。\n\nCtrl + Alt + F12：立即恢复并退出。" : L"\n\n请在本窗口中央持续小幅画圈。\n"
            L"期间可以逐一点按左键、右键、中键、两个侧键，向上和向下滚动滚轮。\n"
            L"每阶段 6 秒，三阶段结束后自动恢复。\n\nCtrl + Alt + F12：立即恢复并退出。");
        bounds.left += 48; bounds.top += 64; bounds.right -= 48;
        DrawTextW(dc, instructions.c_str(), -1, &bounds, DT_LEFT | DT_WORDBREAK);
        EndPaint(window, &paint); return 0;
    }
    if (app && app->await_start && message == WM_KEYDOWN && w == VK_SPACE) {
        app->begin_requested = true; return 0;
    }
    if (app && app->test_surface && message == WM_CLOSE) {
        InterlockedExchange(&app->state().stop, 1); app->quit = true; return 0;
    }
    if (app && app->test_surface && message == WM_APPCOMMAND) return TRUE;
    if (app && message == WM_HOTKEY) {
        if (w == 12) { app->hotkey_exit = true; InterlockedExchange(&app->state().stop, 1); app->quit = true; }
        else if (w >= 6 && w <= 8) {
            InterlockedExchange(&app->state().mode, static_cast<LONG>(w - 6));
            std::cout << (w == 6 ? "Mode: passthrough\n" : w == 7 ? "Mode: zero motion\n" : "Mode: inverted motion\n");
        }
    }
    if (app && message == WM_INPUT) {
        RAWINPUT input{}; UINT size = sizeof(input);
        const auto count = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
        if (count == UINT(-1)) app->read_error = true;
        else if (input.header.dwType == RIM_TYPEMOUSE && (input.header.hDevice == app->physical_raw || input.header.hDevice == app->virtual_raw)) {
            const auto& m = input.data.mouse;
            if (app->received.size() >= kCapacity) app->truncated = true;
            else app->received.push_back({stamp(), input.header.hDevice == app->physical_raw ? 3 : 4,
                static_cast<int>(get(app->state().mode)), m.lLastX, m.lLastY, m.usButtonFlags,
                m.usButtonFlags & RI_MOUSE_WHEEL ? static_cast<short>(m.usButtonData) : 0,
                m.usButtonFlags & RI_MOUSE_HWHEEL ? static_cast<short>(m.usButtonData) : 0});
        }
    }
    if (app && message == WM_INPUT_DEVICE_CHANGE && w == GIDC_REMOVAL
            && (reinterpret_cast<HANDLE>(l) == app->physical_raw || reinterpret_cast<HANDLE>(l) == app->virtual_raw)) {
        InterlockedExchange(&app->state().error, ERROR_DEVICE_NOT_CONNECTED);
        InterlockedExchange(&app->state().stop, 1);
    }
    return DefWindowProcW(window, message, w, l);
}
std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, 0); WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.pop_back(); return result;
}
int inventory(Options& options) {
    auto driver = mn::make_interception_driver();
    if (!driver->open()) { std::cerr << "Interception driver is not connected. Finish driver installation / restart Windows.\n"; return 2; }
    const auto devices = driver->devices();
    if (!options.hardware.empty()) {
        int matches = 0, selected = 0;
        for (const auto& device : devices)
            if (_wcsicmp(device.hardware_id.c_str(), options.hardware.c_str()) == 0 && looks_physical(device.hardware_id)) {
                ++matches; selected = device.id;
            }
        if (matches != 1 || (options.source && options.source != selected))
            throw std::runtime_error("Configured physical mouse is absent, ambiguous, or conflicts with --source");
        options.source = selected;
    }
    int physical = 0;
    for (const auto& d : devices) {
        std::wcout << d.id << L"  " << d.hardware_id << (looks_physical(d.hardware_id) ? L"\n" : L"  [virtual/excluded]\n");
        if (looks_physical(d.hardware_id)) ++physical;
    }
    vm::FakerOutput output;
    const bool virtual_ready = output.open();
    std::wcout << L"FakerInput: " << (virtual_ready ? L"connected" : L"unavailable")
               << L"; physical source candidates: " << physical << L'\n';
    if (virtual_ready) std::wcout << L"Virtual mouse: " << output.raw_path() << L"; Raw Input: "
                               << (raw_handle(output.raw_path()) ? L"ready" : L"not enumerated") << L'\n';
    if (options.list) return virtual_ready && physical ? 0 : 2;
    if (!virtual_ready || !physical) {
        std::cerr << "No capture enabled. Newly installed class filters may require a Windows restart.\n"; return 2;
    }
    if (!options.source) {
        std::cout << "Select the physical mouse number above (11..20), then press Enter: " << std::flush;
        if (!(std::cin >> options.source)) return 2;
    }
    return 0;
}
int run(Options options) {
    if (!options.smoke) { const int status = inventory(options); if (status || options.list) return status; }
    App app;
    app.test_surface = options.acceptance || options.input_check;
    app.input_check = options.input_check;
    app.await_start = app.test_surface && !options.smoke && !options.preflight;
    app.received.reserve(kCapacity);
    const auto name = L"Local\\CodVirtualMouse_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    if (!app.mapping.open(name, true)) throw std::runtime_error("Cannot allocate session state");
    new (app.mapping.state) Shared{};
    auto& s = app.state(); s.source = options.source; s.parent = GetCurrentProcessId();
    s.simulate = options.smoke; s.hang = options.hang; s.preflight = options.preflight; s.mode = static_cast<LONG>(options.mode);
    if (!options.smoke) {
        auto driver = mn::make_interception_driver();
        if (!driver->open()) throw std::runtime_error("Interception context unavailable");
        const auto devices = driver->devices();
        const auto source = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.id == options.source && looks_physical(d.hardware_id); });
        if (source == devices.end()) throw std::runtime_error("Source is absent or is not an eligible physical mouse");
        app.hardware = source->hardware_id;
        wcsncpy_s(s.hardware, app.hardware.c_str(), _TRUNCATE);
        app.physical_path = vm::raw_path_for_hardware(app.hardware, true);
        vm::FakerOutput probe;
        if (!probe.open()) throw std::runtime_error("FakerInput preflight failed");
        app.virtual_path = probe.raw_path();
        app.physical_raw = raw_handle(app.physical_path); app.virtual_raw = raw_handle(app.virtual_path);
        if (!app.physical_raw || !app.virtual_raw || app.physical_raw == app.virtual_raw)
            throw std::runtime_error("Source/virtual Raw Input identities are absent or ambiguous; no capture enabled");
    }
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = receiver; cls.lpszClassName = L"CodVirtualMouseReceiver";
    if (!RegisterClassW(&cls)) throw std::runtime_error("Receiver class failed");
    app.window = CreateWindowW(cls.lpszClassName, app.test_surface ? L"鼠标转写验收" : L"",
        app.test_surface ? WS_OVERLAPPEDWINDOW : 0,
        app.test_surface ? CW_USEDEFAULT : 0, 0, app.test_surface ? 1100 : 0, app.test_surface ? 700 : 0,
        app.test_surface ? nullptr : HWND_MESSAGE, nullptr, cls.hInstance, &app);
    RAWINPUTDEVICE raw{1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, app.window};
    if (!app.window || !RegisterRawInputDevices(&raw, 1, sizeof(raw))) throw std::runtime_error("Raw receiver failed");
    for (int key : {6, 7, 8, 12})
        if (!RegisterHotKey(app.window, key, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F1 + key - 1))
            throw std::runtime_error("Required control/escape hotkey unavailable; no capture enabled");
    if (options.output.empty()) options.output = std::filesystem::path(L"runs/mouse_virtual_relay") /
        (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directories(options.output)) throw std::runtime_error("Output directory must be new");
    { std::ofstream identity(options.output / L"mapping-name.txt"); identity << utf8(name); }
    std::ofstream phases(options.output / L"worker-phases.csv");
    phases << "tick_ms,phase,ready,active,error\n" << std::flush;
    if (app.test_surface) { ShowWindow(app.window, SW_MAXIMIZE); SetForegroundWindow(app.window); UpdateWindow(app.window); }
    if (app.await_start) {
        std::cout << "WAITING: press Space in the test window to start; no capture enabled.\n" << std::flush;
        while (!app.begin_requested && !app.quit && !get(s.stop)) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            MsgWaitForMultipleObjectsEx(0, nullptr, 30, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        if (app.quit || get(s.stop)) throw std::runtime_error("Acceptance canceled before capture");
        app.await_start = false; InvalidateRect(app.window, nullptr, TRUE); UpdateWindow(app.window);
    }
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker \"" + name + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
    InterlockedExchange64(&s.parent_heartbeat, GetTickCount64());
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child)) throw std::runtime_error("Worker launch failed");
    app.process = child.hProcess; CloseHandle(child.hThread);
    { std::ofstream pid(options.output / L"worker-pid.txt"); pid << child.dwProcessId; }
    std::cout << "Controls: Ctrl+Alt+F6 passthrough / F7 block motion / F8 invert / F12 restore and exit.\n" << std::flush;
    const auto started = GetTickCount64();
    ULONGLONG stop_at = 0, exited_at = 0, ready_at = 0;
    int acceptance_stage = -1;
    LONG last_phase = -1;
    bool announced = false;
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const auto now = GetTickCount64();
        if (get(s.phase) != last_phase) {
            last_phase = get(s.phase);
            phases << now << ',' << last_phase << ',' << get(s.ready) << ',' << get(s.active) << ',' << get(s.error) << '\n' << std::flush;
        }
        InterlockedExchange64(&s.parent_heartbeat, now);
        if (!announced && get(s.ready)) { announced = true; ready_at = now; std::cout << "READY: " << (options.smoke ? "lifecycle simulation" : options.preflight ? "preflight only; no physical capture" : "relay running; physical capture requires recorded source evidence") << '\n' << std::flush; }
        if (options.acceptance && ready_at) {
            const int stage = static_cast<int>((now - ready_at) / 6000);
            if (stage >= 3) InterlockedExchange(&s.stop, 1);
            else if (stage != acceptance_stage) {
                acceptance_stage = stage; InterlockedExchange(&s.mode, stage);
                InvalidateRect(app.window, nullptr, TRUE);
                std::cout << "Acceptance stage " << stage + 1 << "/3: "
                    << (stage == 0 ? "passthrough" : stage == 1 ? "zero motion" : "inverted motion")
                    << " (6 seconds). Keep moving the selected mouse.\n" << std::flush;
            }
        }
        if (((options.smoke || options.preflight) && now - started > 800) || (options.seconds && now - started > static_cast<ULONGLONG>(options.seconds) * 1000)) InterlockedExchange(&s.stop, 1);
        if (get(s.stop) && !stop_at) stop_at = now;
        const DWORD process_wait = options.exit_pending_test ? WAIT_TIMEOUT : WaitForSingleObject(app.process, 0);
        DWORD exit_status = STILL_ACTIVE;
        const bool exit_requested = GetExitCodeProcess(app.process, &exit_status) && exit_status != STILL_ACTIVE;
        const bool resources_released = get(s.phase) == 12 && !get(s.active) && get64(s.released_at);
        if (process_wait == WAIT_OBJECT_0 || (exit_requested && resources_released)) {
            if (!exited_at) exited_at = now;
            if (now - exited_at >= 150) {
                // An exit code can become visible before kernel teardown has
                // signalled the process object. Only leave after the worker
                // explicitly closed its input/output resources. Pending kernel
                // teardown is a failed session, never successful restoration.
                if (process_wait != WAIT_OBJECT_0) {
                    app.kernel_exit_pending = true;
                    if (!get(s.error)) InterlockedExchange(&s.error, ERROR_PROCESS_ABORTED);
                }
                break; // Drain already submitted Raw Input.
            }
        } else {
            const auto heartbeat = static_cast<ULONGLONG>(get64(s.heartbeat));
            if ((heartbeat && now - heartbeat > 350) || (!heartbeat && now - started > 3000) || (stop_at && now - stop_at > 350)) {
                TerminateProcess(app.process, 91); WaitForSingleObject(app.process, 1000);
                app.forced = true;
                if (!options.smoke && get64(s.armed_at)) {
                    vm::FakerOutput cleanup;
                    if (!cleanup.open() || !cleanup.send({})) app.read_error = true;
                }
                InterlockedExchange(&s.active, 0);
                if (!get64(s.released_at)) InterlockedExchange64(&s.released_at, stamp());
            }
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    for (int key : {6, 7, 8, 12}) UnregisterHotKey(app.window, key);
    DestroyWindow(app.window);
    std::ofstream events(options.output / L"events.csv");
    events << "qpc,kind,mode,x,y,buttons,wheel,hwheel\n";
    const auto write_record = [&](const Record& r) { events << r.qpc << ',' << r.kind << ',' << r.mode << ',' << r.x << ',' << r.y << ',' << r.buttons << ',' << r.wheel << ',' << r.hwheel << '\n'; };
    for (LONG i = 0; i < get(s.written); ++i) write_record(s.records[i]);
    for (const auto& r : app.received) write_record(r);
    std::ofstream devices(options.output / L"devices.txt");
    devices << "physical_hardware=" << utf8(app.hardware) << "\nphysical_raw=" << utf8(app.physical_path)
            << "\nvirtual_raw=" << utf8(app.virtual_path) << '\n';
    DWORD child_exit = 0; GetExitCodeProcess(app.process, &child_exit);
    std::ofstream summary(options.output / L"session.json");
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    summary << "{\n  \"simulation\": " << (options.smoke ? "true" : "false")
        << ",\n  \"forced_worker_exit\": " << (app.forced ? "true" : "false")
        << ",\n  \"kernel_exit_pending\": " << (app.kernel_exit_pending ? "true" : "false")
        << ",\n  \"hotkey_exit\": " << (app.hotkey_exit ? "true" : "false")
        << ",\n  \"worker_exit\": " << child_exit << ",\n  \"error\": " << get(s.error)
        << ",\n  \"failure_reason\": " << std::quoted(s.failure_reason)
        << ",\n  \"receiver_error\": " << (app.read_error ? "true" : "false")
        << ",\n  \"diagnostics_truncated\": " << (app.truncated || get(s.diagnostics_truncated) ? "true" : "false")
        << ",\n  \"qpc_frequency\": " << frequency.QuadPart
        << ",\n  \"armed_at\": " << get64(s.armed_at) << ",\n  \"released_at\": " << get64(s.released_at)
        << ",\n  \"ready_observed\": " << (announced ? "true" : "false") << "\n}\n";
    events.flush(); devices.flush(); summary.flush();
    if (!events || !devices || !summary) throw std::runtime_error("Cannot persist session evidence");
    std::cout << "Session: " << options.output.string() << '\n';
    if (s.failure_reason[0]) std::cerr << "Capture failed: " << s.failure_reason << '\n';
    if (app.kernel_exit_pending) std::cerr << "Worker released its resources, but kernel process teardown is still pending. Session failed.\n";
    else std::cout << "Input owner stopped. Physical/game acceptance requires recorded source activity.\n";
    if (options.smoke) return announced && !app.read_error && !app.kernel_exit_pending && (options.hang ? app.forced : child_exit == 0 && !app.forced) ? 0 : 2;
    return get(s.error) || app.read_error || app.forced || !announced || child_exit ? 2 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    struct ConsoleInputMode {
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD saved = 0; bool valid = false;
        ConsoleInputMode() { valid = GetConsoleMode(input, &saved) != FALSE; if (valid) SetConsoleMode(input, (saved | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE); }
        ~ConsoleInputMode() { if (valid) SetConsoleMode(input, saved); }
    } console_mode;
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--worker") return worker(argv[2]);
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--list") options.list = true;
            else if (arg == L"--preflight-only") options.preflight = true;
            else if (arg == L"--exit-pending-test") options.smoke = options.exit_pending_test = true;
            else if (arg == L"--hardware" && i + 1 < argc) options.hardware = argv[++i];
            else if (arg == L"--acceptance") options.acceptance = true;
            else if (arg == L"--input-check") options.input_check = true;
            else if (arg == L"--smoke") options.smoke = true;
            else if (arg == L"--watchdog-test") options.smoke = options.hang = true;
            else if (arg == L"--source" && i + 1 < argc) { options.source = std::stoi(argv[++i]); if (options.source < 11 || options.source > 20) throw std::runtime_error("--source must be 11..20"); }
            else if (arg == L"--seconds" && i + 1 < argc) { options.seconds = std::stoi(argv[++i]); if (options.seconds < 1 || options.seconds > 300) throw std::runtime_error("--seconds must be 1..300"); }
            else if (arg == L"--out" && i + 1 < argc) options.output = argv[++i];
            else if (arg == L"--mode" && i + 1 < argc) {
                const std::wstring mode = argv[++i];
                if (mode == L"pass") options.mode = vm::Mode::Pass;
                else if (mode == L"block") options.mode = vm::Mode::Block;
                else if (mode == L"invert") options.mode = vm::Mode::Invert;
                else throw std::runtime_error("--mode must be pass, block or invert");
            } else throw std::runtime_error("Usage: mouse_virtual_relay [--list | --source 11..20 | --hardware ID] [--mode pass|block|invert] [--acceptance] [--preflight-only] [--seconds 1..300] [--out NEW_DIRECTORY]");
        }
        if (options.input_check) {
            if (options.acceptance || options.seconds || options.mode != vm::Mode::Pass)
                throw std::runtime_error("--input-check is a fixed 12-second passthrough test");
            options.seconds = 12;
        }
        return run(options);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
