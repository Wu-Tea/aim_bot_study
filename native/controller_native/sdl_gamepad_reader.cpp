#include "sdl_gamepad_reader.h"
#include "io_recovery_policy.h"
#include "sdl_touchpad_state.h"

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
constexpr int kSdlIgnore = 0;
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
constexpr int kControllerButtonTouchpad = 20;

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

std::string safe_name(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return "unknown";
    }
    return std::string(value);
}

// Opaque printable tokens survive the native TOML loader without introducing
// escaping rules for Windows paths, quotes or non-ASCII device names.
std::string identity_token(const char* prefix, const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result(prefix);
    result.reserve(result.size() + value.size() * 2);
    for (unsigned char c : value) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}

struct SdlApi {
    using SdlInit = int (*)(std::uint32_t);
    using SdlQuitSubSystem = void (*)(std::uint32_t);
    using SdlSetHint = int (*)(const char*, const char*);
    using SdlJoystickEventState = int (*)(int);
    using SdlPumpEvents = void (*)();
    using SdlNumJoysticks = int (*)();
    using SdlJoystickNameForIndex = const char* (*)(int);
    using SdlJoystickPathForIndex = const char* (*)(int);
    using SdlJoystickGetSerial = const char* (*)(void*);
    using SdlJoystickOpen = void* (*)(int);
    using SdlJoystickClose = void (*)(void*);
    using SdlJoystickUpdate = void (*)();
    using SdlJoystickGetAttached = int (*)(void*);
    using SdlJoystickNumAxes = int (*)(void*);
    using SdlJoystickNumButtons = int (*)(void*);
    using SdlJoystickNumHats = int (*)(void*);
    using SdlGameControllerOpen = void* (*)(int);
    using SdlGameControllerClose = void (*)(void*);
    using SdlGameControllerGetAxis = std::int16_t (*)(void*, int);
    using SdlGameControllerGetButton = std::uint8_t (*)(void*, int);
    using SdlIsGameController = int (*)(int);
    using SdlGameControllerGetNumTouchpadFingers = int (*)(void*, int);

    HMODULE library = nullptr;
    SdlInit init = nullptr;
    SdlQuitSubSystem quit_subsystem = nullptr;
    SdlSetHint set_hint = nullptr;
    SdlJoystickEventState joystick_event_state = nullptr;
    SdlPumpEvents pump_events = nullptr;
    SdlNumJoysticks num_joysticks = nullptr;
    SdlJoystickNameForIndex joystick_name_for_index = nullptr;
    SdlJoystickPathForIndex joystick_path_for_index = nullptr;
    SdlJoystickGetSerial joystick_serial = nullptr;
    SdlJoystickOpen joystick_open = nullptr;
    SdlJoystickClose joystick_close = nullptr;
    SdlJoystickUpdate joystick_update = nullptr;
    SdlJoystickGetAttached joystick_get_attached = nullptr;
    SdlJoystickNumAxes joystick_num_axes = nullptr;
    SdlJoystickNumButtons joystick_num_buttons = nullptr;
    SdlJoystickNumHats joystick_num_hats = nullptr;
    SdlGameControllerOpen game_controller_open = nullptr;
    SdlGameControllerClose game_controller_close = nullptr;
    SdlGameControllerGetAxis game_controller_axis = nullptr;
    SdlGameControllerGetButton game_controller_button = nullptr;
    SdlIsGameController is_game_controller = nullptr;
    SdlGameControllerGetNumTouchpadFingers game_controller_num_fingers = nullptr;
    SdlTouchpadFingerReader game_controller_finger = nullptr;

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
        ok = load_proc(library, "SDL_PumpEvents", pump_events) && ok;
        ok = load_proc(library, "SDL_NumJoysticks", num_joysticks) && ok;
        ok = load_proc(library, "SDL_JoystickNameForIndex", joystick_name_for_index) && ok;
        ok = load_proc(library, "SDL_JoystickOpen", joystick_open) && ok;
        ok = load_proc(library, "SDL_JoystickClose", joystick_close) && ok;
        ok = load_proc(library, "SDL_JoystickUpdate", joystick_update) && ok;
        ok = load_proc(library, "SDL_JoystickGetAttached", joystick_get_attached) && ok;
        ok = load_proc(library, "SDL_JoystickNumAxes", joystick_num_axes) && ok;
        ok = load_proc(library, "SDL_JoystickNumButtons", joystick_num_buttons) && ok;
        ok = load_proc(library, "SDL_JoystickNumHats", joystick_num_hats) && ok;
        ok = load_proc(library, "SDL_GameControllerOpen", game_controller_open) && ok;
        ok = load_proc(library, "SDL_GameControllerClose", game_controller_close) && ok;
        ok = load_proc(library, "SDL_GameControllerGetAxis", game_controller_axis) && ok;
        ok = load_proc(library, "SDL_GameControllerGetButton", game_controller_button) && ok;
        ok = load_proc(library, "SDL_IsGameController", is_game_controller) && ok;
        // Optional on older SDL DLLs: missing touch support must not disable
        // the existing axes/buttons or synthesize a touch request.
        load_proc(library, "SDL_GameControllerGetNumTouchpadFingers", game_controller_num_fingers);
        load_proc(library, "SDL_GameControllerGetTouchpadFinger", game_controller_finger);
        load_proc(library, "SDL_SetHint", set_hint);
        load_proc(library, "SDL_JoystickPathForIndex", joystick_path_for_index);
        load_proc(library, "SDL_JoystickGetSerial", joystick_serial);
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
    // RuntimeLoop owns a fixed-rate input poll and never consumes SDL_Event.
    // Disable SDL's implicit event-driven joystick updates so there is one
    // state-update owner instead of an unconsumed event queue plus manual
    // SDL_JoystickUpdate calls.
    api.set_hint("SDL_AUTO_UPDATE_JOYSTICKS", "0");
    api.set_hint("SDL_JOYSTICK_HIDAPI", "1");
    api.set_hint("SDL_JOYSTICK_HIDAPI_PS4", "1");
    api.set_hint("SDL_JOYSTICK_HIDAPI_PS5", "1");
}

}  // namespace

