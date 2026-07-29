#include "native_benchmark_physical_input.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::PhysicalGamepadState;
using controller_native::benchmark_adapter::apply_benchmark_physical_input;
using controller_native::sustained_aimlab::ControllerObservation;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_maps_full_scale_left_and_manual_right_sticks() {
    ControllerObservation input;
    input.left_x = -1.0;
    input.manual_stick = {0.25, -0.5};
    input.jump_action = true;
    input.slide_action = true;
    PhysicalGamepadState physical;
    apply_benchmark_physical_input(input, physical);
    require(physical.left_x == -1.0f,
            "benchmark input must preserve full-scale left_x");
    require(physical.left_y == 0.0f,
            "horizontal strafe benchmark must keep left_y neutral");
    require(physical.right_x == 0.25f && physical.right_y == -0.5f,
            "benchmark input must preserve manual right stick");
    require(physical.a && physical.b,
            "benchmark action cues must map to physical A and B");
}

void test_clamps_out_of_range_synthetic_input() {
    ControllerObservation input;
    input.left_x = 1.5;
    input.manual_stick = {-2.0, 2.0};
    PhysicalGamepadState physical;
    apply_benchmark_physical_input(input, physical);
    require(physical.left_x == 1.0f &&
                physical.right_x == -1.0f &&
                physical.right_y == 1.0f,
            "synthetic physical inputs must stay in unit range");
}

}  // namespace

int main() {
    try {
        test_maps_full_scale_left_and_manual_right_sticks();
        test_clamps_out_of_range_synthetic_input();
        std::cout << "native_benchmark_physical_input_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native_benchmark_physical_input_tests FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
