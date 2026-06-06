#pragma once

#include <vector>

namespace controller_native {

struct PhysicalGamepadState {
    bool connected = false;
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool rb = false;
    bool lb = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool back = false;
    bool guide = false;
    bool start = false;
    bool left_thumb = false;
    bool right_thumb = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
};

struct XInputUserSlot {
    unsigned int user_index = 0;
    bool connected = false;
};

class XInputReader {
public:
    explicit XInputReader(unsigned int user_index = 0);

    PhysicalGamepadState read() const;
    unsigned int user_index() const;

private:
    unsigned int user_index_ = 0;
};

std::vector<XInputUserSlot> scan_xinput_user_slots();
unsigned int detect_first_connected_user_index(unsigned int fallback_user_index = 0);

}  // namespace controller_native
