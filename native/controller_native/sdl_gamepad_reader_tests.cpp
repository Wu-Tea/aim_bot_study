#include "sdl_gamepad_reader.h"
#include "ds4_output_report.h"
#include "output_composer.h"
#include "control_frame.h"
#include "test_support/native_test_registry.h"

#include <Windows.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <random>
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
    int (*set_hat)(void*, int, unsigned char) = nullptr;
    Guid (*guid)(void*) = nullptr;
    void (*guid_string)(Guid, char*, int) = nullptr;
    int (*add_mapping)(const char*) = nullptr;

    template<class T> void load(const char* name, T& function) {
        function = reinterpret_cast<T>(GetProcAddress(dll, name));
        require(function != nullptr, "SDL virtual input test API missing");
    }
    void start(int touchpad_index, bool remapped = false, bool mapped = true) {
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
        load("SDL_JoystickSetVirtualHat", set_hat);
        load("SDL_JoystickGetGUID", guid); load("SDL_JoystickGetGUIDString", guid_string);
        load("SDL_GameControllerAddMapping", add_mapping);
        require(init(0x6200) == 0, "SDL fixture init failed");
        initialized = true;
        index = attach(mapped ? 1 : 0, mapped ? 6 : 7, 21, remapped ? 1 : 0);
        require(index >= 0, "SDL virtual attach failed");
        joystick = open(index);
        require(joystick != nullptr, "SDL virtual open failed");
        if (!mapped) return;
        std::array<char, 33> id{};
        guid_string(guid(joystick), id.data(), static_cast<int>(id.size()));
        const std::string mapping = std::string(id.data()) + (remapped ?
            ",Remapped regression,a:b1,b:b0,x:b3,y:b2,back:b6,guide:b4,start:b5,"
            "leftstick:b8,rightstick:b7,leftshoulder:b10,rightshoulder:b9,"
            "dpup:h0.2,dpdown:h0.8,dpleft:h0.1,dpright:h0.4,leftx:a2,lefty:a3,"
            "rightx:a0,righty:a1,lefttrigger:a5,righttrigger:a4,touchpad:b" :
            ",DS4 input regression,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
            "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
            "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,"
            "rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,touchpad:b") +
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
    for (const auto& finger : physical.touchpad_fingers)
        require(!finger.active, "a controller without touch hardware must not invent contact");
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
void named_selection_matches_real_enumeration() {
    VirtualInput input; input.start(15);
    const auto devices=controller_native::scan_sdl_joystick_devices();
    const auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& d) { return d.device_index==input.index; });
    require(found!=devices.end() && found->opened && !found->name.empty() && !found->id.empty(),
        "actual enumeration must supply display name and selectable identity");
    controller_native::SdlGamepadReader correct(input.index,found->id);
    require(correct.available(),"selected identity must open the same real SDL input");
    controller_native::SdlGamepadReader wrong(input.index,"path:wrong");
    require(!wrong.available(),"an index cannot override an explicit device identity");
    require(correct.reconnect(),"explicit identity must survive reconnect with the original fixture");
    input.press_only(15);
    require((read_report(correct).bytes[6]&2)==2,"named selection must retain the native button/output mapping");
}

void semantic_mapping_survives_polling_and_reconnect() {
    VirtualInput input; input.start(20, true);
    controller_native::SdlGamepadReader reader(input.index);
    require(reader.available(), "mapped fixture must open");
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) require(reader.reconnect(), "mapped fixture reconnect");
        // A nonidentity layout must survive the actual reader and DS4 encoder.
        const int destination[] = {2,3,0,1,8,7};
        for (int axis = 0; axis < 6; ++axis) {
            for (int byte = 0; byte < 256; ++byte) {
                require(input.set_axis(input.joystick, axis,
                    static_cast<short>(byte * 257 - 32768)) == 0, "mapped axis injection");
                require(read_report(reader).bytes[destination[axis]] == byte,
                    "mapped axis/trigger must reach its semantic DS4 channel");
            }
            input.set_axis(input.joystick, axis, axis < 4 ? 0 : -32768);
            (void)read_report(reader);
        }
        const unsigned expected[] = {0x48,0x28,0x88,0x18,8,0x2008,0x1008,0x8008,0x4008,0x208,0x108};
        for (int button = 0; button < 11; ++button) {
            input.press_only(button);
            const auto b = read_report(reader).bytes;
            require((unsigned(b[4]) | (unsigned(b[5]) << 8)) == expected[button],
                "mapped button must reach its semantic DS4 bit");
            require((b[6] & 3) == (button == 4 ? 1 : 0), "mapped guide bit");
        }
        input.press_only(-1);
        for (const auto sample : {std::pair<unsigned char,int>{1,6}, {2,0}, {4,2}, {8,4}, {0,8}}) {
            require(input.set_hat(input.joystick, 0, sample.first) == 0, "hat injection");
            require(read_report(reader).bytes[4] == sample.second,
                "D-pad must use mapped directions rather than raw hat bits");
        }
        // Preserve sub-byte stick precision and exact neutral at the input boundary.
        for (short value : {short(-32768),short(-1),short(0),short(1),short(32767)}) {
            input.set_axis(input.joystick, 0, value);
            const auto state = reader.read();
            const float expected_x = float(value) / (value < 0 ? 32768.f : 32767.f);
            require(state.right_x == expected_x && state.left_x == 0,
                "mapped manual input must have no deadzone or cross-axis leakage");
        }
        input.set_axis(input.joystick,0,0);
        input.press_only(20);
        require((read_report(reader).bytes[6] & 2) != 0, "remapped touchpad survives reconnect");
        input.press_only(-1);
        (void)read_report(reader);
    }
}

