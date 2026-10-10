#include "touchpad_fire.h"
#include "sdl_touchpad_state.h"
#include "incident_fixture_support.h"
#include "ds4_output_report.h"
#include "output_composer.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
using namespace controller_native;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
PhysicalGamepadState touching() {
    PhysicalGamepadState input;
    input.connected = true;
    input.touchpad_fingers[0] = {true, .9f, .1f};
    return input;
}

void region_and_release() {
    TouchpadFire fire;
    auto input = touching();
    require(fire.update(input, 1).pressed, "touch immediately starts first pulse");
    require(!fire.update(input, 1.03).pressed, "pulse releases after 30 ms");
    require(fire.update(input, 1.05).pressed, "held touch starts next pulse after 50 ms");
    input.touchpad_fingers[0].active = false;
    require(!fire.update(input, 1.101).requested, "lifting cancels an in-flight pulse");
    input.touchpad = true;
    require(!fire.update(input, 1.102).requested, "click is not contact");
    input.touchpad_fingers[1] = {true, .75f, .6f};
    require(fire.update(input, 1.103).pressed, "either finger and exact region boundary work");
    for (const auto position : {TouchpadFingerState{true,.749f,.1f},
            {true,.9f,.601f}, {true,1.1f,.1f}, {true,.9f,-.1f},
            {true,std::numeric_limits<float>::quiet_NaN(),.1f}}) {
        input.touchpad_fingers[1] = position;
        require(!fire.update(input, 1.104).requested,
                "outside/invalid contact must cancel immediately");
    }
    input = touching();
    input.connected = false;
    require(!fire.update(input, 1.105).requested, "disconnect cancels stale contact");
    input.connected = true;
    require(fire.update(input, 1.106).pressed, "fresh request starts fresh pulse");
    fire.reset();
    require(fire.update(input, 1.11).pressed, "reset discards old pulse phase");
}

void touch_fire_1200_rpm_and_aim_scope() {
    GamepadRuntimeConfig config;
    config.auto_fire.enabled=false;
    config.auto_fire.manual_fire_activates_ai_aim=false;
    config.auto_fire.pulse_width_ms=5;
    config.auto_fire.pulse_period_ms=200; // Target AutoFire must not set the touch RPM.
    double now=10;
    NativeGamepadController controller(config,&now);
    auto input=touching();
    input.touchpad_fingers[0].y=.6f;
    int starts=0;bool previous=false;
    for (int tick=0;tick<1000;++tick) {
        now=10+tick*.001;
        const auto preparation=controller.begin_tick(input);
        require(pipeline_contract::requests_target_search(preparation.activation) &&
                pipeline_contract::permits_assist(preparation.activation),
                "held touch must activate search and aim before Vision, including fire pulse gaps");
        require(!preparation.scope.physical_ads_active && !preparation.scope.physical_ads_ready &&
                !preparation.scope.manual_fire_active,
                "touch aim must not forge physical L2 or manual fire input");
        require(preparation.scope.scope_epoch==1 && controller.ads_epoch()==1,
                "one held contact owns one scope and acquisition epoch");
        const auto out=controller.build_output_from_sampled_input();
        require(out.rb==(tick%50<30),"1200 RPM requires 30 ms down / 20 ms up regardless of AutoFire settings");
        starts+=out.rb && !previous;previous=out.rb;
    }
    require(starts==20,"one second must contain 20 touch-fire starts");
    input.touchpad_fingers[0].y=std::nextafter(.6f,1.f);
    now+=.001;
    const auto released=controller.begin_tick(input);
    require(released.activation==pipeline_contract::AssistActivation::Off && released.scope.scope_released,
            "crossing into the bottom 40 percent releases touch aim on the same tick");
    const auto out=controller.build_output_from_sampled_input();
    require(!out.rb && out.y,"lower contact double-taps triangle without touch fire or aim");
}

