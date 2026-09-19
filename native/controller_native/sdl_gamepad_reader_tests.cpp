#include "sdl_gamepad_reader.h"
#include "ds4_output_report.h"
#include "output_composer.h"
#include "control_frame.h"
#include "test_support/native_test_registry.h"

#include <Windows.h>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// SDL's in-process virtual joystick never creates an OS/game-visible pad.
// Exercise the real dynamically loaded reader without emitting gameplay input.
struct VirtualInput {
    struct Guid { unsigned char data[16]; };
    HMODULE dll = nullptr;
    int index = -1;
    void* joystick = nullptr;
    bool initialized = false;
    int (*init)(unsigned int) = nullptr;
    void (*quit)(unsigned int) = nullptr;
    int (*attach)(int, int, int, int) = nullptr;
    int (*detach)(int) = nullptr;
    void* (*open)(int) = nullptr;
    void (*close)(void*) = nullptr;
    int (*set_button)(void*, int, unsigned char) = nullptr;
    int (*set_axis)(void*, int, short) = nullptr;
    Guid (*guid)(void*) = nullptr;
    void (*guid_string)(Guid, char*, int) = nullptr;
    int (*add_mapping)(const char*) = nullptr;

    template<class T> void load(const char* name, T& function) {
        function = reinterpret_cast<T>(GetProcAddress(dll, name));
        require(function != nullptr, "SDL virtual input test API missing");
    }
    void start(int touchpad_index) {
        std::array<wchar_t, MAX_PATH> path{};
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        const auto dll_path = std::filesystem::path(path.data()).parent_path() / "SDL2.dll";
        dll = LoadLibraryW(dll_path.c_str());
        require(dll != nullptr, "SDL2.dll required for reader integration test");
        load("SDL_Init", init); load("SDL_QuitSubSystem", quit);
        load("SDL_JoystickAttachVirtual", attach); load("SDL_JoystickDetachVirtual", detach);
        load("SDL_JoystickOpen", open); load("SDL_JoystickClose", close);
        load("SDL_JoystickSetVirtualButton", set_button);
        load("SDL_JoystickSetVirtualAxis", set_axis);
        load("SDL_JoystickGetGUID", guid); load("SDL_JoystickGetGUIDString", guid_string);
        load("SDL_GameControllerAddMapping", add_mapping);
        require(init(0x6200) == 0, "SDL fixture init failed");
        initialized = true;
        index = attach(1, 6, 21, 0);
        require(index >= 0, "SDL virtual attach failed");
        joystick = open(index);
        require(joystick != nullptr, "SDL virtual open failed");
        std::array<char, 33> id{};
        guid_string(guid(joystick), id.data(), static_cast<int>(id.size()));
        const std::string mapping = std::string(id.data()) +
            ",DS4 input regression,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
            "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
            "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,"
            "rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,touchpad:b" +
            std::to_string(touchpad_index) + ",platform:Windows,";
        require(add_mapping(mapping.c_str()) >= 0, "SDL fixture mapping rejected");
        require(set_axis(joystick, 4, -32768) == 0 && set_axis(joystick, 5, -32768) == 0,
            "SDL fixture must start with triggers released");
    }
    void press_only(int button) {
        for (int i = 0; i < 21; ++i)
            require(set_button(joystick, i, i == button ? 1 : 0) == 0,
                "SDL fixture button injection failed");
    }
    ~VirtualInput() {
        if (joystick && close) close(joystick);
        if (index >= 0 && detach) detach(index);
        if (initialized && quit) quit(0x6200);
        if (dll) FreeLibrary(dll);
    }
};

controller_native::Ds4OutputReport read_report(controller_native::SdlGamepadReader& reader) {
    const auto physical = reader.read();
    require(physical.connected, "virtual fixture input disconnected");
    auto frame = controller_native::ControlFrame::begin(physical,
        pipeline_contract::ControllerTickId::from(1),
        pipeline_contract::EventSequence::from(1));
    controller_native::OutputComposer composer;
    require(composer.compose(frame) == controller_native::OutputComposeStatus::Ok,
        "reader output must compose");
    return controller_native::to_ds4_report(*composer.finalized_output());
}

void all_sony_buttons_to_ds4() {
    VirtualInput input; input.start(15); // Actual SDL 2.28.4 Sony HIDAPI binding.
    controller_native::SdlGamepadReader reader(input.index);
    require(reader.available(), "production SDL reader failed to open fixture");
    const unsigned expected[] = {0x28,0x48,0x18,0x88,0x1008,8,0x2008,
        0x4008,0x8008,0x108,0x208,0,4,6,2,8,8,8,8,8,8};
    for (int button = 0; button < 21; ++button) {
        input.press_only(button);
        const auto b = read_report(reader).bytes;
        require((unsigned(b[4]) | (unsigned(b[5]) << 8)) == expected[button],
            "physical SDL button must reach the correct DS4 report bit only");
        require((b[6] & 3) == (button == 5 ? 1 : button == 15 ? 2 : 0),
            "touchpad/PS must forward; mute/Fn/paddles must not masquerade as touchpad");
        input.press_only(-1);
        const auto released = read_report(reader).bytes;
        require(released[4] == 8 && released[5] == 0 && released[6] == 0,
            "every digital release must reach the very next report");
    }
}

void touchpad_uses_device_binding() {
    VirtualInput input; input.start(20);
    controller_native::SdlGamepadReader reader(input.index);
    input.press_only(15);
    require((read_report(reader).bytes[6] & 2) == 0,
        "an unrelated button 15 must not be assumed to be touchpad");
    input.press_only(20);
    require((read_report(reader).bytes[6] & 2) == 2,
        "touchpad must follow SDL's actual device mapping");
}

void all_axes_and_triggers_to_ds4() {
    VirtualInput input; input.start(15);
    controller_native::SdlGamepadReader reader(input.index);
    // Exercise physical polling and composition as well as encoding, including
    // all 256 Sony codes, both Y inversions, and trigger initialization.
    for (int axis = 0; axis < 6; ++axis) {
        for (int byte = 0; byte < 256; ++byte) {
            require(input.set_axis(input.joystick, axis,
                static_cast<short>(byte * 257 - 32768)) == 0,
                "axis injection failed");
            const auto b = read_report(reader).bytes;
            const int report_index = axis < 4 ? axis : axis + 3;
            require(b[report_index] == byte,
                "every physical Sony axis/trigger code must survive polling/composition/report");
            if (axis >= 4) {
                const unsigned mask = axis == 4 ? 4 : 8;
                require((b[5] & mask) == (byte == 0 ? 0 : mask),
                    "trigger digital bit must track its transmitted analog value");
            }
        }
        require(input.set_axis(input.joystick, axis, axis < 4 ? 0 : -32768) == 0,
            "axis release failed");
        const auto b = read_report(reader).bytes;
        require(b[axis < 4 ? axis : axis + 3] == (axis < 4 ? 128 : 0),
            "axis/trigger release must be reflected immediately");
    }
}
} // namespace

void register_sdl_gamepad_reader_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "sony_buttons_to_ds4_report", all_sony_buttons_to_ds4);
    registry.add_case("BaseRuntimeFreshness", "touchpad_uses_sdl_device_binding", touchpad_uses_device_binding);
    registry.add_case("BaseRuntimeFreshness", "sony_axes_triggers_to_ds4_report", all_axes_and_triggers_to_ds4);
}
