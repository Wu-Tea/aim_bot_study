#include "shared_state.h"
#include <algorithm>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using namespace mouse_link;
namespace {
int selected_device = 0;
int selected(int device) { return device == selected_device; }
struct Context {
    InterceptionContext value = interception_create_context();
    void release() {
        if (value && selected_device) {
            interception_set_filter(value, selected, INTERCEPTION_FILTER_MOUSE_NONE);
            // Packets captured immediately before disarm still belong to us.
            // Flush them unchanged so a queued button-up cannot remain held.
            int device = 0;
            while ((device = interception_wait_with_timeout(value, 0)) != 0) {
                InterceptionStroke pending{};
                if (interception_receive(value, device, &pending, 1) != 1) break;
                if (interception_send(value, device, &pending, 1) != 1) break;
            }
        }
    }
    ~Context() { release(); if (value) interception_destroy_context(value); }
};
bool send(Context& context, Shared& state, unsigned phase, const InterceptionMouseStroke& packet) {
    const auto* stroke = reinterpret_cast<const InterceptionStroke*>(&packet);
    if (interception_send(context.value, selected_device, stroke, 1) != 1) return false;
    state.phases[phase].sent.add(packet.x, packet.y, packet.state, packet.rolling);
    return true;
}
int worker(const wchar_t* name, bool hang_test) {
    Mapping mapping;
    if (!mapping.open(name, false) || mapping.state->magic != kMagic) return 20;
    auto& state = *mapping.state;
    InterlockedExchange64(&state.worker_heartbeat, GetTickCount64());
    if (hang_test) { Sleep(INFINITE); return 0; }
    Context context;
    if (!context.value) { InterlockedExchange(&state.driver_state, -1); return 2; }
    for (int i = 0; i < 10; ++i) {
        interception_get_hardware_id(context.value, INTERCEPTION_MOUSE(i), state.hardware[i], sizeof(state.hardware[i]));
        state.hardware[i][255] = 0;
    }
    InterlockedExchange(&state.driver_state, 1);
    LONG previous_command = -1;
    bool armed = false;
    while (!read32(state.shutdown)) {
        InterlockedExchange64(&state.worker_heartbeat, GetTickCount64());
        const LONG command = read32(state.command);
        const auto mode = static_cast<Mode>(command & 0xff);
        const unsigned phase = (static_cast<unsigned>(command) >> 8) & 0xff;
        if (command != previous_command) {
            if (mode == Mode::Bypass) {
                context.release(); armed = false;
                InterlockedExchange64(&state.active_since, 0);
                InterlockedExchange(&state.worker_state, 0);
            } else {
                if (!armed) {
                    selected_device = read32(state.device);
                    if (selected_device < 11 || selected_device > 20 ||
                        state.hardware[selected_device - 11][0] == 0) {
                        InterlockedExchange(&state.error, 21); return 21;
                    }
                    // The receiver must already be alive before any filtering.
                    if (read32(state.receiver_state) != 1) {
                        InterlockedExchange(&state.error, 22); return 22;
                    }
                    interception_set_filter(context.value, selected, INTERCEPTION_FILTER_MOUSE_ALL);
                    if (interception_get_filter(context.value, selected_device) != INTERCEPTION_FILTER_MOUSE_ALL) {
                        InterlockedExchange(&state.error, 23); return 23;
                    }
                    armed = true;
                    InterlockedExchange64(&state.active_since, GetTickCount64());
                }
                InterlockedExchange(&state.phases[phase].mode, static_cast<LONG>(mode));
                InterlockedExchange64(&state.phases[phase].unmarked_baseline, read64(state.unmarked_motion));
                InterlockedExchange(&state.worker_state, 1);
                if (mode == Mode::Pulse) {
                    InterceptionMouseStroke pulse{};
                    pulse.x = kPulseCounts;
                    pulse.flags = INTERCEPTION_MOUSE_MOVE_NOCOALESCE;
                    pulse.information = state.marker | phase;
                    if (!send(context, state, phase, pulse)) {
                        InterlockedExchange(&state.error, 24); return 24;
                    }
                }
            }
            previous_command = command;
            InterlockedExchange(&state.acknowledged, command);
        }
        if (!armed) { Sleep(8); continue; }
        const int device = interception_wait_with_timeout(context.value, 8);
        if (device == 0) continue;
        InterceptionMouseStroke source{}, replacement{};
        if (device != selected_device || interception_receive(context.value, device,
                reinterpret_cast<InterceptionStroke*>(&source), 1) != 1) {
            InterlockedExchange(&state.error, 25); return 25;
        }
        // Driver-level send must not loop back as another physical packet.
        if ((source.information & 0xffffff00u) == state.marker) {
            InterlockedExchange(&state.error, 26); return 26;
        }
        state.phases[phase].source.add(source.x, source.y, source.state, source.rolling);
        if (!transform(source, mode, state.marker | phase, replacement)) {
            // Unsupported input is delivered once, then the filter is released.
            interception_send(context.value, device, reinterpret_cast<const InterceptionStroke*>(&source), 1);
            InterlockedExchange(&state.error, 27); return 27;
        }
        if (!send(context, state, phase, replacement)) {
            InterlockedExchange(&state.error, 24); return 24;
        }
    }
    context.release();
    InterlockedExchange(&state.worker_state, 0);
    return 0;
}

LRESULT CALLBACK receiver_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* state = reinterpret_cast<Shared*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<Shared*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state && message == WM_INPUT) {
        RAWINPUT input{}; UINT bytes = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &input, &bytes,
                sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
            InterlockedExchange(&state->receiver_state, -1);
        } else if (input.header.dwType == RIM_TYPEMOUSE) {
            const auto& mouse = input.data.mouse;
            if ((mouse.ulExtraInformation & 0xffffff00u) == state->marker) {
                const unsigned phase = mouse.ulExtraInformation & 0xff;
                state->phases[phase].received.add(mouse.lLastX, mouse.lLastY,
                    mouse.usButtonFlags, static_cast<short>(mouse.usButtonData));
                InterlockedIncrement64(&state->tagged_packets);
            } else if (mouse.lLastX || mouse.lLastY) {
                InterlockedIncrement64(&state->unmarked_motion);
            }
        }
        // DefWindowProc performs foreground WM_INPUT cleanup when applicable.
        return DefWindowProcW(window, message, w, l);
    }
    if (state && message == WM_TIMER) {
        InterlockedExchange64(&state->receiver_heartbeat, GetTickCount64());
        if (read32(state->receiver_shutdown)) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
int receiver(const wchar_t* name) {
    Mapping mapping;
    if (!mapping.open(name, false) || mapping.state->magic != kMagic) return 30;
    auto& state = *mapping.state;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"CodMouseLinkIndependentReceiver"; cls.lpfnWndProc = receiver_proc;
    if (!RegisterClassW(&cls)) return 31;
    HWND window = CreateWindowW(cls.lpszClassName, L"", 0, 0,0,0,0,
        HWND_MESSAGE, nullptr, cls.hInstance, &state);
    if (!window) return 32;
    RAWINPUTDEVICE device{0x01, 0x02, RIDEV_INPUTSINK, window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device)) || !SetTimer(window, 1, 20, nullptr)) {
        InterlockedExchange(&state.receiver_state, -1); return 33;
    }
    InterlockedExchange(&state.receiver_state, 1);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    InterlockedExchange(&state.receiver_state, 0);
    DestroyWindow(window);
    return 0;
}

