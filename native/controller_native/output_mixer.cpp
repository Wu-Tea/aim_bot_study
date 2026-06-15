#include "output_mixer.h"

namespace controller_native {

NativeControllerOutputComponents output_components_from_manual_output(
    const GamepadOutputState& output) {
    NativeControllerOutputComponents components;
    components.manual_stick = {output.right_x, output.right_y};
    components.final_stick = components.manual_stick;
    components.fire_button = output.rb || output.right_trigger > 0.04f;
    return components;
}

void capture_output_component_delta(
    const GamepadOutputState& before,
    const GamepadOutputState& after,
    common_native::Vec2f* component) {
    if (component == nullptr) {
        return;
    }
    *component = {
        after.right_x - before.right_x,
        after.right_y - before.right_y};
}

void capture_final_output_component(
    const GamepadOutputState& output,
    NativeControllerOutputComponents* components) {
    if (components == nullptr) {
        return;
    }
    components->final_stick = {output.right_x, output.right_y};
    components->fire_button = output.rb || output.right_trigger > 0.04f;
}

}  // namespace controller_native
