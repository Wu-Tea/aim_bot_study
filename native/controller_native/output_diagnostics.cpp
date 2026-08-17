#include "output_diagnostics.h"

namespace controller_native {

void capture_final_output_component(
    const GamepadOutputState& output,
    NativeControllerOutputComponents* components) {
    if (components == nullptr) return;
    components->final_stick = {output.right_x, output.right_y};
    components->fire_button = output.rb || output.right_trigger > 0.04f;
}

}  // namespace controller_native
