#include "virtual_gamepad.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace controller_native {

namespace {

constexpr std::uint32_t kVigemErrorNone = 0x20000000;
constexpr std::uint32_t kVigemErrorUnavailable = 0xffffffffu;

constexpr std::uint16_t kButtonDpadUp = 0x0001;
constexpr std::uint16_t kButtonDpadDown = 0x0002;
constexpr std::uint16_t kButtonDpadLeft = 0x0004;
constexpr std::uint16_t kButtonDpadRight = 0x0008;
constexpr std::uint16_t kButtonStart = 0x0010;
constexpr std::uint16_t kButtonBack = 0x0020;
constexpr std::uint16_t kButtonLeftThumb = 0x0040;
constexpr std::uint16_t kButtonRightThumb = 0x0080;
constexpr std::uint16_t kButtonLeftShoulder = 0x0100;
constexpr std::uint16_t kButtonRightShoulder = 0x0200;
constexpr std::uint16_t kButtonGuide = 0x0400;
constexpr std::uint16_t kButtonA = 0x1000;
constexpr std::uint16_t kButtonB = 0x2000;
constexpr std::uint16_t kButtonX = 0x4000;
constexpr std::uint16_t kButtonY = 0x8000;

struct XusbReport {
    std::uint16_t wButtons = 0;
    std::uint8_t bLeftTrigger = 0;
    std::uint8_t bRightTrigger = 0;
    std::int16_t sThumbLX = 0;
    std::int16_t sThumbLY = 0;
    std::int16_t sThumbRX = 0;
    std::int16_t sThumbRY = 0;
};

static_assert(sizeof(XusbReport) == 12, "XUSB report layout must match ViGEmClient ABI");

std::filesystem::path executable_directory() {
    std::array<char, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::string(buffer.data(), length)).parent_path();
}

std::vector<std::filesystem::path> vigem_dll_candidates() {
    std::vector<std::filesystem::path> candidates;
    if (const char* from_env = std::getenv("VIGEM_CLIENT_DLL")) {
        if (from_env[0] != '\0') {
            candidates.emplace_back(from_env);
        }
    }
    candidates.emplace_back(executable_directory() / "ViGEmClient.dll");
    return candidates;
}

bool virtual_gamepad_log_enabled() {
    const char* value = std::getenv("GAMEPAD_INPUT_LOG");
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    const std::string text(value);
    return text != "0" && text != "false" && text != "False" && text != "off" && text != "OFF";
}

std::int16_t to_thumb_value(float value) {
    const float clamped = std::max(-1.0f, std::min(1.0f, value));
    if (clamped < 0.0f) {
        return static_cast<std::int16_t>(std::lround(clamped * 32768.0f));
    }
    return static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
}

std::uint8_t to_trigger_value(float value) {
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

XusbReport to_xusb_report(const GamepadOutputState& state) {
    XusbReport report;
    if (state.dpad_up) {
        report.wButtons |= kButtonDpadUp;
    }
    if (state.dpad_down) {
        report.wButtons |= kButtonDpadDown;
    }
    if (state.dpad_left) {
        report.wButtons |= kButtonDpadLeft;
    }
    if (state.dpad_right) {
        report.wButtons |= kButtonDpadRight;
    }
    if (state.start) {
        report.wButtons |= kButtonStart;
    }
    if (state.back) {
        report.wButtons |= kButtonBack;
    }
    if (state.left_thumb) {
        report.wButtons |= kButtonLeftThumb;
    }
    if (state.right_thumb) {
        report.wButtons |= kButtonRightThumb;
    }
    if (state.lb) {
        report.wButtons |= kButtonLeftShoulder;
    }
    if (state.rb) {
        report.wButtons |= kButtonRightShoulder;
    }
    if (state.guide) {
        report.wButtons |= kButtonGuide;
    }
    if (state.a) {
        report.wButtons |= kButtonA;
    }
    if (state.b) {
        report.wButtons |= kButtonB;
    }
    if (state.x) {
        report.wButtons |= kButtonX;
    }
    if (state.y) {
        report.wButtons |= kButtonY;
    }
    report.bLeftTrigger = to_trigger_value(state.left_trigger);
    report.bRightTrigger = to_trigger_value(state.right_trigger);
    report.sThumbLX = to_thumb_value(state.left_x);
    report.sThumbLY = to_thumb_value(state.left_y);
    report.sThumbRX = to_thumb_value(state.right_x);
    report.sThumbRY = to_thumb_value(state.right_y);
    return report;
}

template <typename Fn>
bool load_proc(HMODULE library, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(GetProcAddress(library, name));
    return out != nullptr;
}

}  // namespace

