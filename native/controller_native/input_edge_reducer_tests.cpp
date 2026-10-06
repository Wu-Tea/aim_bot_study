#include "input_edge_reducer.h"
#include "native_gamepad_controller.h"
#include "test_support/native_test_registry.h"

#include <stdexcept>
#include <random>
#include <cmath>

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

void register_input_edge_reducer_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "configurable_l2_threshold_randomized_lifecycle", [] {
        using namespace controller_native;
        for (auto seed : {4511u, 19871u}) {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> unit(0,1);
            for (int scenario=0;scenario<60;++scenario) {
                GamepadRuntimeConfig config;
                auto thresholds=[&] {
                    config.ai_aim.ads_activation_trigger=unit(rng)*.65f;
                    config.ai_aim.ads_scope_ready_trigger=config.ai_aim.ads_activation_trigger+.02f+
                        unit(rng)*(.98f-config.ai_aim.ads_activation_trigger);
                };
                thresholds();
                double now=10;
                NativeGamepadController controller(config,&now);
                PhysicalGamepadState physical{}; physical.connected=true;
                bool active=false,ready=false;
                unsigned idle=0;
                std::uint64_t epoch=0;
                for(int tick=0;tick<(scenario%2?200:2500);++tick) {
                    if(tick && tick%61==0) {thresholds();controller.apply_hot_config(config,true);}
                    const float begin=config.ai_aim.ads_activation_trigger,full=config.ai_aim.ads_scope_ready_trigger;
                    const float release=std::max(0.f,begin-.02f);
                    const float points[]={0,begin,std::nextafter(begin,1.f),full,1,release};
                    physical.left_trigger=tick%10<6?points[tick%10]:unit(rng);
                    physical.right_x=unit(rng)*2-1; physical.right_y=unit(rng)*2-1;
                    const bool before=active;
                    if(physical.left_trigger<=release) {
                        if(active && ++idle>=3) {active=false;idle=0;}
                    } else {
                        idle=0;
                        if(!active && physical.left_trigger>begin)active=true;
                    }
                    if(!active)ready=false;
                    else if(ready)ready=physical.left_trigger>=std::max(begin,full-.05f);
                    else ready=physical.left_trigger>=full;
                    if(active && !before)++epoch;
                    now+=.001;
                    const auto prepared=controller.begin_tick(physical);
                    require(prepared.scope.physical_ads_active==active && prepared.scope.physical_ads_ready==ready,
                            "configured L2 threshold must own activation and readiness without a second gate");
                    require(prepared.scope.physical_ads_epoch==epoch,
                            "only actual activation edges may start new physical ADS epochs");
                    const auto output=controller.build_output_from_sampled_input();
                    require(output.left_trigger==physical.left_trigger && output.right_x==physical.right_x && output.right_y==physical.right_y,
                            "AI activation settings must preserve physical trigger and stick passthrough");
                }
            }
        }
    });
    registry.add_case("BaseRuntimeFreshness", "input_edges_are_typed_unique_facts", test_edges_are_typed_unique_facts);
}
