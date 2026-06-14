#pragma once

#include "../common_native/screen_geometry.h"

#include "virtual_gamepad.h"

namespace controller_native {

struct NativeControllerOutputComponents {
    common_native::Vec2f manual_stick;
    common_native::Vec2f ai_aim_stick;
    common_native::Vec2f dynamic_adjustment_stick;
    common_native::Vec2f recoil_stick;
    common_native::Vec2f final_stick;
    bool fire_button = false;
};

NativeControllerOutputComponents output_components_from_manual_output(
    const GamepadOutputState& output);

void capture_output_component_delta(
    const GamepadOutputState& before,
    const GamepadOutputState& after,
    common_native::Vec2f* component);

void capture_final_output_component(
    const GamepadOutputState& output,
    NativeControllerOutputComponents* components);

}  // namespace controller_native