void unmapped_joystick_is_not_a_gamepad() {
    VirtualInput input; input.start(15, false, false);
    controller_native::SdlGamepadReader reader(input.index);
    require(!reader.available() && !reader.read().connected,
        "unknown raw layout must not generate guessed controls");
    const auto devices = controller_native::scan_sdl_joystick_devices();
    const auto found = std::find_if(devices.begin(), devices.end(),
        [&](const auto& value) { return value.device_index == input.index; });
    require(found != devices.end() && !found->opened,
        "enumeration must not advertise an unmapped joystick as usable input");
}

void randomized_mapped_manual_passthrough() {
    VirtualInput input; input.start(20, true);
    controller_native::SdlGamepadReader reader(input.index);
    const int source_axis[] = {2,3,0,1};
    for (unsigned seed : {81008u,936117u}) {
        std::mt19937 random(seed);
        for (int scenario = 0; scenario < 16; ++scenario) {
            require(reader.reconnect(), "random scenario reconnect");
            const int steps = scenario % 2 ? 2000 : 128;
            for (int tick = 0; tick < steps; ++tick) {
                std::array<short,6> raw{};
                for (int axis = 0; axis < 6; ++axis) {
                    raw[axis] = static_cast<short>(int(random() % 65536) - 32768);
                    require(input.set_axis(input.joystick,axis,raw[axis]) == 0, "random axis injection");
                }
                const int button = int(random() % 3) - 1;
                input.press_only(button);
                const auto state = reader.read();
                const float actual[] = {state.left_x,-state.left_y,state.right_x,-state.right_y};
                for (int axis = 0; axis < 4; ++axis) {
                    const auto value = raw[source_axis[axis]];
                    const float expected = float(value) / (value < 0 ? 32768.f : 32767.f);
                    require(actual[axis] == expected, "random mapped stick preserves raw manual authority");
                }
                require(std::fabs(state.left_trigger - float(int(raw[5])+32768)/65535.f) <= 1.f/32767.f &&
                        std::fabs(state.right_trigger - float(int(raw[4])+32768)/65535.f) <= 1.f/32767.f,
                    "random mapped triggers preserve position within SDL quantization");
                require(state.a == (button == 1) && state.b == (button == 0), "random mapped button and release");
            }
        }
    }
}
} // namespace

void register_sdl_gamepad_reader_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "semantic_mapping_polling_reconnect", semantic_mapping_survives_polling_and_reconnect);
    registry.add_case("BaseRuntimeFreshness", "unmapped_joystick_rejected", unmapped_joystick_is_not_a_gamepad);
    registry.add_case("BaseRuntimeFreshness", "randomized_mapped_manual_passthrough", randomized_mapped_manual_passthrough);
    registry.add_case("BaseRuntimeFreshness", "named_input_real_sdl_enumeration", named_selection_matches_real_enumeration);
    registry.add_case("BaseRuntimeFreshness", "sony_buttons_to_ds4_report", all_sony_buttons_to_ds4);
    registry.add_case("BaseRuntimeFreshness", "touchpad_uses_sdl_device_binding", touchpad_uses_device_binding);
    registry.add_case("BaseRuntimeFreshness", "sony_axes_triggers_to_ds4_report", all_axes_and_triggers_to_ds4);
}
