#include "recoil_reducer.h"

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_recoil_reducer_accepts_only_fire_weapon_context() {
    controller_native::GamepadRecoilConfig config{};
    config.profile_playback_enabled = false;
    config.feedback_amount = 0.20f;
    config.feedback_min_amount = 0.14f;
    config.feedback_max_amount = 0.34f;
    controller_native::RecoilReducer reducer(config);
    const auto idle = reducer.reduce(false, true, 1.0);
    require(!idle.active, "recoil activated without effective fire");
    const auto firing = reducer.reduce(
        true,
        true,
        1.001,
        pipeline_contract::EventSequence::from(3));
    require(firing.active &&
                std::isfinite(firing.stick_delta.x) &&
                std::isfinite(firing.stick_delta.y),
            "effective fire did not produce an independent contribution");
    require(firing.cause_event == pipeline_contract::EventSequence::from(3),
            "recoil contribution lost causal identity");
}

}  // namespace

int main() {
    test_recoil_reducer_accepts_only_fire_weapon_context();
    return 0;
}