struct SdlGamepadReader::Backend {
    SdlApi api;
    void* joystick = nullptr;
    void* controller = nullptr;
    bool initialized = false;
    int axes = 0;
    int buttons = 0;
    int hats = 0;
    int touchpad_fingers = 0;

    ~Backend() {
        close();
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
        api.joystick_event_state(kSdlIgnore);
        return true;
    }

    bool open(int device_index) {
        close();
        // Mapping owns the meaning of every control. Unknown raw layouts
        // cannot safely be presented as a connected gamepad.
        if (!api.is_game_controller(device_index)) return false;
        joystick = api.joystick_open(device_index);
        if (joystick == nullptr) {
            return false;
        }
        axes = std::max(0, api.joystick_num_axes(joystick));
        buttons = std::max(0, api.joystick_num_buttons(joystick));
        hats = std::max(0, api.joystick_num_hats(joystick));
        controller = api.game_controller_open(device_index);
        if (controller == nullptr) {
            close();
            return false;
        }
        if (api.game_controller_num_fingers && api.game_controller_finger) {
            touchpad_fingers = std::clamp(api.game_controller_num_fingers(controller, 0), 0, 2);
        }
        return true;
    }

    std::string identity(int index, void* candidate) const {
        const auto name = safe_name(api.joystick_name_for_index(index));
        if (api.joystick_serial) {
            const char* serial = api.joystick_serial(candidate);
            if (serial && *serial) return identity_token("serial:", name + '\0' + serial);
        }
        if (api.joystick_path_for_index) {
            const char* path = api.joystick_path_for_index(index);
            if (path && *path) return identity_token("path:", path);
        }
        return name == "unknown" ? std::string{} : identity_token("name:", name);
    }

