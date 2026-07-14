#include "sdl_gamepad_reader.h"
#include "io_recovery_policy.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace controller_native {

namespace {

constexpr std::uint32_t kSdlInitJoystick = 0x00000200u;
constexpr std::uint32_t kSdlInitGameController = 0x00002000u;
constexpr std::uint32_t kSdlInitEvents = 0x00004000u;
constexpr std::uint32_t kSdlInitFlags =
    kSdlInitJoystick | kSdlInitGameController | kSdlInitEvents;
constexpr int kSdlEnable = 1;
constexpr int LEFT_TRIGGER_AXIS_INDEX = 4;
constexpr int RIGHT_TRIGGER_AXIS_INDEX = 5;
constexpr int kButtonA = 0;
constexpr int kButtonB = 1;
constexpr int kButtonX = 2;
constexpr int kButtonY = 3;
constexpr int kButtonBack = 4;
constexpr int kButtonGuide = 5;
constexpr int kButtonStart = 6;
constexpr int kButtonLeftThumb = 7;
constexpr int kButtonRightThumb = 8;
constexpr int kButtonLeftShoulder = 9;
constexpr int kButtonRightShoulder = 10;
constexpr int kButtonDpadUp = 11;
constexpr int kButtonDpadDown = 12;
constexpr int kButtonDpadLeft = 13;
constexpr int kButtonDpadRight = 14;
constexpr std::uint8_t kSdlHatUp = 0x01;
constexpr std::uint8_t kSdlHatRight = 0x02;
constexpr std::uint8_t kSdlHatDown = 0x04;
constexpr std::uint8_t kSdlHatLeft = 0x08;

template <typename Fn>
bool load_proc(HMODULE library, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(GetProcAddress(library, name));
    return out != nullptr;
}

std::filesystem::path executable_directory() {
    std::array<char, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::string(buffer.data(), length)).parent_path();
}

std::vector<std::filesystem::path> sdl_dll_candidates() {
    std::vector<std::filesystem::path> candidates;
    if (const char* from_env = std::getenv("SDL2_DLL")) {
        if (from_env[0] != '\0') {
            candidates.emplace_back(from_env);
        }
    }
    candidates.emplace_back(executable_directory() / "SDL2.dll");
    candidates.emplace_back("SDL2.dll");
    return candidates;
}

float normalize_sdl_axis(std::int16_t value) {
    if (value < 0) {
        return std::max(-1.0f, static_cast<float>(value) / 32768.0f);
    }
    return std::min(1.0f, static_cast<float>(value) / 32767.0f);
}

float trigger_to_unit(float value) {
    const float clamped = std::max(-1.0f, std::min(1.0f, value));
    return std::max(0.0f, std::min(1.0f, (clamped + 1.0f) * 0.5f));
}

std::string safe_name(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return "unknown";
    }
    return std::string(value);
}

struct SdlApi {
    using SdlInit = int (*)(std::uint32_t);
    using SdlQuitSubSystem = void (*)(std::uint32_t);
    using SdlSetHint = int (*)(const char*, const char*);
    using SdlJoystickEventState = int (*)(int);
    using SdlNumJoysticks = int (*)();
    using SdlJoystickNameForIndex = const char* (*)(int);
    using SdlJoystickOpen = void* (*)(int);
    using SdlJoystickClose = void (*)(void*);
    using SdlJoystickUpdate = void (*)();
    using SdlJoystickGetAttached = int (*)(void*);
    using SdlJoystickNumAxes = int (*)(void*);
    using SdlJoystickGetAxis = std::int16_t (*)(void*, int);
    using SdlJoystickNumButtons = int (*)(void*);
    using SdlJoystickGetButton = std::uint8_t (*)(void*, int);
    using SdlJoystickNumHats = int (*)(void*);
    using SdlJoystickGetHat = std::uint8_t (*)(void*, int);

    HMODULE library = nullptr;
    SdlInit init = nullptr;
    SdlQuitSubSystem quit_subsystem = nullptr;
    SdlSetHint set_hint = nullptr;
    SdlJoystickEventState joystick_event_state = nullptr;
    SdlNumJoysticks num_joysticks = nullptr;
    SdlJoystickNameForIndex joystick_name_for_index = nullptr;
    SdlJoystickOpen joystick_open = nullptr;
    SdlJoystickClose joystick_close = nullptr;
    SdlJoystickUpdate joystick_update = nullptr;
    SdlJoystickGetAttached joystick_get_attached = nullptr;
    SdlJoystickNumAxes joystick_num_axes = nullptr;
    SdlJoystickGetAxis joystick_get_axis = nullptr;
    SdlJoystickNumButtons joystick_num_buttons = nullptr;
    SdlJoystickGetButton joystick_get_button = nullptr;
    SdlJoystickNumHats joystick_num_hats = nullptr;
    SdlJoystickGetHat joystick_get_hat = nullptr;

