#include "desktop_session.h"
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
enum Action { Read = 101, Validate, Save, Start, Stop, Reload, Logs };
struct Window {
    desktop_native::DesktopSession session;
    HWND editor = nullptr, game = nullptr, status = nullptr;
    std::future<void> operation;
    bool interactive;
    explicit Window(const std::filesystem::path& root, bool interactive = true)
        : session(root), interactive(interactive) {}
};
std::wstring window_text(HWND window) {
    std::wstring text(GetWindowTextLengthW(window) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), static_cast<int>(text.size())));
    return text;
}
std::string selected_game(const Window& window) { return desktop_native::to_utf8(window_text(window.game)); }
void status(HWND window, const std::wstring& value) { SetWindowTextW(window, value.c_str()); }
void refresh(Window& state) {
    if (!state.session.running()) { status(state.status, L"原生程序未运行。编辑后校验、保存，再启动。"); return; }
    const auto snapshot = state.session.snapshot();
    std::wostringstream text;
    text << L"运行中 | revision " << snapshot.revision << L" | ";
    switch (snapshot.status) {
    case runtime_app::Pending: text << L"等待 tick / 新视觉策略帧"; break;
    case runtime_app::Applied: text << L"已生效"; break;
    case runtime_app::RestartRequired: text << L"需要停止后重新启动"; break;
    case runtime_app::Rejected: text << L"配置被拒绝"; break;
    default: text << L"就绪"; break;
    }
    text << L"\r\n" << desktop_native::to_wide(snapshot.message);
    text << L"\r\n学习响应：";
    for (const auto& region : snapshot.regions)
        text << region.effective << L" (" << region.samples << L")  ";
    status(state.status, text.str());
}
void resize(Window& state, int width, int height) {
    MoveWindow(state.editor, 16, 96, width - 32, height - 216, TRUE);
    MoveWindow(state.status, 16, height - 112, width - 32, 100, TRUE);
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_CREATE) {
            state = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            CreateWindowW(L"STATIC", L"原生 C++ 助手 · 编辑 TOML，校验后保存；运行中保存后点击热更新。",
                WS_CHILD | WS_VISIBLE, 16, 12, 840, 24, window, nullptr, nullptr, nullptr);
            state->game = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                16, 44, 128, 180, window, nullptr, nullptr, nullptr);
            for (const auto* game : {L"default", L"apex", L"bo3"}) SendMessageW(state->game, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(game));
            SetWindowTextW(state->game, L"default");
            const wchar_t* labels[] = {L"重新读取", L"校验", L"保存", L"启动", L"停止", L"热更新", L"日志"};
            for (int index = 0; index < 7; ++index)
                CreateWindowW(L"BUTTON", labels[index], WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    156 + index * 94, 44, 86, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Read + index)), nullptr, nullptr);
            state->editor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                16, 96, 820, 460, window, nullptr, nullptr, nullptr);
            SendMessageW(state->editor, EM_SETLIMITTEXT, 1024 * 1024, 0);
            SendMessageW(state->editor, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
            state->status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 564, 820, 100, window, nullptr, nullptr, nullptr);
            SetWindowTextW(state->editor, desktop_native::to_wide(state->session.read_config()).c_str());
            SetTimer(window, 1, 500, nullptr);
            return 0;
        }
        if (!state) return DefWindowProcW(window, message, wparam, lparam);
        if (message == WM_SIZE) { resize(*state, LOWORD(lparam), HIWORD(lparam)); return 0; }
        if (message == WM_TIMER) {
            if (state->operation.valid()) {
                if (state->operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    status(state->status, L"正在处理启动、停止或热更新请求…"); return 0;
                }
                try { state->operation.get(); }
                catch (const std::exception& error) {
                    MessageBoxW(window, desktop_native::to_wide(error.what()).c_str(),
                        L"运行请求失败", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            refresh(*state); return 0;
        }
        if (message == WM_COMMAND && LOWORD(wparam) >= Read && LOWORD(wparam) <= Logs) {
            const auto action = LOWORD(wparam);
            if (action == Read) {
                if (SendMessageW(state->editor, EM_GETMODIFY, 0, 0) && MessageBoxW(window,
                    L"重新读取会放弃窗口中未保存的编辑。是否继续？", L"重新读取", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                SetWindowTextW(state->editor, desktop_native::to_wide(state->session.read_config()).c_str());
                SendMessageW(state->editor, EM_SETMODIFY, FALSE, 0);
            } else if (action == Validate || action == Save) {
                const auto text = desktop_native::to_utf8(window_text(state->editor));
                if (action == Validate) state->session.validate_config(text, selected_game(*state));
                else { state->session.save_config(text, selected_game(*state)); SendMessageW(state->editor, EM_SETMODIFY, FALSE, 0); }
                MessageBoxW(window, action == Save ? L"配置已保存。运行中请点击热更新；需重启的参数会明确提示。" : L"配置校验通过。",
                    L"配置", MB_OK | MB_ICONINFORMATION);
            } else if (action == Start || action == Stop || action == Reload) {
                if (state->operation.valid()) throw std::runtime_error("previous operation is still in progress");
                state->operation = std::async(std::launch::async,
                    [root = state->session.root(), game = selected_game(*state), action] {
                        desktop_native::DesktopSession session(root);
                        if (action == Start) session.start(game);
                        else if (action == Stop) session.stop();
                        else session.request_reload();
                    });
            }
            else if (action == Logs) ShellExecuteW(window, L"open", (state->session.root() / "runs/desktop/native-runtime.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (message == WM_CLOSE) {
            if (SendMessageW(state->editor, EM_GETMODIFY, 0, 0) && MessageBoxW(window,
                L"配置尚未保存。是否放弃编辑并关闭？运行中的原生程序会继续运行。", L"关闭助手", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            DestroyWindow(window); return 0;
        }
        if (message == WM_DESTROY) { KillTimer(window, 1); PostQuitMessage(0); return 0; }
    } catch (const std::exception& error) {
        const auto text = desktop_native::to_wide(error.what());
        if (message == WM_TIMER && state) status(state->status, text);
        else if (!state || state->interactive) MessageBoxW(window, text.c_str(), L"原生助手", MB_OK | MB_ICONERROR);
        else std::cerr << error.what() << '\n';
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<wchar_t> executable(32768);
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        auto root = std::filesystem::path(executable.data()).parent_path().parent_path().parent_path().parent_path();
        std::wstring action = L"gui";
        std::string game = "default";
        for (int index = 1; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (index + 1 >= argc) throw std::runtime_error("missing command argument");
            if (option == L"--project") root = argv[++index];
            else if (option == L"--action") action = argv[++index];
            else if (option == L"--game") game = desktop_native::to_utf8(argv[++index]);
            else throw std::runtime_error("unknown assistant option");
        }
        desktop_native::DesktopSession session(root);
        if (action == L"preview-start") std::wcout << session.start_command(game) << L'\n';
        else if (action == L"preview-stop") std::cout << "stop owned native runtime through named event\n";
        else if (action == L"start") session.start(game);
        else if (action == L"stop") session.stop();
        else if (action == L"reload") session.request_reload();
        else if (action == L"runtime-info") std::cout << session.runtime_info() << '\n';
        else if (action == L"check-config") session.validate_config(session.read_config(), game);
        else if (action == L"status") {
            if (!session.running()) { std::cout << "stopped\n"; return 0; }
            const auto value = session.snapshot();
            std::cout << "status=" << value.status << " revision=" << value.revision << " " << value.message << '\n';
        } else if (action == L"gui" || action == L"ui-check") {
            if (action == L"gui") FreeConsole();
            Window state(root, action == L"gui");
            WNDCLASSW type{}; type.lpfnWndProc = procedure; type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"CodNativeAssistant"; type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (!RegisterClassW(&type)) throw std::runtime_error("cannot register assistant window");
            auto window = CreateWindowW(type.lpszClassName, L"原生 C++ 助手", WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT, 880, 720, nullptr, nullptr, type.hInstance, &state);
            if (!window) throw std::runtime_error("cannot create assistant window");
            if (action == L"ui-check") {
                if (!state.editor || !state.game || !state.status || window_text(state.editor).empty())
                    throw std::runtime_error("assistant configuration controls are incomplete");
                DestroyWindow(window);
                std::cout << "native assistant window/config controls: PASS\n";
                return 0;
            }
            ShowWindow(window, SW_SHOWNORMAL);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
        } else throw std::runtime_error("unknown assistant action");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[NativeAssistant] " << error.what() << '\n';
        return 1;
    }
}
