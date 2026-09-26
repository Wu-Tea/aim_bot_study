#include "recoil_reducer.h"
#include "native_gamepad_controller.h"
#include "test_support/native_test_registry.h"

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

void test_hipfire_recoil_is_half_without_changing_ads_or_manual() {
    controller_native::GamepadRuntimeConfig config{};
    config.recoil.feedback_amount = 0.20f;
    config.recoil.hipfire_multiplier = 0.5f;
    config.auto_fire.enabled = false;
    double now = 1.0;
    controller_native::NativeGamepadController controller(config, &now);
    controller_native::PhysicalGamepadState physical{};
    physical.right_x = 0.4f;
    physical.right_y = 0.3f;
    for (int i = 0; i < 200; ++i) {
        const bool ads = i % 2 == 0;
        physical.left_trigger = ads ? 1.0f : 0.0f;
        physical.right_trigger = 1.0f;
        now += 0.001;
        const auto output = controller.build_output(physical);
        require(std::fabs(output.right_x - physical.right_x) < 1e-6f,
            "recoil multiplier changed horizontal manual output");
        require(std::fabs(output.right_y - (physical.right_y - (ads ? .2f : .1f))) < 1e-6f,
            "physical ADS must select full recoil; hipfire including manual-fire aim must select half");
    }
    physical.right_trigger = 0.0f;
    now += .001;
    const auto idle = controller.build_output(physical);
    require(std::fabs(idle.right_y - physical.right_y) < 1e-6f,
        "idle recoil changed manual sensitivity");

    for (const float requested : {0.0f, .14f, .2f, .34f, 1.0f}) {
        config.recoil.feedback_amount = requested;
        controller_native::RecoilReducer reducer(config.recoil);
        const auto ads = reducer.reduce(true, true, now);
        const auto hip = reducer.reduce(true, false, now);
        require(std::fabs(hip.stick_delta.y - ads.stick_delta.y * .5f) < 1e-6f,
            "hipfire scaling must follow the base clamp, not be raised back to the ADS minimum");
    }
}

}  // namespace

void register_recoil_reducer_tests(native_test::Registry& registry) {
    registry.add_case("FeatureRecoilAndWeapon", "hipfire_recoil_half_keeps_ads_and_manual", test_hipfire_recoil_is_half_without_changing_ads_or_manual);
    registry.add_case("FeatureRecoilAndWeapon", "recoil_reducer_accepts_only_fire_weapon_context", test_recoil_reducer_accepts_only_fire_weapon_context);
}