void touch_aim_target_and_physical_scope_handover() {
    for (bool light_l2 : {false,true}) {
        GamepadRuntimeConfig config;
        config.auto_fire.enabled=false;
        config.auto_fire.manual_fire_activates_ai_aim=false;
        config.recoil.enabled=true;
        double now=40;
        NativeGamepadController controller(config,&now);
        auto input=touching();input.touchpad_fingers={};
        input.left_trigger=light_l2 ? .1f : 0;
        incident_fixture::TargetSpec spec;
        spec.observation_id=25;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,1,now,40,-12));
        (void)controller.build_output(input);
        require(controller.ads_epoch()==0,"idle/light L2 must not start an ADS job");
        input.touchpad_fingers[0]={true,.9f,.5f};
        int gap_aim=0;
        std::uint64_t target=0;
        for (int tick=0;tick<200;++tick) {
            now=40.001+tick*.001;
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,tick+2,now,40,-12));
            const auto preparation=controller.begin_tick(input);
            require(preparation.scope.touch_fire_active && !preparation.scope.manual_fire_active &&
                    !preparation.scope.physical_ads_ready,"touch must be its own activation source");
            require(preparation.scope.touch_fire_pressed==(tick==0),"pulse gaps cannot generate contact edges");
            const auto out=controller.build_output_from_sampled_input();
            const auto& plan=controller.last_target_plan();
            require(plan.target_id!=0 && plan.aim_authority>0 && out.right_x>0,
                    "a valid off-center target must receive aim while touching, even without ready L2");
            if (tick==0) target=plan.target_id;
            require(plan.target_id==target && controller.ads_epoch()==1,"held touch must preserve identity and epoch");
            require(std::fabs(controller.last_output_components().recoil_stick.y)<1e-6f,
                    "enabling touch aim must not enable touch recoil");
            if (!out.rb && out.right_x>0) ++gap_aim;
        }
        require(gap_aim==80,"all four 20 ms release gaps must retain aim output");
        input.left_trigger=1;now+=.001;
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,250,now,40,-12));
        const auto combined=controller.begin_tick(input);
        require(combined.scope.physical_ads_ready && combined.scope.touch_fire_active && !combined.scope.scope_acquired,
                "physical L2 inside touch scope retains its own ready edge without a new scope");
        (void)controller.build_output_from_sampled_input();
        input.touchpad_fingers={};now+=.001;
        const auto ads_only=controller.begin_tick(input);
        require(ads_only.scope.touch_fire_released && !ads_only.scope.scope_released &&
                pipeline_contract::permits_assist(ads_only.activation),"touch release must preserve independently held ADS");
        (void)controller.build_output_from_sampled_input();
        input.left_trigger=0;
        for (int tick=0;tick<3;++tick) {
            now+=.001;const auto preparation=controller.begin_tick(input);
            if (tick==2) require(preparation.activation==pipeline_contract::AssistActivation::Off,
                                "the last held source must release aim");
            (void)controller.build_output_from_sampled_input();
        }

    }
}

