#pragma once

#include "io_recovery_policy.h"

#include <chrono>
#include <cstdint>
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

struct VirtualGamepadUpdateResult {
    bool delivered = false;
    bool backend_connected = false;
    bool reconnect_attempted = false;
    std::uint32_t error_code = 0;
    unsigned int reconnect_count = 0;
};

class VirtualGamepad {
public:
    struct ViGEmBackend;

    VirtualGamepad();
    ~VirtualGamepad();

    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    bool is_connected() const;
    VirtualGamepadUpdateResult update(const GamepadOutputState& state);

private:
    bool connected_ = false;
    bool logging_backend_ = false;
    std::unique_ptr<ViGEmBackend> vigem_;
    IoReconnectThrottle reconnect_throttle_{std::chrono::milliseconds(500)};
    unsigned int reconnect_count_ = 0;
    std::uint32_t last_error_code_ = 0;
};

}  // namespace controller_native
