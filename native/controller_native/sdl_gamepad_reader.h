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
    bool opened = false; // Accessible joystick with an SDL GameController mapping.
    // Independent of enumeration order. Prefer serial, then driver path;
    // name-only identity is accepted only when the matching device is unique.
    std::string id;
};

std::vector<SdlJoystickDevice> scan_sdl_joystick_devices();

class SdlGamepadReader {
public:
    explicit SdlGamepadReader(int device_index = 0, std::string required_id = {});
    ~SdlGamepadReader();

    SdlGamepadReader(const SdlGamepadReader&) = delete;
    SdlGamepadReader& operator=(const SdlGamepadReader&) = delete;

    bool available() const;
    bool attached() const;
    bool reconnect();
    int device_index() const;
    const std::string& device_name() const;
    PhysicalGamepadState read();

private:
    friend std::vector<SdlJoystickDevice> scan_sdl_joystick_devices();

    struct Backend;

    std::unique_ptr<Backend> backend_;
    int device_index_ = -1;
    std::string device_name_;
    std::string required_id_;
    int expected_axes_ = 0;
    int expected_buttons_ = 0;
};

}  // namespace controller_native
