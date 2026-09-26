#include "virtual_gamepad.h"
#include "ds4_output_report.h"

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
    using VigemTargetDs4Update = std::uint32_t (*)(void*, void*, Ds4OutputReport);

    HMODULE library = nullptr;
    void* client = nullptr;
    void* target = nullptr;
    VigemAlloc vigem_alloc = nullptr;
    VigemFree vigem_free = nullptr;
    VigemConnect vigem_connect = nullptr;
    VigemDisconnect vigem_disconnect = nullptr;
    VigemTargetAlloc vigem_target_ds4_alloc = nullptr;
    VigemTargetFree vigem_target_free = nullptr;
    VigemTargetAdd vigem_target_add = nullptr;
    VigemTargetRemove vigem_target_remove = nullptr;
    VigemTargetDs4Update vigem_target_ds4_update_ex = nullptr;
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
        load_proc(backend.library, "vigem_target_ds4_alloc", backend.vigem_target_ds4_alloc) &&
        load_proc(backend.library, "vigem_target_free", backend.vigem_target_free) &&
        load_proc(backend.library, "vigem_target_add", backend.vigem_target_add) &&
        load_proc(backend.library, "vigem_target_remove", backend.vigem_target_remove) &&
        load_proc(
            backend.library,
            "vigem_target_ds4_update_ex",
            backend.vigem_target_ds4_update_ex);
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
    backend.target = backend.vigem_target_ds4_alloc();
    if (backend.target == nullptr) {
        cleanup_vigem(backend);
        return false;
    }
    if (backend.vigem_target_add(backend.client, backend.target) != kVigemErrorNone) {
        cleanup_vigem(backend);
        return false;
    }
    // Replace the driver's canned sensor/axis report before normal operation,
    // including when the target has just been recreated after a disconnect.
    if (backend.vigem_target_ds4_update_ex(
            backend.client, backend.target, to_ds4_report({})) != kVigemErrorNone) {
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
            std::cout << "[NativeRuntime] ViGEm virtual DualShock 4 gamepad is online.\n";
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

    const auto deliver = [&](const Ds4OutputReport& report) {
        if (vigem_ == nullptr || vigem_->client == nullptr || vigem_->target == nullptr ||
            vigem_->vigem_target_ds4_update_ex == nullptr) {
            return kVigemErrorUnavailable;
        }
        const auto submitted = std::chrono::steady_clock::now();
        const auto code = vigem_->vigem_target_ds4_update_ex(vigem_->client, vigem_->target, report);
        if (code == kVigemErrorNone) {
            result.submitted_at_seconds = std::chrono::duration<double>(
                submitted.time_since_epoch()).count();
            result.delivered_right_x = ds4_axis_value(report.bytes[2]);
            result.delivered_right_y = -ds4_axis_value(report.bytes[3]);
        }
        return code;
    };
    // DS4 sensor timestamps use 16/3 microsecond units. They describe report
    // production only; they do not claim that the game has consumed it.
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    const Ds4OutputReport report = to_ds4_report(
        state, report_sequence_++, static_cast<std::uint16_t>((micros * 3) / 16));
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