    ~SdlApi() {
        if (library != nullptr) {
            FreeLibrary(library);
        }
    }

    bool load() {
        for (const std::filesystem::path& candidate : sdl_dll_candidates()) {
            library = LoadLibraryA(candidate.string().c_str());
            if (library != nullptr) {
                break;
            }
        }
        if (library == nullptr) {
            return false;
        }

        bool ok = true;
        ok = load_proc(library, "SDL_Init", init) && ok;
        ok = load_proc(library, "SDL_QuitSubSystem", quit_subsystem) && ok;
        ok = load_proc(library, "SDL_JoystickEventState", joystick_event_state) && ok;
        ok = load_proc(library, "SDL_NumJoysticks", num_joysticks) && ok;
        ok = load_proc(library, "SDL_JoystickNameForIndex", joystick_name_for_index) && ok;
        ok = load_proc(library, "SDL_JoystickOpen", joystick_open) && ok;
        ok = load_proc(library, "SDL_JoystickClose", joystick_close) && ok;
        ok = load_proc(library, "SDL_JoystickUpdate", joystick_update) && ok;
        ok = load_proc(library, "SDL_JoystickGetAttached", joystick_get_attached) && ok;
        ok = load_proc(library, "SDL_JoystickNumAxes", joystick_num_axes) && ok;
        ok = load_proc(library, "SDL_JoystickGetAxis", joystick_get_axis) && ok;
        ok = load_proc(library, "SDL_JoystickNumButtons", joystick_num_buttons) && ok;
        ok = load_proc(library, "SDL_JoystickGetButton", joystick_get_button) && ok;
        ok = load_proc(library, "SDL_JoystickNumHats", joystick_num_hats) && ok;
        ok = load_proc(library, "SDL_JoystickGetHat", joystick_get_hat) && ok;
        load_proc(library, "SDL_SetHint", set_hint);
        if (!ok) {
            FreeLibrary(library);
            library = nullptr;
        }
        return ok;
    }
};

void apply_sdl_joystick_hints(SdlApi& api) {
    if (api.set_hint == nullptr) {
        return;
    }
    api.set_hint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    api.set_hint("SDL_JOYSTICK_HIDAPI", "1");
    api.set_hint("SDL_JOYSTICK_HIDAPI_PS4", "1");
    api.set_hint("SDL_JOYSTICK_HIDAPI_PS5", "1");
}

}  // namespace

struct SdlGamepadReader::Backend {
    SdlApi api;
    void* joystick = nullptr;
    bool initialized = false;
    int axes = 0;
    int buttons = 0;
    int hats = 0;

    ~Backend() {
        if (joystick != nullptr && api.joystick_close != nullptr) {
            api.joystick_close(joystick);
        }
        if (initialized && api.quit_subsystem != nullptr) {
            api.quit_subsystem(kSdlInitFlags);
        }
    }

    bool load_and_init() {
        if (!api.load()) {
            return false;
        }
        apply_sdl_joystick_hints(api);
        if (api.init(kSdlInitFlags) != 0) {
            return false;
        }
        initialized = true;
        api.joystick_event_state(kSdlEnable);
        return true;
    }

    bool open(int device_index) {
        close();
        joystick = api.joystick_open(device_index);
        if (joystick == nullptr) {
            return false;
        }
        axes = std::max(0, api.joystick_num_axes(joystick));
        buttons = std::max(0, api.joystick_num_buttons(joystick));
        hats = std::max(0, api.joystick_num_hats(joystick));
        return true;
    }

    void close() {
        if (joystick != nullptr && api.joystick_close != nullptr) {
            api.joystick_close(joystick);
        }
        joystick = nullptr;
        axes = 0;
        buttons = 0;
        hats = 0;
    }

    bool attached() const {
        return joystick != nullptr && api.joystick_get_attached != nullptr &&
            api.joystick_get_attached(joystick) != 0;
    }

    float axis_or(int index, float fallback) const {
        if (joystick == nullptr || index < 0 || index >= axes) {
            return fallback;
        }
        return normalize_sdl_axis(api.joystick_get_axis(joystick, index));
    }

    bool button(int index) const {
        if (joystick == nullptr || index < 0 || index >= buttons) {
            return false;
        }
        return api.joystick_get_button(joystick, index) != 0;
    }
};

std::vector<SdlJoystickDevice> scan_sdl_joystick_devices() {
    SdlGamepadReader::Backend backend;
    if (!backend.load_and_init()) {
        return {};
    }

    std::vector<SdlJoystickDevice> devices;
    const int count = std::max(0, backend.api.num_joysticks());
    devices.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        SdlJoystickDevice device;
        device.device_index = index;
        device.name = safe_name(backend.api.joystick_name_for_index(index));
        if (void* joystick = backend.api.joystick_open(index)) {
            device.opened = true;
            device.axes = std::max(0, backend.api.joystick_num_axes(joystick));
            device.buttons = std::max(0, backend.api.joystick_num_buttons(joystick));
            device.hats = std::max(0, backend.api.joystick_num_hats(joystick));
            backend.api.joystick_close(joystick);
        }
        devices.push_back(device);
    }
    return devices;
}

