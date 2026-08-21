#include "aim_scope_reducer.h"
#include "test_support/native_test_registry.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

controller_native::PhysicalGamepadState neutral() {
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    return physical;
}

struct ScopeHarness {
    controller_native::InputEdgeReducer input_edges;
    controller_native::AimScopeReducer scope;
    std::uint64_t sequence = 1;

    controller_native::AimScopeSnapshot update(
        const controller_native::PhysicalGamepadState& physical,
        bool rb_counts_as_aiming,
        bool manual_fire_activates_aim) {
        const auto edges = input_edges.sample(
            physical,
            rb_counts_as_aiming,
            &sequence);
        return scope.reduce(edges, manual_fire_activates_aim);
    }
};

void test_physical_ads_edges_are_debounced_and_unique() {
    ScopeHarness reducer;
    auto physical = neutral();
    require(!reducer.update(physical, false, true).assist_active,
            "neutral sample acquired aim scope");

    physical.left_trigger = 0.8f;
    const auto pressed = reducer.update(physical, false, true);
    require(pressed.physical_ads_pressed && pressed.scope_acquired,
            "LT rising edge was not emitted");
    require(pressed.physical_ads_epoch == 1 && pressed.scope_epoch == 1,
            "LT rising edge did not advance epochs once");
    const auto held = reducer.update(physical, false, true);
    require(!held.physical_ads_pressed && held.physical_ads_epoch == 1,
            "held LT emitted a duplicate edge");

    physical.left_trigger = 0.0f;
    require(reducer.update(physical, false, true).physical_ads_active,
            "first idle sample released LT");
    require(reducer.update(physical, false, true).physical_ads_active,
            "second idle sample released LT");
    const auto released = reducer.update(physical, false, true);
    require(released.physical_ads_released && released.scope_released,
            "third idle sample did not emit release");
}

void test_manual_fire_scope_is_target_independent_input_state() {
    ScopeHarness reducer;
    auto physical = neutral();
    physical.right_trigger = 1.0f;
    const auto pressed = reducer.update(physical, false, true);
    require(pressed.manual_fire_pressed && pressed.manual_fire_active,
            "manual fire did not acquire aim scope");
    require(pressed.scope_acquired && pressed.manual_fire_epoch == 1,
            "manual fire did not advance its edge identity");
    const auto held = reducer.update(physical, false, true);
    require(!held.manual_fire_pressed && held.manual_fire_epoch == 1,
            "held fire emitted a duplicate edge");
    physical.right_trigger = 0.0f;
    const auto released = reducer.update(physical, false, true);
    require(released.manual_fire_released && released.scope_released,
            "manual fire release did not release its scope");

    physical.right_trigger = 1.0f;
    const auto disabled = reducer.update(physical, false, false);
    require(!disabled.manual_fire_active && !disabled.assist_active,
            "disabled fire-to-aim created authority");
}

void test_fire_then_lt_preserves_the_physical_ads_event() {
    ScopeHarness reducer;
    auto physical = neutral();
    physical.right_trigger = 1.0f;
    const auto fire = reducer.update(physical, false, true);
    require(fire.scope_acquired && fire.scope_epoch == 1,
            "fire did not acquire the initial scope");

    physical.left_trigger = 0.8f;
    const auto ads = reducer.update(physical, false, true);
    require(ads.physical_ads_pressed,
            "LT edge was hidden by an already-active fire scope");
    require(!ads.scope_acquired && ads.scope_epoch == 1,
            "LT inside fire scope fabricated a second scope acquisition");
    require(ads.physical_ads_epoch == 1 &&
                ads.source == controller_native::AimScopeSource::PhysicalAdsAndManualFire,
            "combined scope identity is wrong");
}

void test_lt_then_fire_preserves_the_manual_fire_event() {
    ScopeHarness reducer;
    auto physical = neutral();
    physical.left_trigger = 0.8f;
    (void)reducer.update(physical, false, true);
    physical.right_trigger = 1.0f;
    const auto fire = reducer.update(physical, false, true);
    require(fire.manual_fire_pressed,
            "manual-fire edge was hidden by an already-active ADS scope");
    require(!fire.scope_acquired && fire.scope_epoch == 1,
            "fire inside ADS scope fabricated a second scope acquisition");
}

}  // namespace

void register_aim_scope_reducer_tests(native_test::Registry& registry) {
    registry.add_case("BaseAds", "physical_ads_edges_are_debounced_and_unique", test_physical_ads_edges_are_debounced_and_unique);
    registry.add_case("BaseAds", "manual_fire_scope_is_target_independent", test_manual_fire_scope_is_target_independent_input_state);
    registry.add_case("BaseAds", "fire_then_lt_preserves_physical_ads_event", test_fire_then_lt_preserves_the_physical_ads_event);
    registry.add_case("BaseAds", "lt_then_fire_preserves_manual_fire_event", test_lt_then_fire_preserves_the_manual_fire_event);
}
