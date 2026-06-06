#pragma once

#include <memory>

namespace controller_native {

struct GamepadOutputState {
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

class VirtualGamepad {
public:
    struct ViGEmBackend;

    VirtualGamepad();
    ~VirtualGamepad();

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    bool is_connected() const;
    void update(const GamepadOutputState& state);

private:
    bool connected_ = false;
    bool logging_backend_ = false;
    std::unique_ptr<ViGEmBackend> vigem_;
};

}  // namespace controller_native
