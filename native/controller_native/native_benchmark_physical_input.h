#pragma once

#include "native_gamepad_controller.h"
#include "sustained_aimlab_simulator.h"

#include <algorithm>

namespace controller_native::benchmark_adapter {

inline void apply_benchmark_physical_input(
    const sustained_aimlab::ControllerObservation& input,
    PhysicalGamepadState& physical) noexcept {
    physical.left_x = static_cast<float>(
        std::clamp(input.left_x, -1.0, 1.0));
    physical.left_y = 0.0f;
    physical.right_x = static_cast<float>(
        std::clamp(input.manual_stick.x, -1.0, 1.0));
    physical.right_y = static_cast<float>(
        std::clamp(input.manual_stick.y, -1.0, 1.0));
    physical.a = input.jump_action;
}

}  // namespace controller_native::benchmark_adapter