void touch_macro_randomized_short_long_lifecycle() {
    std::uint64_t total_steps=0;
    for (unsigned seed : {20261010u,19072026u}) {
        std::mt19937 random(seed);
        for (int scenario=0;scenario<64;++scenario) {
            GamepadRuntimeConfig config;
            config.auto_fire.enabled=(scenario%2)==0;
            config.auto_fire.manual_fire_activates_ai_aim=(scenario%3)==0;
            config.auto_fire.fire_output=(scenario%2)==0 ? "RB" : "RT";
            config.auto_fire.pulse_period_ms=100+random()%401;
            config.auto_fire.pulse_width_ms=1+random()%20;
            config.recoil.enabled=true;
            double now=60;
            NativeGamepadController controller(config,&now);
            auto input=touching();
            incident_fixture::TargetSpec spec;
            spec.observation_id=31;spec.selector_generation=1;
            spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
            const int duration=(scenario%2)==0 ? 250 : 5000;
            int remaining=0;bool contact=false;int phase=-1;
            for (int tick=0;tick<duration;++tick) {
                now=60+tick*.001;
                if (remaining--<=0) {
                    remaining=10+random()%191;
                    input.connected=random()%10!=0;
                    input.touchpad_fingers={};
                    const int position=random()%5;
                    if (position==0) input.touchpad_fingers[0]={true,.9f,.6f};
                    if (position==1) input.touchpad_fingers[1]={true,.75f,.4f};
                    if (position==2) input.touchpad_fingers[0]={true,.9f,.601f};
                    if (position==3) input.touchpad_fingers[0]={true,.74f,.1f};
                    input.right_x=(static_cast<int>(random()%11)-5)*.001f;
                    input.right_y=(static_cast<int>(random()%11)-5)*.001f;
                }
                const bool upper=input.connected &&
                    ((input.touchpad_fingers[0].active && input.touchpad_fingers[0].x>=.75f && input.touchpad_fingers[0].y<=.6f) ||
                     (input.touchpad_fingers[1].active && input.touchpad_fingers[1].x>=.75f && input.touchpad_fingers[1].y<=.6f));
                phase=upper ? (contact ? (phase+1)%50 : 0) : -1;
                if (tick%8==0) controller.submit_vision_snapshot(
                    incident_fixture::observed_snapshot(spec,tick+1,now,20+random()%21,-10));
                const auto preparation=controller.begin_tick(input);
                require(preparation.scope.touch_fire_active==upper &&
                        pipeline_contract::permits_assist(preparation.activation)==upper,
                        "randomized contact admission must own aim independently of pulse phase and AutoFire");
                require(preparation.scope.touch_fire_pressed==(upper && !contact) &&
                        preparation.scope.touch_fire_released==(!upper && contact),"random contact transitions must emit one edge");
                const auto out=controller.build_output_from_sampled_input();
                const bool pulse=config.auto_fire.fire_output=="RB" ? out.rb : out.right_trigger>.5f;
                require(pulse==(upper && phase<30),"random short/long holds must follow independent 1200 RPM pulses");
                require(std::isfinite(out.right_x) && std::isfinite(out.right_y) &&
                        std::hypot(out.right_x,out.right_y)<=1.00001f,"aim output must remain finite and bounded");
                require(std::fabs(controller.last_output_components().recoil_stick.y)<1e-6f,
                        "random touch shots must not add recoil");
                if (!upper && input.connected) require(out.right_x==input.right_x && out.right_y==input.right_y,
                        "released/lower/outside touch must return exact physical sticks");
                contact=upper;++total_steps;
            }
            controller.reset();input=touching();input.connected=false;now+=.1;
            const auto preparation=controller.begin_tick(input);
            require(preparation.activation==pipeline_contract::AssistActivation::Off,"reset/disconnect must discard touch lease");
            const auto out=controller.build_output_from_sampled_input();
            require(!out.rb && out.right_trigger==0 && !out.y,"reset/disconnect cannot replay a macro");
        }
    }
    require(total_steps==336000,"both independent seeds and all short/long scenarios must execute");
}

void sdl_contact_lifecycle() {
    struct Sample {
        int status = 0;
        std::uint8_t active = 1;
        float x = .9f, y = .1f;
    } samples[2];
    const auto read = +[](void* data, int pad, int finger, std::uint8_t* active,
                         float* x, float* y, float* pressure) -> int {
        require(pad == 0 && finger >= 0 && finger < 2, "query correct pad/finger slots");
        const auto& value = static_cast<Sample*>(data)[finger];
        *active = value.active; *x = value.x; *y = value.y; *pressure = 0;
        return value.status;
    };
    auto input = touching();
    input.touchpad_fingers = read_sdl_touchpad_fingers(samples, 2, read);
    TouchpadFire fire;
    require(input.touchpad_fingers[1].active && fire.update(input, 1).pressed,
            "SDL contacts flow into fire without requiring pressure or click");
    samples[0].active = 0; samples[1].status = -1;
    input.touchpad_fingers = read_sdl_touchpad_fingers(samples, 2, read);
    require(!fire.update(input, 1.001).requested,
            "release and failed read cannot retain the preceding touch");
    samples[0].active = 1; samples[0].x = std::numeric_limits<float>::infinity();
    require(!read_sdl_touchpad_fingers(samples, 2, read)[0].active, "reject invalid SDL coordinates");
    require(!read_sdl_touchpad_fingers(nullptr, 2, read)[0].active &&
            !read_sdl_touchpad_fingers(samples, 2, nullptr)[0].active,
            "missing controller or optional touch API produces no contact");
}

