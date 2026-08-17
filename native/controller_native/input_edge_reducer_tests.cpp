#include "input_edge_reducer.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_edges_are_typed_unique_facts() {
    controller_native::InputEdgeReducer reducer;
    controller_native::PhysicalGamepadState physical{};
    std::uint64_t sequence = 1;
    auto sample = [&] {
        return reducer.sample(physical, false, &sequence);
    };

    const auto neutral = sample();
    require(!neutral.physical_ads_pressed && !neutral.manual_fire_pressed,
            "neutral sample emitted an edge");
    physical.left_trigger = 1.0f;
    physical.right_trigger = 1.0f;
    const auto rising = sample();
    require(rising.physical_ads_pressed && rising.manual_fire_pressed,
            "combined LT/fire onset lost an edge");
    const auto held = sample();
    require(!held.physical_ads_pressed && !held.manual_fire_pressed,
            "held inputs emitted duplicate edges");

    physical.left_trigger = 0.0f;
    physical.right_trigger = 0.0f;
    const auto fire_release = sample();
    require(fire_release.manual_fire_released &&
                !fire_release.physical_ads_released,
            "fire release should precede debounced LT release");
    require(!sample().physical_ads_released,
            "LT released before debounce completed");
    const auto ads_release = sample();
    require(ads_release.physical_ads_released,
            "debounced LT release event missing");
}

}  // namespace

int main() {
    test_edges_are_typed_unique_facts();
    return 0;
}
