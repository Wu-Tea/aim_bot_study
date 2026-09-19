#pragma once

#include "virtual_gamepad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace controller_native {

// ViGEm DS4_REPORT_EX ABI: the 63 bytes after HID report ID 0x01.
// Use the extended report so the driver's canned gyro/touch data cannot leak
// through a partial update. This runtime supplies sticks/buttons, not sensors.
struct Ds4OutputReport {
    std::array<std::uint8_t, 63> bytes{};
};
static_assert(sizeof(Ds4OutputReport) == 63, "DS4_REPORT_EX ABI mismatch");
static_assert(alignof(Ds4OutputReport) == 1, "DS4_REPORT_EX must be packed");

inline std::uint8_t ds4_axis(float value) noexcept {
    if (!std::isfinite(value)) return 128;
    const float bounded = std::clamp(value, -1.0f, 1.0f);
    // Preserve exact neutral and both endpoints without a curve or deadzone.
    return static_cast<std::uint8_t>(std::lround(
        128.0f + bounded * (bounded < 0.0f ? 128.0f : 127.0f)));
}

inline std::uint8_t ds4_trigger(float value) noexcept {
    if (!std::isfinite(value)) return 0;
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

inline Ds4OutputReport to_ds4_report(
    const GamepadOutputState& state,
    std::uint8_t sequence = 0,
    std::uint16_t timestamp = 0) noexcept {
    Ds4OutputReport report;
    auto& b = report.bytes;
    b[0] = ds4_axis(state.left_x);
    b[1] = ds4_axis(-state.left_y); // Internal positive Y is up; DS4 is down.
    b[2] = ds4_axis(state.right_x);
    b[3] = ds4_axis(-state.right_y);

    // Opposite directions cancel on their own axis. DS4's neutral hat is 8,
    // not 0 (which means up).
    const int x = int(state.dpad_right) - int(state.dpad_left);
    const int y = int(state.dpad_down) - int(state.dpad_up);
    std::uint16_t buttons = y < 0 ? (x < 0 ? 7 : x > 0 ? 1 : 0)
        : y > 0 ? (x < 0 ? 5 : x > 0 ? 3 : 4)
        : x < 0 ? 6 : x > 0 ? 2 : 8;
    if (state.x) buttons |= 1u << 4; // Square
    if (state.a) buttons |= 1u << 5; // Cross
    if (state.b) buttons |= 1u << 6; // Circle
    if (state.y) buttons |= 1u << 7; // Triangle
    if (state.lb) buttons |= 1u << 8;
    if (state.rb) buttons |= 1u << 9;
    b[7] = ds4_trigger(state.left_trigger);
    b[8] = ds4_trigger(state.right_trigger);
    if (b[7] != 0) buttons |= 1u << 10; // L2 digital companion
    if (b[8] != 0) buttons |= 1u << 11; // R2 digital companion
    if (state.back) buttons |= 1u << 12; // Share
    if (state.start) buttons |= 1u << 13; // Options
    if (state.left_thumb) buttons |= 1u << 14;
    if (state.right_thumb) buttons |= 1u << 15;
    b[4] = static_cast<std::uint8_t>(buttons);
    b[5] = static_cast<std::uint8_t>(buttons >> 8);
    b[6] = static_cast<std::uint8_t>(
        ((sequence & 0x3fu) << 2) | (state.guide ? 1u : 0u) |
        (state.touchpad ? 2u : 0u));
    b[9] = static_cast<std::uint8_t>(timestamp);
    b[10] = static_cast<std::uint8_t>(timestamp >> 8);
    b[29] = 0x1b; // USB connected, full battery; no headset/accessory.
    // All three touch slots have both contacts lifted, even when a consumer
    // inspects them despite the zero touch-packet count at byte 32.
    for (const int offset : {34, 38, 43, 47, 52, 56}) b[offset] = 0x80;
    return report;
}

} // namespace controller_native