void native_hold_fire_without_recoil() {
    for (const auto output_button : {"RB", "RT"}) {
      for (float contact_y : {.1f, .6f}) {
        GamepadRuntimeConfig config;
        config.auto_fire.enabled = false; // Human touch does not require Vision AutoFire.
        config.auto_fire.fire_output = output_button;
        config.recoil.enabled = true;
        double now = 10;
        NativeGamepadController controller(config, &now);
        auto input = touching();
        input.touchpad_fingers[0].y = contact_y;
        input.left_x = .21f; input.right_x = .013f; input.right_y = -.023f;
        input.a = input.touchpad = true;
        int rising_edges = 0;
        bool previous = false;
        for (int tick = 0; tick < 300; ++tick) {
            now = 10 + tick * .001;
            const auto out = controller.build_output(input);
            const bool expected = tick % 50 < 30;
            const bool pressed = config.auto_fire.fire_output == "RB" ? out.rb : out.right_trigger > .5f;
            require(pressed == expected, "native output must alternate 30 ms down / 20 ms up");
            require(out.right_x == input.right_x && out.right_y == input.right_y,
                    "without a target, touch aim must preserve manual input and add no recoil");
            require(out.left_x == input.left_x && out.a && out.touchpad,
                    "other physical input must remain unchanged");
            const auto report = to_ds4_report(out);
            require(config.auto_fire.fire_output == "RB"
                        ? bool(report.bytes[5] & 2) == expected
                        : (report.bytes[8] == (expected ? 255 : 0)),
                    "correct DS4 R1/R2 signal must reach the report");
            rising_edges += pressed && !previous;
            previous = pressed;
        }
        require(rising_edges == 6, "six held-touch cycles must execute in 300 ms");
        now = 10.3;
        require(config.auto_fire.fire_output == "RB" ? controller.build_output(input).rb
                    : controller.build_output(input).right_trigger > .5f, "seventh pulse starts");
        input.touchpad_fingers = {};
        now += .001;
        const auto released = controller.build_output(input);
        require(!released.rb && released.right_trigger == 0, "release cancels current pulse next tick");
        input.rb = true;
        now += .001;
        const auto manual = controller.build_output(input);
        require(manual.rb && manual.right_y < input.right_y - .05f,
                "ordinary manual fire retains existing recoil behavior");
        controller.reset();
        input = touching(); input.connected = false;
        const auto disconnected = controller.build_output(input);
        require(!disconnected.rb && disconnected.right_trigger == 0, "disconnected touch cannot fire");
        config.ai_aim.adapter_direct_mouse_manual = true;
        NativeGamepadController mouse(config, &now);
        input.connected = true;
        const auto ignored = mouse.build_output(input);
        require(!ignored.rb && ignored.right_trigger == 0, "mouse adapter does not consume gamepad touches");
      }
    }
}

void touch_owns_target_fire_gaps() {
    GamepadRuntimeConfig config;
    config.auto_fire.enabled = true;
    config.auto_fire.require_aim_ready = false;
    config.recoil.enabled = true;
    double now = 20;
    NativeGamepadController controller(config, &now);
    incident_fixture::TargetSpec spec;
    spec.observation_id = 20; spec.selector_generation = 1;
    spec.has_enemy_cue = spec.enemy_identity_confirmed = spec.fire_authority = true;
    auto input = touching(); input.left_trigger = 1;
    for (int tick = 0; tick < 150; ++tick) {
        now = 20 + tick * .001;
        auto snapshot = incident_fixture::observed_snapshot(spec, tick + 1, now, 0, 0);
        snapshot.state.auto_fire_requested = true;
        controller.submit_vision_snapshot(snapshot);
        const auto out = controller.build_output(input);
        require(out.rb == (tick % 50 < 30), "target AutoFire must not fill touch pulse gaps");
        require(std::fabs(controller.last_output_components().recoil_stick.y) < 1e-6f,
                "ADS and a target cannot enable recoil on touch fire");
    }
    require(controller.last_target_plan().target_id != 0,
            "overlap fixture must actually admit a target");
    input.touchpad_fingers = {};
    now += .001;
    auto snapshot = incident_fixture::observed_snapshot(spec, 200, now, 0, 0);
    snapshot.state.auto_fire_requested = true;
    controller.submit_vision_snapshot(snapshot);
    (void)controller.build_output(input);
    require(controller.last_output_components().auto_fire_active,
            "negative control: the same target can independently request AutoFire after touch release");
}