struct App {
    Mapping mapping;
    HANDLE job = nullptr, worker_process = nullptr, receiver_process = nullptr;
    HWND window = nullptr, combo = nullptr;
    HFONT font = nullptr;
    std::wstring session, executable, report;
    std::vector<int> devices;
    unsigned phase = 0;
    ULONGLONG started = GetTickCount64();
    bool populated = false, stopping = false, worker_killed = false, receiver_seen = false;
    bool smoke = false, watchdog_test = false;
    std::wstring status = L"启动中，尚未拦截任何输入。";
    Shared& state() { return *mapping.state; }
    HANDLE spawn(const std::wstring& role) {
        std::wstring command = L"\"" + executable + L"\" " + role + L" \"" + session + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return nullptr;
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            TerminateProcess(process.hProcess, 40);
            CloseHandle(process.hThread); CloseHandle(process.hProcess); return nullptr;
        }
        ResumeThread(process.hThread); CloseHandle(process.hThread);
        return process.hProcess;
    }
    void command(Mode mode) {
        if (stopping || worker_killed || read32(state().driver_state) != 1) return;
        if (read32(state().receiver_state) != 1) { status = L"独立接收端未就绪，不能启用拦截。"; return; }
        if (mode != Mode::Bypass) {
            const LRESULT choice = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (choice < 0 || static_cast<size_t>(choice) >= devices.size()) return;
            if (read32(state().worker_state) != 1)
                InterlockedExchange(&state().device, devices[static_cast<size_t>(choice)]);
            if (phase + 1 >= kMaxPhases) { status = L"本次记录已满，请重新启动工具。"; return; }
            ++phase;
        }
        EnableWindow(combo, mode == Mode::Bypass);
        InterlockedExchange(&state().command, static_cast<LONG>((phase << 8) | static_cast<unsigned>(mode)));
        status = mode == Mode::Bypass ? L"请求恢复原生输入；请移动鼠标检查恢复。" : L"已请求切换模式；只移动所选鼠标，观察下方两端计数。";
    }
    void tick() {
        const auto now = GetTickCount64();
        receiver_seen = receiver_seen || read32(state().receiver_state) == 1;
        if (!populated && read32(state().driver_state) != 0) {
            populated = true;
            if (read32(state().driver_state) < 0) {
                status = L"Interception 驱动不可用（或被其他程序独占）。未启用拦截。";
            } else {
                for (int i = 0; i < 10; ++i) if (state().hardware[i][0]) {
                    devices.push_back(INTERCEPTION_MOUSE(i));
                    std::wstring label = L"设备 " + std::to_wstring(INTERCEPTION_MOUSE(i)) + L"  " + state().hardware[i];
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                }
                SendMessageW(combo, CB_SETCURSEL, 0, 0);
                status = devices.empty() ? L"驱动已连接，但未找到鼠标设备。" : L"请选择鼠标，再按“原样转发”。当前仍是原生输入。";
            }
        }
        if (worker_process && WaitForSingleObject(worker_process, 0) == WAIT_TIMEOUT) {
            const auto heartbeat = static_cast<ULONGLONG>(read64(state().worker_heartbeat));
            const auto receiver_tick = static_cast<ULONGLONG>(read64(state().receiver_heartbeat));
            const bool stuck = heartbeat ? now - heartbeat > 350 : now - started > 3000;
            const bool observer_lost = read32(state().worker_state) == 1 &&
                (!receiver_tick || now - receiver_tick > 350 || read32(state().receiver_state) != 1);
            if (stuck || observer_lost || (stopping && now - stop_at > 350)) {
                TerminateProcess(worker_process, 41); worker_killed = true;
                status = L"工作进程/接收端异常，已终止输入所有者；请确认原生鼠标恢复。";
            }
        }
        if (read32(state().error)) {
            status = L"输入链故障，已退出工作进程。错误码 " + std::to_wstring(read32(state().error));
        }
        const auto active_at = static_cast<ULONGLONG>(read64(state().active_since));
        if (!stopping && active_at && now - active_at >= 60'000 &&
            (read32(state().command) & 0xff) != 0) {
            command(Mode::Bypass); status = L"本次接管达到 60 秒，自动恢复原生输入。";
        }
        if (stopping && (!worker_process || WaitForSingleObject(worker_process, 0) != WAIT_TIMEOUT)) {
            InterlockedExchange(&state().receiver_shutdown, 1);
            if (!receiver_process || WaitForSingleObject(receiver_process, 0) != WAIT_TIMEOUT || now - stop_at > 1000)
                DestroyWindow(window);
        }
        else if (smoke && now - started > 1800) stop();
        if (window && IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
    }
    ULONGLONG stop_at = 0;
    void stop() {
        if (stopping) return;
        stopping = true; stop_at = GetTickCount64();
        InterlockedExchange(&state().shutdown, 1);
        status = L"正在释放拦截并退出。";
    }
    void save() {
        // Disk I/O is after interception has ended, never in the receive loop.
        std::filesystem::create_directories(std::filesystem::path(report).parent_path());
        std::ofstream out{std::filesystem::path(report)};
        out << "phase,mode,source_packets,source_x,source_y,source_abs_x,source_abs_y,"
               "sent_packets,sent_x,sent_y,sent_abs_x,sent_abs_y,received_packets,received_x,received_y,received_abs_x,received_abs_y\n";
        for (unsigned i = 1; i <= phase; ++i) {
            auto& p = state().phases[i];
            out << i << ',' << read32(p.mode);
            for (auto* counter : {&p.source, &p.sent, &p.received}) {
                const auto t = counter->snapshot();
                out << ',' << t.packets << ',' << t.x << ',' << t.y << ',' << t.abs_x << ',' << t.abs_y;
            }
            out << '\n';
        }
    }
    ~App() {
        if (mapping.state) { InterlockedExchange(&state().shutdown, 1); InterlockedExchange(&state().receiver_shutdown, 1); }
        // Job ownership also closes all child handles if this UI crashes.
        if (job) CloseHandle(job);
        if (worker_process) CloseHandle(worker_process);
        if (receiver_process) CloseHandle(receiver_process);
        if (font) DeleteObject(font);
    }
};
const wchar_t* mode_label(unsigned mode) {
    switch (static_cast<Mode>(mode)) {
        case Mode::Relay: return L"原样转发"; case Mode::Block: return L"屏蔽位移";
        case Mode::Invert: return L"反向位移"; case Mode::Pulse: return L"定量 +80（物理位移屏蔽）";
        default: return L"原生输入";
    }
}
void text_line(HDC dc, int x, int y, const std::wstring& value) {
    TextOutW(dc, x, y, value.c_str(), static_cast<int>(value.size()));
}
LRESULT CALLBACK ui_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, w, l);
    if (message == WM_COMMAND || message == WM_HOTKEY) {
        const unsigned id = message == WM_COMMAND ? LOWORD(w) : static_cast<unsigned>(w);
        if (id >= 101 && id <= 104) app->command(static_cast<Mode>(id - 100));
        if (id == 105) app->command(Mode::Bypass);
        if (id == 106) app->stop();
        return 0;
    }
    if (message == WM_TIMER) { app->tick(); return 0; }
    if (message == WM_CLOSE) { app->stop(); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect); FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        const auto previous_font = SelectObject(dc, app->font); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(28,35,44));
        text_line(dc, 24, 20, L"Mouse Link · 真实鼠标输入链工具");
        text_line(dc, 24, 55, L"先选设备 → 原样转发 → 屏蔽 → 反向；最后恢复原生输入。");
        text_line(dc, 24, 187, L"热键均为 Ctrl+Alt：F6 转发 / F7 屏蔽 / F8 反向 / F9 +80 / F10 恢复 / F12 退出");
        text_line(dc, 24, 222, app->status);
        auto& state = app->state();
        const auto acknowledged = static_cast<unsigned>(read32(state.acknowledged));
        text_line(dc, 24, 267, std::wstring(L"已生效模式：") + mode_label(acknowledged & 0xff));
        const unsigned phase = acknowledged >> 8;
        auto& p = state.phases[phase & 0xff];
        const auto src = p.source.snapshot(), sent = p.sent.snapshot(), raw = p.received.snapshot();
        const auto row = [](const wchar_t* name, const Totals& t) {
            return std::wstring(name) + L"  包数=" + std::to_wstring(t.packets) +
                L"    X=" + std::to_wstring(t.x) + L"    Y=" + std::to_wstring(t.y) +
                L"    |X|=" + std::to_wstring(t.abs_x) + L"    |Y|=" + std::to_wstring(t.abs_y);
        };
        text_line(dc, 24, 302, row(L"控制端收到的原包", src));
        text_line(dc, 24, 337, row(L"提交给驱动的替代", sent));
        text_line(dc, 24, 372, row(L"独立 Raw 接收端", raw));
        const auto unmarked = read64(state.unmarked_motion) - read64(p.unmarked_baseline);
        text_line(dc, 24, 407, L"本阶段未标记移动包（来源未确认）：" + std::to_wstring(unmarked));
        const auto proof = evaluate(static_cast<Mode>(acknowledged & 0xff), src, sent, raw, unmarked,
            read32(state.receiver_state) == 1, read64(state.tagged_packets) != 0);
        const wchar_t* evidence = L"等待真实鼠标输入；没有输入不能判定通过。";
        if (proof == Evidence::WaitingForReceiver) evidence = L"等待独立接收端看到带标记的替代包。";
        if (proof == Evidence::Mismatch) evidence = L"当前计数未匹配：停止移动后观察；持续不匹配则未通过。";
        if (proof == Evidence::Matched) evidence = L"当前累计位移匹配；仍需检查按钮、滚轮和退出恢复。";
        text_line(dc, 24, 452, evidence);
        text_line(dc, 24, 487, L"屏蔽/反向只改 X/Y；按钮与滚轮按原包转发。每次接管最多 60 秒。");
        text_line(dc, 24, 522, L"本工具不启动 Vision/AI。此窗口的桌面链路结果不代表目标游戏已验收。");
        SelectObject(dc, previous_font); EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int ui(bool smoke, bool watchdog_test) {
    App app; app.smoke = smoke; app.watchdog_test = watchdog_test;
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH); app.executable = exe;
    app.session = L"Local\\CodMouseLink_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    app.report = L"artifacts/mouse_link/sessions/" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".csv";
    if (!app.mapping.open(app.session.c_str(), true)) return 50;
    new (app.mapping.state) Shared{};
    app.state().marker = (static_cast<unsigned>(GetTickCount64()) ^ GetCurrentProcessId() ^ 0x4c4e4b00u) & 0xffffff00u;
    if (app.state().marker == 0) app.state().marker = 0x4c4e4b00u;
    app.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!app.job || !SetInformationJobObject(app.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 51;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = ui_proc;
    cls.lpszClassName = L"CodMouseLinkTool"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls)) return 52;
    HWND window = CreateWindowW(cls.lpszClassName, L"Mouse Link — 鼠标链路工具", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 610, nullptr, nullptr, cls.hInstance, &app);
    if (!window) return 53;
    app.font = CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    app.combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        24, 90, 855, 180, window, nullptr, cls.hInstance, nullptr);
    SendMessageW(app.combo, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    const wchar_t* labels[]{L"原样转发 F6", L"屏蔽位移 F7", L"反向位移 F8", L"定量 +80 F9", L"恢复原生 F10", L"释放并退出 F12"};
    const UINT keys[]{VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F12};
    for (int i = 0; i < 6; ++i) {
        HWND button = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            24 + i * 145, 137, 137, 35, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(101 + i)), cls.hInstance, nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
        if (!RegisterHotKey(window, 101 + i, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, keys[i])) {
            std::cerr << "Required emergency/control hotkey unavailable; no worker started.\n"; return 54;
        }
    }
    if (!SetTimer(window, 1, 50, nullptr)) return 55;
    app.receiver_process = app.spawn(L"--receiver");
    app.worker_process = app.spawn(watchdog_test ? L"--test-hang" : L"--worker");
    if (!app.receiver_process || !app.worker_process) return 56;
    ShowWindow(window, smoke ? SW_HIDE : SW_SHOW);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    for (int i = 0; i < 6; ++i) UnregisterHotKey(window, 101 + i);
    app.save();
    if (smoke) {
        if (!app.receiver_seen) return 59;
        if (watchdog_test && !app.worker_killed) return 57;
        if (!watchdog_test && app.worker_killed) return 58;
        std::cout << (watchdog_test ? "PASS: independent watchdog terminated stalled worker; no input capture.\n"
                                   : "PASS: UI/receiver/worker lifecycle; no input capture.\n");
    }
    return 0;
}
int inventory() {
    Context context;
    if (!context.value) {
        std::cerr << "UNAVAILABLE: Interception context cannot open. Driver missing, not loaded, or in use. No filters enabled.\n";
        return 2;
    }
    unsigned count = 0;
    for (int i = 0; i < 10; ++i) {
        wchar_t id[256]{};
        if (interception_get_hardware_id(context.value, INTERCEPTION_MOUSE(i), id, sizeof(id))) {
            id[255] = 0; std::wcout << INTERCEPTION_MOUSE(i) << L": " << id << L'\n'; ++count;
        }
    }
    std::cout << "No filters enabled. Present mouse interfaces: " << count << '\n';
    return count ? 0 : 2;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--worker") return worker(argv[2], false);
        if (argc == 3 && std::wstring(argv[1]) == L"--test-hang") return worker(argv[2], true);
        if (argc == 3 && std::wstring(argv[1]) == L"--receiver") return receiver(argv[2]);
        if (argc == 2 && std::wstring(argv[1]) == L"--list") return inventory();
        if (argc == 2 && std::wstring(argv[1]) == L"--smoke") return ui(true, false);
        if (argc == 2 && std::wstring(argv[1]) == L"--watchdog-test") return ui(true, true);
        if (argc == 1) return ui(false, false);
        std::cerr << "Usage: mouse_link_tool.exe [--list | --smoke | --watchdog-test]\n";
        return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