    std::vector<SdlJoystickDevice> devices() const {
        std::vector<SdlJoystickDevice> result;
        const int count = std::max(0, api.num_joysticks());
        result.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            SdlJoystickDevice device;
            device.device_index = index;
            device.name = safe_name(api.joystick_name_for_index(index));
            if (void* candidate = api.joystick_open(index)) {
                device.opened = api.is_game_controller(index) != 0;
                device.axes = std::max(0, api.joystick_num_axes(candidate));
                device.buttons = std::max(0, api.joystick_num_buttons(candidate));
                device.hats = std::max(0, api.joystick_num_hats(candidate));
                device.id = identity(index, candidate);
                api.joystick_close(candidate);
            }
            result.push_back(std::move(device));
        }
        return result;
    }

    void close() {
        if (controller != nullptr && api.game_controller_close != nullptr) {
            api.game_controller_close(controller);
        }
        controller = nullptr;
        touchpad_fingers = 0;
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

    float axis(int index) const {
        return normalize_sdl_axis(api.game_controller_axis(controller, index));
    }

    float trigger(int index) const {
        // SDL's mapped triggers are already 0..32767, with zero at rest.
        return std::clamp(float(api.game_controller_axis(controller, index)) / 32767.f, 0.f, 1.f);
    }

    bool button(int index) const {
        return api.game_controller_button(controller, index) != 0;
    }
};

std::vector<SdlJoystickDevice> scan_sdl_joystick_devices() {
    SdlGamepadReader::Backend backend;
    if (!backend.load_and_init()) {
        return {};
    }

    return backend.devices();
}

SdlGamepadReader::SdlGamepadReader(int device_index, std::string required_id)
    : backend_(std::make_unique<Backend>()), required_id_(std::move(required_id)) {
    if (!backend_->load_and_init() || !backend_->open(device_index)) {
        backend_.reset();
        return;
    }
    if (!required_id_.empty() && backend_->identity(device_index, backend_->joystick) != required_id_) {
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
    // This runtime does not poll SDL_Event. SDL requires an explicit event
    // pump in that case so HID-backed devices can publish their latest axis
    // state before SDL_JoystickUpdate/GetAxis reads it.
    backend_->api.pump_events();
    backend_->api.joystick_update();
    backend_->close();
    const auto devices = backend_->devices();
    const int selected = select_sdl_reconnect_device(
        devices, device_name_, expected_axes_, expected_buttons_, required_id_);
    if (selected < 0 || !backend_->open(selected)) {
        return false;
    }
    if (!required_id_.empty() && backend_->identity(selected, backend_->joystick) != required_id_) {
        backend_->close();
        return false;
    }
    device_index_ = selected;
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

    // Keep this on the runtime thread that initialized SDL. Calling only
    // SDL_JoystickUpdate is insufficient for some HIDAPI controllers when no
    // SDL event loop is running: their stick axes can otherwise remain at the
    // report captured when the device was opened.
    backend_->api.pump_events();
    backend_->api.joystick_update();
    if (!backend_->attached()) {
        return state;
    }
    state.connected = true;
    state.left_x = backend_->axis(0);
    state.left_y = -backend_->axis(1);
    state.right_x = backend_->axis(2);
    state.right_y = -backend_->axis(3);
    state.left_trigger = backend_->trigger(LEFT_TRIGGER_AXIS_INDEX);
    state.right_trigger = backend_->trigger(RIGHT_TRIGGER_AXIS_INDEX);

    state.a = backend_->button(kButtonA);
    state.b = backend_->button(kButtonB);
    state.x = backend_->button(kButtonX);
    state.y = backend_->button(kButtonY);
    state.back = backend_->button(kButtonBack);
    state.guide = backend_->button(kButtonGuide);
    state.start = backend_->button(kButtonStart);
    state.touchpad = backend_->button(kControllerButtonTouchpad);
    state.touchpad_fingers = read_sdl_touchpad_fingers(
        backend_->controller, backend_->touchpad_fingers,
        backend_->api.game_controller_finger);
    state.left_thumb = backend_->button(kButtonLeftThumb);
    state.right_thumb = backend_->button(kButtonRightThumb);
    state.lb = backend_->button(kButtonLeftShoulder);
    state.rb = backend_->button(kButtonRightShoulder);

    state.dpad_up = backend_->button(kButtonDpadUp);
    state.dpad_down = backend_->button(kButtonDpadDown);
    state.dpad_left = backend_->button(kButtonDpadLeft);
    state.dpad_right = backend_->button(kButtonDpadRight);
    return state;
}

}  // namespace controller_native