void triangle_double_tap_native() {
    for (bool lift_immediately : {false, true}) {
        GamepadRuntimeConfig config;
        config.auto_fire.enabled = false;
        config.recoil.enabled = true;
        double now = 30;
        NativeGamepadController controller(config, &now);
        auto input = touching();
        input.touchpad_fingers[0].y = .8f;
        input.right_x = .017f; input.right_y = -.021f;
        input.a = true;
        int presses = 0;
        bool previous = false;
        for (int tick = 0; tick < 300; ++tick) {
            now = 30 + tick * .001;
            if (lift_immediately && tick == 1) input.touchpad_fingers = {};
            const auto out = controller.build_output(input);
            const bool expected = tick < 20 || (tick >= 40 && tick < 60);
            require(out.y == expected, "triangle must follow down20/up20/down20/up timeline");
            require(bool(to_ds4_report(out).bytes[4] & 0x80) == expected,
                    "both triangle pulses and releases must reach DS4 report");
            require(!out.rb && out.right_trigger == 0 && out.a &&
                    out.right_x == input.right_x && out.right_y == input.right_y,
                    "triangle gesture must not activate fire, aim or recoil");
            presses += out.y && !previous;
            previous = out.y;
        }
        require(presses == 2, "quick release completes two clicks; holding never repeats them");
        input.touchpad_fingers = {};
        now += .001; (void)controller.build_output(input);
        input.touchpad_fingers[1] = {true, .9f, .8f};
        now += .001;
        require(controller.build_output(input).y, "new lower contact starts another double tap");
        input.connected = false;
        now += .001;
        require(!controller.build_output(input).y, "disconnect cancels pending triangle gesture");
        input.connected = true; input.touchpad_fingers = {};
        now += .1;
        require(!controller.build_output(input).y, "reconnect without touch cannot replay the second click");
        input.y = true;
        require(controller.build_output(input).y, "physical triangle remains native passthrough");
    }
}

void touch_regions_and_triangle_lifecycle() {
    TouchpadFire fire;
    TouchpadTriangle triangle;
    auto input = touching();
    for (float y : {0.f, .25f, .49f, .5f, .501f, .599f, .6f}) {
        input.touchpad_fingers[0].y = y;
        require(fire.update(input, 1).requested && !triangle.update(input, 1),
                "top 60 percent including split line belongs only to fire");
    }
    input.touchpad_fingers[0].y = std::nextafter(.6f,1.f);
    require(!fire.update(input, 1).requested && triangle.update(input, 1),
            "bottom 40 percent belongs only to triangle");
    input.touchpad_fingers[1] = input.touchpad_fingers[0];
    require(!triangle.update(input, 1.020), "second finger must not restart first press");
    input.touchpad_fingers[0].active = false;
    require(!triangle.update(input, 1.039), "remaining lower finger must not retrigger");
    require(triangle.update(input, 1.040), "second pulse follows full release interval");
    require(!triangle.update(input, 1.060) && !triangle.update(input, 2),
            "held lower contact stays idle after one double tap");
    triangle.reset();
    input.touchpad_fingers = {};
    input.touchpad = true;
    require(!triangle.update(input, 3), "mechanical click alone cannot double tap triangle");
    input.touchpad_fingers[0] = {true,.9f,.8f};
    require(triangle.update(input, 3), "start timing stall fixture");
    require(!triangle.update(input, 3.1) && !triangle.update(input, 3.119),
            "late tick must publish a release, not collapse both clicks");
    require(triangle.update(input, 3.120), "second click waits 20 ms after delivered release");
    triangle.reset();
    input.touchpad_fingers = {};
    require(!triangle.update(input, 3.121), "reset cancels pending second click");
    input.touchpad_fingers[0] = {true,.749f,.8f};
    require(!triangle.update(input, 3.122), "lower width remains rightmost 25 percent");
    input.touchpad_fingers[0] = {true,.9f,std::numeric_limits<float>::quiet_NaN()};
    require(!triangle.update(input, 3.123), "invalid lower coordinate cannot start gesture");

    // Literal expected boundaries are independent of the shared production constant.
    // Sweep both axes, including out-of-range contacts; restart gesture state each time.
    for (int xi = -1; xi <= 41; ++xi) {
        for (int yi = -1; yi <= 41; ++yi) {
            const float x = xi / 40.f, y = yi / 40.f;
            fire.reset(); triangle.reset();
            input.touchpad_fingers = {};
            input.touchpad_fingers[0] = {true, x, y};
            const bool valid = x >= .75f && x <= 1 && y >= 0 && y <= 1;
            require(fire.update(input, 4).requested == (valid && y <= .6f),
                    "grid contacts must match the top 60 percent fire region");
            require(triangle.update(input, 4) == (valid && y > .6f),
                    "grid contacts must match the remaining triangle region");
        }
    }
}