struct VirtualGamepad::ViGEmBackend {
    using VigemAlloc = void* (*)();
    using VigemFree = void (*)(void*);
    using VigemConnect = std::uint32_t (*)(void*);
    using VigemDisconnect = void (*)(void*);
    using VigemTargetAlloc = void* (*)();
    using VigemTargetFree = void (*)(void*);
    using VigemTargetAdd = std::uint32_t (*)(void*, void*);
    using VigemTargetRemove = std::uint32_t (*)(void*, void*);
    using VigemTargetX360Update = std::uint32_t (*)(void*, void*, XusbReport);

    HMODULE library = nullptr;
    void* client = nullptr;
    void* target = nullptr;
    VigemAlloc vigem_alloc = nullptr;
    VigemFree vigem_free = nullptr;
    VigemConnect vigem_connect = nullptr;
    VigemDisconnect vigem_disconnect = nullptr;
    VigemTargetAlloc vigem_target_x360_alloc = nullptr;
    VigemTargetFree vigem_target_free = nullptr;
    VigemTargetAdd vigem_target_add = nullptr;
    VigemTargetRemove vigem_target_remove = nullptr;
    VigemTargetX360Update vigem_target_x360_update = nullptr;
};

namespace {

void cleanup_vigem(VirtualGamepad::ViGEmBackend& backend) {
    if (backend.client != nullptr && backend.target != nullptr && backend.vigem_target_remove) {
        backend.vigem_target_remove(backend.client, backend.target);
    }
    if (backend.target != nullptr && backend.vigem_target_free) {
        backend.vigem_target_free(backend.target);
        backend.target = nullptr;
    }
    if (backend.client != nullptr && backend.vigem_disconnect) {
        backend.vigem_disconnect(backend.client);
    }
    if (backend.client != nullptr && backend.vigem_free) {
        backend.vigem_free(backend.client);
        backend.client = nullptr;
    }
    if (backend.library != nullptr) {
        FreeLibrary(backend.library);
        backend.library = nullptr;
    }
}

bool initialize_vigem(VirtualGamepad::ViGEmBackend& backend) {
    for (const auto& candidate : vigem_dll_candidates()) {
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        backend.library = LoadLibraryA(candidate.string().c_str());
        if (backend.library == nullptr) {
            continue;
        }
        break;
    }
    if (backend.library == nullptr) {
        return false;
    }

    const bool loaded =
        load_proc(backend.library, "vigem_alloc", backend.vigem_alloc) &&
        load_proc(backend.library, "vigem_free", backend.vigem_free) &&
        load_proc(backend.library, "vigem_connect", backend.vigem_connect) &&
        load_proc(backend.library, "vigem_disconnect", backend.vigem_disconnect) &&
        load_proc(backend.library, "vigem_target_x360_alloc", backend.vigem_target_x360_alloc) &&
        load_proc(backend.library, "vigem_target_free", backend.vigem_target_free) &&
        load_proc(backend.library, "vigem_target_add", backend.vigem_target_add) &&
        load_proc(backend.library, "vigem_target_remove", backend.vigem_target_remove) &&
        load_proc(
            backend.library,
            "vigem_target_x360_update",
            backend.vigem_target_x360_update);
    if (!loaded) {
        cleanup_vigem(backend);
        return false;
    }

    backend.client = backend.vigem_alloc();
    if (backend.client == nullptr) {
        cleanup_vigem(backend);
        return false;
    }
    if (backend.vigem_connect(backend.client) != kVigemErrorNone) {
        cleanup_vigem(backend);
        return false;
    }
    backend.target = backend.vigem_target_x360_alloc();
    if (backend.target == nullptr) {
        cleanup_vigem(backend);
        return false;
    }
    if (backend.vigem_target_add(backend.client, backend.target) != kVigemErrorNone) {
        cleanup_vigem(backend);
        return false;
    }
    return true;
}

}  // namespace

