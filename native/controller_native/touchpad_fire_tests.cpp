#include "touchpad_fire.h"
#include "sdl_touchpad_state.h"
#include "incident_fixture_support.h"
#include "ds4_output_report.h"
#include "output_composer.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <limits>
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
    require(fire.update(input, 1, 30, 100).pressed, "touch immediately starts first pulse");
    require(!fire.update(input, 1.03, 30, 100).pressed, "pulse releases after 30 ms");
    require(fire.update(input, 1.1, 30, 100).pressed, "held touch starts next pulse");
    input.touchpad_fingers[0].active = false;
    require(!fire.update(input, 1.101, 30, 100).requested, "lifting cancels an in-flight pulse");
    input.touchpad = true;
    require(!fire.update(input, 1.102, 30, 100).requested, "click is not contact");
    input.touchpad_fingers[1] = {true, .75f, .5f};
    require(fire.update(input, 1.103, 30, 100).pressed, "either finger and exact region boundary work");
    for (const auto position : {TouchpadFingerState{true,.749f,.1f},
            {true,.9f,.501f}, {true,1.1f,.1f}, {true,.9f,-.1f},
            {true,std::numeric_limits<float>::quiet_NaN(),.1f}}) {
        input.touchpad_fingers[1] = position;
        require(!fire.update(input, 1.104, 30, 100).requested,
                "outside/invalid contact must cancel immediately");
    }
    input = touching();
    input.connected = false;
    require(!fire.update(input, 1.105, 30, 100).requested, "disconnect cancels stale contact");
    input.connected = true;
    require(fire.update(input, 1.106, 30, 100).pressed, "fresh request starts fresh pulse");
    fire.reset();
    require(fire.update(input, 1.11, 30, 100).pressed, "reset discards old pulse phase");
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
    require(input.touchpad_fingers[1].active && fire.update(input, 1, 30, 100).pressed,
            "SDL contacts flow into fire without requiring pressure or click");
    samples[0].active = 0; samples[1].status = -1;
    input.touchpad_fingers = read_sdl_touchpad_fingers(samples, 2, read);
    require(!fire.update(input, 1.001, 30, 100).requested,
            "release and failed read cannot retain the preceding touch");
    samples[0].active = 1; samples[0].x = std::numeric_limits<float>::infinity();
    require(!read_sdl_touchpad_fingers(samples, 2, read)[0].active, "reject invalid SDL coordinates");
    require(!read_sdl_touchpad_fingers(nullptr, 2, read)[0].active &&
            !read_sdl_touchpad_fingers(samples, 2, nullptr)[0].active,
            "missing controller or optional touch API produces no contact");
}

void native_hold_fire_without_recoil() {
    for (const auto output_button : {"RB", "RT"}) {
        GamepadRuntimeConfig config;
        config.auto_fire.enabled = false; // Human touch does not require Vision AutoFire.
        config.auto_fire.fire_output = output_button;
        config.recoil.enabled = true;
        double now = 10;
        NativeGamepadController controller(config, &now);
        auto input = touching();
        input.left_x = .21f; input.right_x = .013f; input.right_y = -.023f;
        input.a = input.touchpad = true;
        int rising_edges = 0;
        bool previous = false;
        for (int tick = 0; tick < 300; ++tick) {
            now = 10 + tick * .001;
            const auto out = controller.build_output(input);
            const bool expected = tick % 100 < 30;
            const bool pressed = config.auto_fire.fire_output == "RB" ? out.rb : out.right_trigger > .5f;
            require(pressed == expected, "native output must alternate 30 ms down / 70 ms up");
            require(out.right_x == input.right_x && out.right_y == input.right_y,
                    "touch fire must not activate aim or recoil, including gaps");
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
        require(rising_edges == 3, "three held-touch cycles must actually execute");
        now = 10.3;
        require(config.auto_fire.fire_output == "RB" ? controller.build_output(input).rb
                    : controller.build_output(input).right_trigger > .5f, "fourth pulse starts");
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
        require(out.rb == (tick % 100 < 30), "target AutoFire must not fill touch pulse gaps");
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
    for (float y : {.25f, .49f, .5f}) {
        input.touchpad_fingers[0].y = y;
        require(fire.update(input, 1, 30, 100).requested && !triangle.update(input, 1),
                "expanded upper half including center line belongs only to fire");
    }
    input.touchpad_fingers[0].y = .501f;
    require(!fire.update(input, 1, 30, 100).requested && triangle.update(input, 1),
            "lower half belongs only to triangle");
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
    registry.add_case("BaseEndToEnd", "touch_triangle_double_tap_native", triangle_double_tap_native);
    registry.add_case("BaseEndToEnd", "touch_regions_triangle_lifecycle", touch_regions_and_triangle_lifecycle);
    registry.add_case("BaseContracts", "auxiliary_triangle_output_contract", auxiliary_triangle_output_contract);
    registry.add_case("BaseEndToEnd", "touch_fire_sdl_contact_lifecycle", sdl_contact_lifecycle);
    registry.add_case("BaseEndToEnd", "touch_fire_region_release", region_and_release);
    registry.add_case("BaseEndToEnd", "touch_fire_native_no_recoil", native_hold_fire_without_recoil);
    registry.add_case("BaseEndToEnd", "touch_fire_owns_target_gaps", touch_owns_target_fire_gaps);
}
