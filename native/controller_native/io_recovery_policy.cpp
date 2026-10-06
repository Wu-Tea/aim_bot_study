#include "io_recovery_policy.h"

namespace controller_native {

int select_sdl_reconnect_device(
    const std::vector<SdlJoystickDevice>& devices,
    const std::string& preferred_name,
    int minimum_axes,
    int minimum_buttons,
    const std::string& required_id) {
    if (preferred_name.empty() && required_id.empty()) {
        return -1;
    }
    int selected = -1;
    for (const SdlJoystickDevice& device : devices) {
        const bool matches = required_id.empty() ? device.name == preferred_name : device.id == required_id;
        if (device.opened && matches &&
            device.axes >= minimum_axes && device.buttons >= minimum_buttons) {
            if (required_id.empty()) return device.device_index; // Existing automatic reconnect policy.
            if (selected >= 0) return -1; // Ambiguous identity cannot select an arbitrary controller.
            selected = device.device_index;
        }
    }
    return selected;
}

IoReconnectThrottle::IoReconnectThrottle(std::chrono::milliseconds retry_interval)
    : retry_interval_(retry_interval) {}

bool IoReconnectThrottle::should_attempt(std::chrono::steady_clock::time_point now) const {
    return retry_not_before_ == std::chrono::steady_clock::time_point{} ||
        now >= retry_not_before_;
}

void IoReconnectThrottle::record_failure(std::chrono::steady_clock::time_point now) {
    ++failure_count_;
    retry_not_before_ = now + retry_interval_;
}

void IoReconnectThrottle::record_success() {
    failure_count_ = 0;
    retry_not_before_ = std::chrono::steady_clock::time_point{};
}

unsigned int IoReconnectThrottle::failure_count() const {
    return failure_count_;
}

}  // namespace controller_native
