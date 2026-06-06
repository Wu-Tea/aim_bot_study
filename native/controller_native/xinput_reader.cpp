#include "xinput_reader.h"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <vector>

namespace controller_native {

namespace {

float normalize_stick(SHORT value) {
    if (value < 0) {
        return std::max(-1.0f, static_cast<float>(value) / 32768.0f);
    }
    return std::min(1.0f, static_cast<float>(value) / 32767.0f);
}

float normalize_trigger(BYTE value) {
    return static_cast<float>(value) / 255.0f;
}

bool has_button(WORD buttons, WORD mask) {
    return (buttons & mask) != 0;
}

}  // namespace

XInputReader::XInputReader(unsigned int user_index)
    : user_index_(user_index) {}

unsigned int XInputReader::user_index() const {
    return user_index_;
}

PhysicalGamepadState XInputReader::read() const {
    XINPUT_STATE raw_state{};
    const DWORD result = XInputGetState(user_index_, &raw_state);
    PhysicalGamepadState state;
    if (result != ERROR_SUCCESS) {
        return state;
    }

    const XINPUT_GAMEPAD& gamepad = raw_state.Gamepad;
    state.connected = true;
    state.left_x = normalize_stick(gamepad.sThumbLX);
    state.left_y = normalize_stick(gamepad.sThumbLY);
    state.right_x = normalize_stick(gamepad.sThumbRX);
    state.right_y = normalize_stick(gamepad.sThumbRY);
    state.left_trigger = normalize_trigger(gamepad.bLeftTrigger);
    state.right_trigger = normalize_trigger(gamepad.bRightTrigger);
    state.rb = has_button(gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    state.lb = has_button(gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
    state.a = has_button(gamepad.wButtons, XINPUT_GAMEPAD_A);
    state.b = has_button(gamepad.wButtons, XINPUT_GAMEPAD_B);
    state.x = has_button(gamepad.wButtons, XINPUT_GAMEPAD_X);
    state.y = has_button(gamepad.wButtons, XINPUT_GAMEPAD_Y);
    state.back = has_button(gamepad.wButtons, XINPUT_GAMEPAD_BACK);
    state.start = has_button(gamepad.wButtons, XINPUT_GAMEPAD_START);
    state.left_thumb = has_button(gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
    state.right_thumb = has_button(gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    state.dpad_up = has_button(gamepad.wButtons, XINPUT_GAMEPAD_DPAD_UP);
    state.dpad_down = has_button(gamepad.wButtons, XINPUT_GAMEPAD_DPAD_DOWN);
    state.dpad_left = has_button(gamepad.wButtons, XINPUT_GAMEPAD_DPAD_LEFT);
    state.dpad_right = has_button(gamepad.wButtons, XINPUT_GAMEPAD_DPAD_RIGHT);
    return state;
}

std::vector<XInputUserSlot> scan_xinput_user_slots() {
    std::vector<XInputUserSlot> slots;
    slots.reserve(XUSER_MAX_COUNT);
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE raw_state{};
        XInputUserSlot slot;
        slot.user_index = static_cast<unsigned int>(index);
        slot.connected = XInputGetState(index, &raw_state) == ERROR_SUCCESS;
        slots.push_back(slot);
    }
    return slots;
}

unsigned int detect_first_connected_user_index(unsigned int fallback_user_index) {
    const unsigned int clamped_fallback =
        std::min<unsigned int>(fallback_user_index, XUSER_MAX_COUNT - 1);
    const std::vector<XInputUserSlot> slots = scan_xinput_user_slots();
    for (const XInputUserSlot& slot : slots) {
        if (slot.connected) {
            return slot.user_index;
        }
    }
    return clamped_fallback;
}

}  // namespace controller_native