VirtualGamepad::VirtualGamepad() {
    vigem_ = std::make_unique<ViGEmBackend>();
    if (initialize_vigem(*vigem_)) {
        connected_ = true;
        logging_backend_ = false;
        if (virtual_gamepad_log_enabled()) {
            std::cout << "[NativeRuntime] ViGEm virtual Xbox 360 gamepad is online.\n";
        }
        return;
    }
    connected_ = false;
    logging_backend_ = true;
    last_error_code_ = kVigemErrorUnavailable;
    if (virtual_gamepad_log_enabled()) {
        std::cout << "[NativeRuntime] ViGEm unavailable; output recovery is pending.\n";
    }
}

VirtualGamepad::~VirtualGamepad() {
    if (vigem_) {
        cleanup_vigem(*vigem_);
    }
}

bool VirtualGamepad::is_connected() const {
    return connected_;
}

VirtualGamepadUpdateResult VirtualGamepad::update(const GamepadOutputState& state) {
    VirtualGamepadUpdateResult result;
    result.backend_connected = connected_;
    result.reconnect_count = reconnect_count_;
    result.error_code = last_error_code_;
    const auto now = std::chrono::steady_clock::now();

    const auto deliver = [&](const XusbReport& report) {
        if (vigem_ == nullptr || vigem_->client == nullptr || vigem_->target == nullptr ||
            vigem_->vigem_target_x360_update == nullptr) {
            return kVigemErrorUnavailable;
        }
        return vigem_->vigem_target_x360_update(vigem_->client, vigem_->target, report);
    };
    const XusbReport report = to_xusb_report(state);
    if (connected_) {
        const std::uint32_t code = deliver(report);
        if (code == kVigemErrorNone) {
            last_error_code_ = 0;
            result.delivered = true;
            result.backend_connected = true;
            result.error_code = 0;
            return result;
        }
        last_error_code_ = code;
        connected_ = false;
        cleanup_vigem(*vigem_);
        reconnect_throttle_.record_success();
    }

    if (vigem_ != nullptr && reconnect_throttle_.should_attempt(now)) {
        result.reconnect_attempted = true;
        if (initialize_vigem(*vigem_)) {
            ++reconnect_count_;
            connected_ = true;
            logging_backend_ = false;
            reconnect_throttle_.record_success();
            const std::uint32_t code = deliver(report);
            if (code == kVigemErrorNone) {
                last_error_code_ = 0;
                result.delivered = true;
                result.backend_connected = true;
                result.error_code = 0;
                result.reconnect_count = reconnect_count_;
                return result;
            }
            last_error_code_ = code;
            connected_ = false;
            cleanup_vigem(*vigem_);
        } else {
            last_error_code_ = kVigemErrorUnavailable;
        }
        reconnect_throttle_.record_failure(now);
    }

    result.backend_connected = connected_;
    result.error_code = last_error_code_;
    result.reconnect_count = reconnect_count_;
    if (logging_backend_ && virtual_gamepad_log_enabled() && result.reconnect_attempted) {
        std::cerr << "[NativeRuntime][Output] ViGEm unavailable; report not delivered.\n";
    }
    return result;
}

}  // namespace controller_native
