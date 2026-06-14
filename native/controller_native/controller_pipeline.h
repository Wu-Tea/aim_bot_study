#pragma once

#include "virtual_gamepad.h"
#include "xinput_reader.h"

namespace controller_native {

GamepadOutputState output_from_physical_input(const PhysicalGamepadState& physical);

}  // namespace controller_native
