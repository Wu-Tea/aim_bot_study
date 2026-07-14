#pragma once

#include "sdl_gamepad_reader.h"

#include <chrono>
#include <string>
#include <vector>

namespace controller_native {

int select_sdl_reconnect_device(
    const std::vector<SdlJoystickDevice>& devices,
    const std::string& preferred_name,
    int minimum_axes,
    int minimum_buttons);

class IoReconnectThrottle {
public:
    explicit IoReconnectThrottle(std::chrono::milliseconds retry_interval);

    bool should_attempt(std::chrono::steady_clock::time_point now) const;
    void record_failure(std::chrono::steady_clock::time_point now);
    void record_success();
    unsigned int failure_count() const;

private:
    std::chrono::milliseconds retry_interval_{};
    std::chrono::steady_clock::time_point retry_not_before_{};
    unsigned int failure_count_ = 0;
};

}  // namespace controller_native
