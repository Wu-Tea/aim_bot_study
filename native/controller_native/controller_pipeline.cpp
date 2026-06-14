#include "controller_pipeline.h"

namespace controller_native {

GamepadOutputState output_from_physical_input(const PhysicalGamepadState& physical) {
    GamepadOutputState output;
    output.left_x = physical.left_x;
    output.left_y = physical.left_y;
    output.right_x = physical.right_x;
    output.right_y = physical.right_y;
    output.left_trigger = physical.left_trigger;
    output.right_trigger = physical.right_trigger;
    output.rb = physical.rb;
    output.lb = physical.lb;
    output.a = physical.a;
    output.b = physical.b;
    output.x = physical.x;
    output.y = physical.y;
    output.back = physical.back;
    output.guide = physical.guide;
    output.start = physical.start;
    output.left_thumb = physical.left_thumb;
    output.right_thumb = physical.right_thumb;
    output.dpad_up = physical.dpad_up;
    output.dpad_down = physical.dpad_down;
    output.dpad_left = physical.dpad_left;
    output.dpad_right = physical.dpad_right;
    return output;
}

}  // namespace controller_native
