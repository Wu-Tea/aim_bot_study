#pragma once

#include "xinput_reader.h"

#include <memory>
#include <string>
#include <vector>

namespace controller_native {

struct SdlJoystickDevice {
    int device_index = -1;
    std::string name;
    int axes = 0;
    int buttons = 0;
    int hats = 0;
    bool opened = false;
};

std::vector<SdlJoystickDevice> scan_sdl_joystick_devices();

class SdlGamepadReader {
public:
    explicit SdlGamepadReader(int device_index = 0);
    ~SdlGamepadReader();

    SdlGamepadReader(const SdlGamepadReader&) = delete;
    SdlGamepadReader& operator=(const SdlGamepadReader&) = delete;

    bool available() const;
    int device_index() const;
    const std::string& device_name() const;
    PhysicalGamepadState read();

private:
    friend std::vector<SdlJoystickDevice> scan_sdl_joystick_devices();

    struct Backend;

    std::unique_ptr<Backend> backend_;
    int device_index_ = -1;
    std::string device_name_;
    bool trigger_initialized_ = false;
};

}  // namespace controller_native