SdlGamepadReader::SdlGamepadReader(int device_index)
    : backend_(std::make_unique<Backend>()) {
    if (!backend_->load_and_init() || !backend_->open(device_index)) {
        backend_.reset();
        return;
    }
    device_index_ = device_index;
    device_name_ = safe_name(backend_->api.joystick_name_for_index(device_index));
    expected_axes_ = backend_->axes;
    expected_buttons_ = backend_->buttons;
}

SdlGamepadReader::~SdlGamepadReader() = default;

bool SdlGamepadReader::available() const {
    return backend_ != nullptr && backend_->joystick != nullptr;
}

bool SdlGamepadReader::attached() const {
    return available() && backend_->attached();
}

bool SdlGamepadReader::reconnect() {
    if (backend_ == nullptr) {
        return false;
    }
    backend_->api.joystick_update();
    backend_->close();
    std::vector<SdlJoystickDevice> devices;
    const int count = std::max(0, backend_->api.num_joysticks());
    devices.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        SdlJoystickDevice device;
        device.device_index = index;
        device.name = safe_name(backend_->api.joystick_name_for_index(index));
        if (void* candidate = backend_->api.joystick_open(index)) {
            device.opened = true;
            device.axes = std::max(0, backend_->api.joystick_num_axes(candidate));
            device.buttons = std::max(0, backend_->api.joystick_num_buttons(candidate));
            device.hats = std::max(0, backend_->api.joystick_num_hats(candidate));
            backend_->api.joystick_close(candidate);
        }
        devices.push_back(std::move(device));
    }
    const int selected = select_sdl_reconnect_device(
        devices, device_name_, expected_axes_, expected_buttons_);
    if (selected < 0 || !backend_->open(selected)) {
        return false;
    }
    device_index_ = selected;
    trigger_initialized_ = false;
    return true;
}

int SdlGamepadReader::device_index() const {
    return device_index_;
}

const std::string& SdlGamepadReader::device_name() const {
    return device_name_;
}

PhysicalGamepadState SdlGamepadReader::read() {
    PhysicalGamepadState state;
    if (!available()) {
        return state;
    }

    backend_->api.joystick_update();
    if (!backend_->attached()) {
        return state;
    }
    state.connected = true;
    state.left_x = backend_->axis_or(0, 0.0f);
    state.left_y = -backend_->axis_or(1, 0.0f);
    state.right_x = backend_->axis_or(2, 0.0f);
    state.right_y = -backend_->axis_or(3, 0.0f);

    float raw_left_trigger = backend_->axis_or(LEFT_TRIGGER_AXIS_INDEX, -1.0f);
    float raw_right_trigger = backend_->axis_or(RIGHT_TRIGGER_AXIS_INDEX, -1.0f);
    if (!trigger_initialized_ && (raw_left_trigger != 0.0f || raw_right_trigger != 0.0f)) {
        trigger_initialized_ = true;
    }
    if (!trigger_initialized_) {
        raw_left_trigger = -1.0f;
        raw_right_trigger = -1.0f;
    }
    state.left_trigger = trigger_to_unit(raw_left_trigger);
    state.right_trigger = trigger_to_unit(raw_right_trigger);

    state.a = backend_->button(kButtonA);
    state.b = backend_->button(kButtonB);
    state.x = backend_->button(kButtonX);
    state.y = backend_->button(kButtonY);
    state.back = backend_->button(kButtonBack);
    state.guide = backend_->button(kButtonGuide);
    state.start = backend_->button(kButtonStart);
    state.left_thumb = backend_->button(kButtonLeftThumb);
    state.right_thumb = backend_->button(kButtonRightThumb);
    state.lb = backend_->button(kButtonLeftShoulder);
    state.rb = backend_->button(kButtonRightShoulder);

    if (backend_->hats > 0) {
        const std::uint8_t hat = backend_->api.joystick_get_hat(backend_->joystick, 0);
        state.dpad_up = (hat & kSdlHatUp) != 0;
        state.dpad_down = (hat & kSdlHatDown) != 0;
        state.dpad_left = (hat & kSdlHatLeft) != 0;
        state.dpad_right = (hat & kSdlHatRight) != 0;
    } else {
        state.dpad_up = backend_->button(kButtonDpadUp);
        state.dpad_down = backend_->button(kButtonDpadDown);
        state.dpad_left = backend_->button(kButtonDpadLeft);
        state.dpad_right = backend_->button(kButtonDpadRight);
    }
    return state;
}

}  // namespace controller_native
