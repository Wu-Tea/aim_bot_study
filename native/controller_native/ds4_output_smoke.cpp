#include "sdl_gamepad_reader.h"
#include "virtual_gamepad.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

// Opt-in hardware smoke. Creates one temporary DS4 and sends neutral only;
// never runs Vision, AI, recoil or any physical-input forwarding.
int main() {
    using namespace controller_native;
    const auto before = scan_sdl_joystick_devices();
    VirtualGamepad output;
    if (!output.is_connected()) {
        std::cerr << "DS4 smoke: ViGEm connection failed\n";
        return 1;
    }
    if (!output.update({}).delivered) return 2;
    const auto after = scan_sdl_joystick_devices();
    const auto created = std::find_if(after.begin(), after.end(), [&](const auto& device) {
        return device.opened &&
            std::none_of(before.begin(), before.end(), [&](const auto& old) {
                return old.device_index == device.device_index && old.name == device.name;
            }) && (device.name.find("PS4") != std::string::npos ||
                device.name.find("DualShock") != std::string::npos);
    });
    if (created == after.end()) {
        std::cerr << "DS4 smoke: newly created DS4 not visible through SDL\n";
        for (const auto& device : after) std::cerr << device.name << '\n';
        return 3;
    }
    SdlGamepadReader reader(created->device_index);
    if (!reader.available()) return 4;
    unsigned int samples = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < end) {
        const auto result = output.update({});
        if (!result.delivered || result.reconnect_attempted) return 5;
        const auto state = reader.read();
        // SDL expands Sony's center byte 128 to +128 in signed 16-bit space.
        constexpr float neutral_tolerance = 1.0f / 127.0f;
        if (!state.connected || std::fabs(state.left_x) > neutral_tolerance ||
            std::fabs(state.left_y) > neutral_tolerance ||
            std::fabs(state.right_x) > neutral_tolerance ||
            std::fabs(state.right_y) > neutral_tolerance ||
            state.left_trigger != 0 || state.right_trigger != 0 ||
            state.a || state.b || state.x || state.y || state.rb || state.lb ||
            state.back || state.guide || state.start || state.touchpad || state.left_thumb ||
            state.right_thumb || state.dpad_up || state.dpad_down ||
            state.dpad_left || state.dpad_right) {
            std::cerr << "DS4 smoke: non-neutral or disconnected readback\n";
            return 6;
        }
        ++samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "DS4 smoke PASS: device=" << created->name
              << " neutral_readbacks=" << samples << '\n';
    // Reader closes before output target removal. No virtual device remains.
    return 0;
}
