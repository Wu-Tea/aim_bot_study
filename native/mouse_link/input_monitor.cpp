#include "input_provenance.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mouse_link;
struct Event {
    LONGLONG qpc;
    bool raw;
    std::uintptr_t device, tag;
    unsigned message, flags, buttons, data, os_time;
    LONG x, y;
};
struct Monitor {
    static constexpr std::size_t capacity = 262144;
    std::vector<Event> events;
    unsigned dropped = 0, read_errors = 0;
    Monitor() { events.reserve(capacity); }
    void append(Event event) noexcept {
        if (events.size() == capacity) { ++dropped; return; }
        events.push_back(event); // Preallocated, no disk I/O or device queries in hook.
    }
} *active = nullptr;
LONGLONG timestamp() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
LRESULT CALLBACK hook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && active) {
        const auto& m = *reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        active->append({timestamp(), false, 0, m.dwExtraInfo, static_cast<unsigned>(w),
            m.flags, 0, m.mouseData, m.time, m.pt.x, m.pt.y});
    }
    return CallNextHookEx(nullptr, code, w, l); // Observer never suppresses an input event.
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INPUT && active) {
        RAWINPUT input{}; UINT size = sizeof(input);
        const UINT result = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT,
            &input, &size, sizeof(RAWINPUTHEADER));
        if (result == static_cast<UINT>(-1) || result < sizeof(RAWINPUTHEADER)) ++active->read_errors;
        else if (input.header.dwType == RIM_TYPEMOUSE) {
            if (result < offsetof(RAWINPUT, data) + sizeof(RAWMOUSE)) ++active->read_errors;
            else {
                const auto& m = input.data.mouse;
                active->append({timestamp(), true, reinterpret_cast<std::uintptr_t>(input.header.hDevice),
                    m.ulExtraInformation, message, m.usFlags, m.usButtonFlags, m.usButtonData,
                    static_cast<unsigned>(GetMessageTime()), m.lLastX, m.lLastY});
            }
        }
    }
    return DefWindowProcW(window, message, w, l);
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
void pump_until(ULONGLONG deadline) {
    while (GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
unsigned send_probe(std::uintptr_t tag) {
    std::array<INPUT, 20> inputs{};
    for (unsigned i = 0; i < inputs.size(); ++i) {
        inputs[i].type = INPUT_MOUSE;
        inputs[i].mi.dx = i % 2 ? -1 : 1;
        inputs[i].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
        inputs[i].mi.dwExtraInfo = tag;
    }
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    unsigned seconds = 15; bool self_test = false, no_legacy = false;
    std::uintptr_t extra_tag = 0;
    std::filesystem::path directory;
    HHOOK observer = nullptr; HWND window = nullptr;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument == L"--self-test") self_test = true;
            else if (argument == L"--no-legacy") no_legacy = true;
            else if (argument == L"--seconds" && i + 1 < argc) seconds = std::stoul(argv[++i]);
            else if (argument == L"--tag" && i + 1 < argc) extra_tag = std::stoull(argv[++i], nullptr, 0);
            else if (argument == L"--out" && i + 1 < argc) directory = argv[++i];
            else throw std::runtime_error("Usage: mouse_input_monitor [--seconds 1..60] [--out NEW_DIRECTORY] [--tag 0xHEX] [--self-test] [--no-legacy]");
        }
        if (seconds < 1 || seconds > 60) throw std::runtime_error("seconds must be 1..60");
        if (directory.empty()) directory = std::filesystem::path(L"runs/mouse_input_monitor") /
            (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        // Each run owns a new directory; never overwrite earlier measurements.
        if (!std::filesystem::create_directories(directory)) throw std::runtime_error("output directory already exists");
        Monitor monitor; active = &monitor;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        const auto start = timestamp();
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"MouseInputEvidenceMonitor"; cls.lpfnWndProc = window_proc;
        if (!RegisterClassW(&cls)) throw std::runtime_error("window class registration failed");
        window = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("listener window creation failed");
        // NOLEGACY affects this listener's legacy messages, not another process.
        RAWINPUTDEVICE raw{1, 2, RIDEV_INPUTSINK | (no_legacy ? RIDEV_NOLEGACY : 0u), window};
        if (!RegisterRawInputDevices(&raw, 1, sizeof(raw))) throw std::runtime_error("Raw Input registration failed");
        observer = SetWindowsHookExW(WH_MOUSE_LL, hook, cls.hInstance, 0);
        if (!observer) throw std::runtime_error("low-level observer registration failed");
        // A flushed line is the experiment runner's readiness handshake. It is
        // emitted only after both registrations succeed, never after a delay.
        std::cout << "Listening: LL injection flags + independent Raw Input devices. No capture/filter." << std::endl;
        unsigned sent_tagged = 0, sent_untagged = 0;
        if (self_test) {
            std::cout << "Self-test: 20 tagged + 20 untagged +/-1-count moves, no clicks.\n";
            sent_tagged = send_probe(kProbeTag);
            pump_until(GetTickCount64() + 100);
            sent_untagged = send_probe(0);
        }
        pump_until(GetTickCount64() + (self_test ? 300 : seconds * 1000));
        UnhookWindowsHookEx(observer); observer = nullptr;
        const double duration_ms = (timestamp() - start) * 1000.0 / frequency.QuadPart;
        active = nullptr;
        DestroyWindow(window); window = nullptr;

        std::ofstream events(directory / "events.csv");
        events << "t_us,stream,origin_evidence,tag_match,lower_integrity,device_handle,extra_info,message,flags,buttons,data,os_time_ms,x,y,coordinate_kind\n";
        std::map<std::uintptr_t, std::uint64_t> devices;
        // Include idle devices so a passive run still provides a device map.
        // Enumeration identifies Windows devices, not physical authenticity.
        UINT device_count = 0;
        if (GetRawInputDeviceList(nullptr, &device_count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
            throw std::runtime_error("Raw Input device enumeration failed");
        std::vector<RAWINPUTDEVICELIST> device_list(device_count);
        if (device_count != 0) {
            const UINT found = GetRawInputDeviceList(device_list.data(), &device_count, sizeof(RAWINPUTDEVICELIST));
            if (found == static_cast<UINT>(-1)) throw std::runtime_error("Raw Input device enumeration changed/failed; rerun measurement");
            for (UINT i = 0; i < found; ++i)
                if (device_list[i].dwType == RIM_TYPEMOUSE)
                    devices.emplace(reinterpret_cast<std::uintptr_t>(device_list[i].hDevice), 0);
        }
        std::map<std::string, std::uint64_t> groups;
        unsigned tagged_injected = 0, untagged_injected = 0;
        for (const auto& e : monitor.events) {
            const auto evidence = e.raw ? raw_evidence(e.device != 0, e.tag, extra_tag) : hook_evidence(e.flags, e.tag, extra_tag);
            const char* stream = e.raw ? "raw" : "ll";
            ++groups[std::string(stream) + "." + evidence.origin + (evidence.tag_match ? ".tag_match" : ".no_tag")];
            if (e.raw && e.device) ++devices[e.device];
            if (!e.raw && e.message == WM_MOUSEMOVE && (e.flags & LLMHF_INJECTED)) {
                if (e.tag == kProbeTag) ++tagged_injected;
                if (e.tag == 0) ++untagged_injected;
            }
            events << (e.qpc - start) * 1000000.0 / frequency.QuadPart << ',' << stream << ',' << evidence.origin
                << ',' << evidence.tag_match << ',' << evidence.lower_integrity << ',' << e.device << ',' << e.tag
                << ',' << e.message << ',' << e.flags << ',' << e.buttons << ',' << e.data << ',' << e.os_time
                << ',' << e.x << ',' << e.y << ',' << (e.raw ? ((e.flags & MOUSE_MOVE_ABSOLUTE) ? "raw_absolute" : "relative_counts") : "screen_position") << '\n';
        }
        std::ofstream device_file(directory / "devices.csv");
        device_file << "device_handle,received_packets,name_query_succeeded,device_path\n";
        for (const auto& [handle, count] : devices) {
            UINT size = 0;
            const auto device = reinterpret_cast<HANDLE>(handle);
            bool ok = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size) != static_cast<UINT>(-1) && size > 0;
            std::wstring name;
            if (ok) {
                name.resize(size + 1);
                ok = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &size) != static_cast<UINT>(-1);
                name.resize(ok ? wcslen(name.c_str()) : 0);
            }
            device_file << handle << ',' << count << ',' << ok << ',' << quoted(utf8(name)) << '\n';
        }
        const bool passed = sent_tagged == 20 && sent_untagged == 20 && tagged_injected == 20 &&
            untagged_injected >= 20 && monitor.dropped == 0 && monitor.read_errors == 0;
        std::ofstream summary(directory / "summary.json");
        summary << "{\n  \"self_test\": " << (self_test ? "true" : "false")
            << ",\n  \"no_legacy\": " << (no_legacy ? "true" : "false")
            << ",\n  \"self_test_passed\": " << (self_test ? (passed ? "true" : "false") : "null")
            << ",\n  \"duration_ms\": " << duration_ms << ",\n  \"events\": " << monitor.events.size()
            << ",\n  \"dropped\": " << monitor.dropped << ",\n  \"read_errors\": " << monitor.read_errors
            << ",\n  \"sent_tagged\": " << sent_tagged << ",\n  \"sent_untagged\": " << sent_untagged
            << ",\n  \"observed_tagged_injected_moves\": " << tagged_injected
            << ",\n  \"observed_untagged_injected_moves\": " << untagged_injected << ",\n  \"groups\": {";
        bool first = true;
        for (const auto& [group, count] : groups) {
            summary << (first ? "\n" : ",\n") << "    \"" << group << "\": " << count; first = false;
            std::cout << group << '=' << count << '\n';
        }
        summary << "\n  },\n  \"physical_origin_verified\": false,\n  \"game_receipt_verified\": false\n}\n";
        events.flush(); device_file.flush(); summary.flush();
        if (!events || !device_file || !summary) throw std::runtime_error("report write failed");
        std::cout << "Report: " << directory.string() << "\nDropped=" << monitor.dropped << " read_errors=" << monitor.read_errors << '\n';
        std::cout << "Windows mouse devices=" << devices.size() << "; handle/name is not proof of physical origin.\n";
        if (self_test) std::cout << (passed ? "PASS" : "FAIL") << " SendInput observed classification; physical/driver/game origin not validated.\n";
        return monitor.dropped || monitor.read_errors || (self_test && !passed) ? 2 : 0;
    } catch (const std::exception& error) {
        active = nullptr;
        if (observer) UnhookWindowsHookEx(observer);
        if (window) DestroyWindow(window);
        std::cerr << error.what() << '\n'; return 1;
    }
}