void auxiliary_triangle_output_contract() {
    auto physical = touching();
    auto frame = ControlFrame::begin(physical,
        pipeline_contract::ControllerTickId::from(1),
        pipeline_contract::EventSequence::from(1));
    OutputComposer composer;
    frame.auxiliary_buttons().triangle = true;
    require(composer.compose(frame) == OutputComposeStatus::InvalidInput,
            "synthetic triangle must have an authorized command header");
    auto& header = frame.auxiliary_buttons().header;
    header.controller_tick = frame.controller_tick();
    header.sequence = frame.sample_sequence();
    frame.auxiliary_dpad().header = header;
    frame.auxiliary_dpad().up = true;
    require(composer.compose(frame) == OutputComposeStatus::Ok &&
            composer.finalized_output()->y && composer.finalized_output()->dpad_up,
            "triangle and existing auxiliary dpad must compose together");
    require(composer.merge_auxiliary_actions({}, frame.auxiliary_buttons()) ==
                OutputComposeStatus::AlreadyFinalized,
            "auxiliary buttons cannot mutate finalized output");
    GamepadRuntimeConfig config;
    config.ai_aim.adapter_direct_mouse_manual = true;
    double now = 10;
    NativeGamepadController mouse(config, &now);
    physical.touchpad_fingers[0].y = .8f;
    require(!mouse.build_output(physical).y, "mouse mode ignores lower touch action");
}
} // namespace

void register_touchpad_fire_tests(native_test::Registry& registry) {
    registry.add_case("BaseEndToEnd", "touch_fire_1200_rpm_and_aim_scope", touch_fire_1200_rpm_and_aim_scope);
    registry.add_case("BaseEndToEnd", "touch_aim_target_physical_scope_handover", touch_aim_target_and_physical_scope_handover);
    registry.add_case("BaseEndToEnd", "touch_macro_randomized_short_long_lifecycle", touch_macro_randomized_short_long_lifecycle);
    registry.add_case("BaseEndToEnd", "touch_triangle_double_tap_native", triangle_double_tap_native);
    registry.add_case("BaseEndToEnd", "touch_regions_triangle_lifecycle", touch_regions_and_triangle_lifecycle);
    registry.add_case("BaseContracts", "auxiliary_triangle_output_contract", auxiliary_triangle_output_contract);
    registry.add_case("BaseEndToEnd", "touch_fire_sdl_contact_lifecycle", sdl_contact_lifecycle);
    registry.add_case("BaseEndToEnd", "touch_fire_region_release", region_and_release);
    registry.add_case("BaseEndToEnd", "touch_fire_native_no_recoil", native_hold_fire_without_recoil);
    registry.add_case("BaseEndToEnd", "touch_fire_owns_target_gaps", touch_owns_target_fire_gaps);
}
