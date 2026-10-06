#include "intent_filter.h"
#include "assist_control_state_machine.h"
#include "native_gamepad_controller.h"
#include "ds4_output_report.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <random>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
}

void test_deadzone_sized_drift_is_neutral() {
    controller_native::IntentFilter filter;
    pipeline_contract::IntentState state{};
    for (int i = 0; i < 200; ++i) {
        state = filter.update({0.0f, 0.0f}, {-0.0118f, 0.004f}, false, false, i * 0.001);
    }
    require_true(state.right_phase == pipeline_contract::StickPhase::Neutral,
                 "deadzone drift must remain neutral");
    require_near(state.filtered_right.x, 0.0f, 0.002f,
                 "deadzone drift must not become correction intent");
    require_near(state.raw_right.x, -0.0118f, 0.00001f,
                 "intent filtering must preserve raw input");
}

void test_sustained_input_is_not_learned_away() {
    controller_native::IntentFilter filter;
    pipeline_contract::IntentState state{};
    for (int i = 0; i < 20; ++i) {
        state = filter.update({0.0f, 0.0f}, {0.7f, 0.0f}, true, false, i * 0.01);
    }
    require_true(state.right_phase == pipeline_contract::StickPhase::Sustained,
                 "held right input must become sustained intent");
    require_true(state.filtered_right.x > 0.6f,
                 "neutral learner must not absorb sustained input");
    require_true(state.right_confidence > 0.8f,
                 "strong held input must be high confidence");
}

void test_reversal_and_release_are_explicit() {
    controller_native::IntentFilter filter;
    filter.update({0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.00);
    filter.update({0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.01);
    auto state = filter.update({-0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.02);
    require_true(state.left_phase == pipeline_contract::StickPhase::Reversal,
                 "sign change must report reversal");
    state = filter.update({0.0f, 0.0f}, {0.0f, 0.0f}, true, false, 0.03);
    require_true(state.left_phase == pipeline_contract::StickPhase::Release,
                 "return to neutral must report release");
    state = filter.update({0.0f, 0.0f}, {0.0f, 0.0f}, true, false, 0.04);
    require_true(state.left_phase == pipeline_contract::StickPhase::Neutral,
                 "release is a single transition state");
}

void test_gamepad_raw_passthrough_with_fifteen_percent_ai_intent_floor() {
    double now = 1.0;
    controller_native::NativeGamepadController controller({}, &now);
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    // Include a long small offset: physical output must never be learned away.
    for (int i = 0; i < 300; ++i) {
        const float values[] = {0.004f, -0.012f, 0.08f, -0.149f, 0.15f, -0.15f, 0.0f};
        physical.right_x = i < 200 ? 0.012f : values[i % 7];
        physical.right_y = -physical.right_x;
        const auto intent = controller.begin_tick(physical).intent;
        require_true(intent.filtered_right.x == 0 && intent.filtered_right.y == 0,
            "AI must ignore each right-stick axis at or below fifteen percent");
        const auto output = controller.build_output_from_sampled_input();
        require_true(output.right_x == physical.right_x && output.right_y == physical.right_y,
            "AI intent deadzone must not alter raw physical passthrough");
        now += 0.001;
    }
    // No recentering or deadzone rescaling outside the AI-only threshold.
    physical.right_x = 0.30f; physical.right_y = -0.30f;
    auto intent = controller.begin_tick(physical).intent;
    require_true(intent.filtered_right.x == physical.right_x &&
        intent.filtered_right.y == physical.right_y &&
        intent.right_x.neutral_bias == 0 && intent.right_y.neutral_bias == 0,
        "AI intent at full activity must use the current unshifted input");
    (void)controller.build_output_from_sampled_input();
    // Every near-center Sony report code is still present at the DS4 output.
    for (int byte = 119; byte <= 137; ++byte) {
        const int sdl = byte * 257 - 32768;
        physical.right_x = float(sdl) / (sdl < 0 ? 32768.0f : 32767.0f);
        physical.right_y = -physical.right_x;
        now += 0.001;
        intent = controller.begin_tick(physical).intent;
        const auto output = controller.build_output_from_sampled_input();
        const auto report = controller_native::to_ds4_report(output);
        require_true(intent.filtered_right.x == 0 && intent.filtered_right.y == 0 &&
            report.bytes[2] == byte && report.bytes[3] == byte,
            "AI-neutral Sony jitter must survive controller and DS4 encoding byte-for-byte");
    }
}

void test_gamepad_ai_deadzone_does_not_change_mouse_intent() {
    double now = 1.0;
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.adapter_direct_mouse_manual = true;
    controller_native::NativeGamepadController controller(config, &now);
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.right_x = 0.04f;
    require_true(controller.begin_tick(physical).intent.filtered_right.x > 0.03f,
        "gamepad AI intent threshold must not leak into direct mouse input");
}

void test_gamepad_intent_transition_curve() {
    double now = 1.0;
    controller_native::NativeGamepadController controller({}, &now);
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    const float magnitudes[] = {0.0f, .08f, .15f, .20f, .225f, .25f, .30f, .80f};
    const float weights[] = {0, 0, 0, 7.f/27.f, .5f, 20.f/27.f, 1, 1};
    for (float sign : {-1.f, 1.f}) for (int i = 0; i < 8; ++i) {
        physical.right_x = sign*magnitudes[i];
        physical.right_y = -physical.right_x;
        now += .001;
        const auto intent = controller.begin_tick(physical).intent;
        require_near(intent.right_x.activity, weights[i], 2e-6f,
            "gamepad manual authority must rise smoothly from 15 to 30 percent");
        require_near(intent.filtered_right.x, physical.right_x*weights[i], 2e-6f,
            "D correction must consume the same attenuated intent");
        require_near(intent.filtered_right.y, physical.right_y*weights[i], 2e-6f,
            "vertical D correction must share the curve");
        const auto output = controller.build_output_from_sampled_input();
        require_true(output.right_x == physical.right_x && output.right_y == physical.right_y,
            "intent attenuation must not alter no-target raw passthrough");
    }
}

void test_gamepad_authority_curve_continuity(const native_test::TestContext& context) {
    float max_step = 0;
    int samples = 0, passthrough_checks = 0;
    for (int axis : {0, 1}) for (float sign : {-1.f, 1.f})
    for (bool fresh : {false, true}) for (bool carried : {false, true})
    for (bool firing : {false, true}) {
        controller_native::IntentFilterConfig fc; fc.gamepad_right_stick_curve = true;
        controller_native::IntentFilter filter(fc);
        controller_native::AssistControlStateMachineConfig ac; ac.use_gamepad_intent_for_arbitration = true;
        controller_native::AssistControlStateMachine arbiter(ac);
        float previous = 0;
        const auto vec = [axis](float x) {return axis ? pipeline_contract::Vec2f{0,x} : pipeline_contract::Vec2f{x,0};};
        for (int tick = 0; tick <= 2000; ++tick) {
            // Rising and falling through 15%, old 25%, and 30%. Actual D flags
            // come from attenuated intent; they cannot force full authority.
            const float raw = sign * (tick <= 1000 ? tick : 2000-tick) * .0005f;
            const auto intent = filter.update({},vec(raw),true,firing,1+tick*.001,true);
            controller_native::AssistControlStateMachineInput in;
            in.activation = pipeline_contract::AssistActivation::Engaged;
            in.target_authoritative = true;
            in.mode = pipeline_contract::ControlMode::BodyLockFollow;
            in.target_id = in.selector_target_generation = 1;
            in.now_seconds = 1+tick*.001; in.fresh_observation = fresh;
            in.visual_authority = .65f; in.firing = firing;
            in.carried_acquisition_gesture = carried;
            in.manual_stick = vec(raw); in.filtered_manual_stick = intent.filtered_right;
            in.manual_axis_activity = {intent.right_x.activity,intent.right_y.activity};
            in.manual_correction_x = !carried && intent.filtered_right.x != 0;
            in.manual_correction_y = !carried && intent.filtered_right.y != 0;
            in.ai_stick = vec(-sign*.61f);
            in.target_error_px = vec((axis ? sign : -sign)*56);
            const auto out = arbiter.update(in);
            require_true(out.phase == controller_native::AssistControlPhase::Track,"curve sweep must enter Track");
            const float value = axis ? out.stick.y : out.stick.x;
            if (tick) max_step = std::max(max_step,std::fabs(value-previous));
            previous=value; ++samples;
            if (std::fabs(raw)<=.15f) require_near(value,-sign*.61f,2e-6f,"neutral intent must yield to AI");
            if (std::fabs(raw)>=.30f) require_near(value,raw,2e-6f,"full manual authority lost");
            auto cf=in; cf.manual_exit_requested=true;
            controller_native::AssistControlStateMachine exit_machine(ac);
            const auto exited=exit_machine.update(cf);
            require_near(axis?exited.stick.y:exited.stick.x,raw,1e-6f,"explicit exit must bypass curve");
            ++passthrough_checks;
        }
    }
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("gamepad-authority-curve.json"));
    report << std::setprecision(9) << "{\"samples\":"<<samples<<",\"exit_controls\":"<<passthrough_checks<<",\"max_step\":"<<max_step<<"}\n";
    require_true(samples==64032 && passthrough_checks==samples,"incomplete curve sweep");
    require_true(max_step<.02f,"manual authority curve created an output cliff");
}

void test_gesture_purpose_survives_target_acquisition() {
    controller_native::IntentFilter filter;
    auto state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.00,
        false, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Onset &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "gesture begun without I must be acquisition");

    state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.01,
        true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Sustained &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "held acquisition gesture was reinterpreted after I appeared");

    (void)filter.update(
        {0.0f, 0.0f}, {}, true, false, 0.02, true, false);
    state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.03,
        true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Onset &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "new gesture on owned I must correct current target");
}

void test_two_dimensional_reversal_starts_new_purpose() {
    controller_native::IntentFilter filter;
    (void)filter.update(
        {}, {0.40f, 0.20f}, true, false, 0.00, false, false);
    const auto state = filter.update(
        {}, {-0.10f, -0.50f}, true, false, 0.01, true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Reversal &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "2-D reversal must start a correction gesture on owned I");
}

void test_correction_purpose_does_not_cross_target_loss() {
    controller_native::IntentFilter filter;
    auto state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.00, true, false);
    require_true(
        state.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "owned gesture did not begin as current-target correction");

    state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.01, false, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Sustained &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "held correction leaked across target loss");

    state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.02, true, false);
    require_true(
        state.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "next target inherited the prior target's correction purpose");
}

void test_carried_axis_release_continuity(const native_test::TestContext& context) {
    float maximum_step = 0.0f;
    float maximum_neutral_loss = 0.0f;
    int neutral_samples = 0;
    int held_samples = 0;
    int worst_tick = 0, worst_axis = 0;
    float worst_physical = 0, worst_centered = 0, worst_activity = 0, worst_previous = 0, worst_final = 0;
    // Sweep physical release through calibrated/adaptive deadzones, both axes
    // and signs, with and without firing. This checks delivered behavior, not
    // the activity formula, and protects a real held opposing gesture.
    for (int axis : {0, 1}) for (float sign : {-1.0f, 1.0f})
    for (float deadzone : {.02f, .06f}) for (float bias : {-.012f, .012f})
    for (bool firing : {false, true}) {
        controller_native::IntentFilterConfig config;
        config.base_deadzone = deadzone;
        controller_native::IntentFilter filter(config);
        const auto vec = [axis](float value) {
            return axis == 0 ? pipeline_contract::Vec2f{value, 0}
                             : pipeline_contract::Vec2f{0, value};
        };
        for (int i = 0; i < 200; ++i)
            (void)filter.update({}, vec(bias), false, false, i * .001);
        controller_native::AssistControlStateMachine machine;
        float previous = 0;
        for (int i = 0; i <= 360; ++i) {
            const float physical = bias + sign * (.18f - i * .0005f);
            const auto intent = filter.update({}, vec(physical), true, firing,
                                               1 + i * .001, i > 0);
            const auto& state = axis == 0 ? intent.right_x : intent.right_y;
            controller_native::AssistControlStateMachineInput input;
            input.activation = pipeline_contract::AssistActivation::Engaged;
            input.target_authoritative = input.fresh_observation = true;
            input.target_id = 172;
            input.selector_target_generation = 8;
            input.now_seconds = 1 + i * .001;
            input.mode = pipeline_contract::ControlMode::BodyLockFollow;
            input.visual_authority = 1;
            input.firing = firing;
            input.target_error_px = vec(-sign * 32 * (axis == 1 ? -1 : 1));
            input.manual_stick = vec(physical);
            input.centered_manual_available = true;
            input.centered_manual_stick = vec(physical - state.neutral_bias);
            input.filtered_manual_stick = intent.filtered_right;
            input.manual_axis_activity = {intent.right_x.activity, intent.right_y.activity};
            input.carried_acquisition_gesture = intent.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget;
            input.ai_stick = vec(-sign * .5f);
            const auto output = machine.update(input);
            const float final = axis == 0 ? output.stick.x : output.stick.y;
            require_true(input.carried_acquisition_gesture,
                         "release sweep changed gesture purpose unexpectedly");
            if (i == 0) {
                require_near(final, physical, 1e-5f, "held gesture lost physical authority");
                ++held_samples;
            } else {
                if (std::fabs(final - previous) > maximum_step) {
                    worst_tick = i;
                    worst_axis = axis;
                    worst_physical = physical;
                    worst_centered = physical - state.neutral_bias;
                    worst_activity = state.activity;
                    worst_previous = previous;
                    worst_final = final;
                }
                maximum_step = std::max(maximum_step, std::fabs(final - previous));
            }
            if (state.filtered == 0) {
                maximum_neutral_loss = std::max(maximum_neutral_loss,
                    std::fabs(final + sign * .5f));
                ++neutral_samples;
            }
            previous = final;
        }
    }
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("carry-release-continuity.json"));
    report << std::setprecision(9) << "{\"max_output_step\":" << maximum_step
           << ",\"max_neutral_loss\":" << maximum_neutral_loss
           << ",\"neutral_samples\":" << neutral_samples
           << ",\"held_samples\":" << held_samples << "}\n";
    std::cout << "[CarryReleaseSweep] max_step=" << maximum_step
              << " tick=" << worst_tick << " axis=" << worst_axis
              << " physical=" << worst_physical << " centered=" << worst_centered
              << " activity=" << worst_activity << " previous=" << worst_previous
              << " final=" << worst_final << '\n';
    report.close();
    require_true(held_samples == 32 && neutral_samples > 500,
                 "release sweep must cover material and neutral inputs");
    require_true(maximum_step < .04f, "release created a deadzone output cliff");
    require_true(maximum_neutral_loss < .001f, "noise-sized axis retained a carried-input veto");
}

}  // namespace

void register_intent_filter_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "configurable_intent_band_randomized_and_hot_reload", [] {
        using namespace controller_native;
        for (auto seed : {314159u, 271828u}) {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> unit(0, 1);
            for (int scenario = 0; scenario < 60; ++scenario) {
                GamepadRuntimeConfig config;
                const auto change_band = [&] {
                    config.ai_aim.manual_intent_begin = unit(rng) * .8f;
                    config.ai_aim.manual_intent_full = config.ai_aim.manual_intent_begin + .02f +
                        unit(rng) * (.98f - config.ai_aim.manual_intent_begin);
                };
                change_band();
                double now = 10;
                NativeGamepadController controller(config, &now);
                PhysicalGamepadState physical{};
                physical.connected = true; physical.left_trigger = 1;
                std::uint64_t scope_epoch = 0;
                const int count = scenario % 2 ? 100 : 2000;
                for (int tick = 0; tick < count; ++tick) {
                    if (tick && tick % 73 == 0) {
                        change_band();
                        controller.apply_hot_config(config, true);
                    }
                    const float begin = config.ai_aim.manual_intent_begin;
                    const float full = config.ai_aim.manual_intent_full;
                    const float boundary[] = {0, begin, (begin + full) / 2, full, 1};
                    physical.right_x = tick % 8 < 5 ? boundary[tick % 8] : unit(rng);
                    if (tick % 2) physical.right_x = -physical.right_x;
                    physical.right_y = unit(rng) * 2 - 1;
                    now += .0005 + unit(rng) * .004;
                    const auto preparation = controller.begin_tick(physical);
                    if (!tick) scope_epoch = preparation.scope.scope_epoch;
                    require_true(preparation.scope.scope_epoch == scope_epoch,
                                 "threshold reload must preserve physical scope lifecycle");
                    const auto expected = [&](float raw) {
                        const double t = std::clamp((static_cast<double>(std::fabs(raw)) - begin) / (full - begin), 0.0, 1.0);
                        return static_cast<float>(t * t * (3 - 2 * t));
                    };
                    require_near(preparation.intent.right_x.activity, expected(physical.right_x), 3e-6f,
                                 "configured x authority must follow the requested smooth band");
                    require_near(preparation.intent.filtered_right.y, physical.right_y * expected(physical.right_y), 3e-6f,
                                 "configured y correction must use its own axis exactly once");
                    const auto output = controller.build_output_from_sampled_input();
                    require_true(output.right_x == physical.right_x && output.right_y == physical.right_y,
                                 "configurable AI-only intent must preserve native raw passthrough");
                }
            }
        }
        GamepadRuntimeConfig config;
        config.ai_aim.adapter_direct_mouse_manual = true;
        config.ai_aim.manual_intent_begin = .8f;
        config.ai_aim.manual_intent_full = .9f;
        double now = 1;
        NativeGamepadController mouse(config, &now);
        PhysicalGamepadState physical{}; physical.connected = true; physical.right_x = .04f;
        require_true(mouse.begin_tick(physical).intent.filtered_right.x > .03f,
                     "user-configured gamepad band must leave mouse policy unchanged");
        IntentFilterConfig filter_config; filter_config.gamepad_right_stick_curve = true;
        IntentFilter filter(filter_config);
        filter.update({}, {.8f, 0}, true, false, 1, true);
        filter_config.gamepad_intent_begin = .1f; filter_config.gamepad_intent_full = .2f;
        filter.reconfigure(filter_config);
        const auto continued = filter.update({}, {.8f, 0}, true, false, 1.001, true);
        require_true(continued.right_phase == pipeline_contract::StickPhase::Sustained &&
                     continued.right_purpose == pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
                     "reload must retain active gesture phase and purpose");
    });
    registry.add_case("BaseContracts", "gamepad_intent_transition_curve", test_gamepad_intent_transition_curve);
    registry.add_context_case("BaseBodyLock", "gamepad_authority_curve_continuity", test_gamepad_authority_curve_continuity);
    registry.add_case("BaseContracts", "gamepad_raw_passthrough_ai_intent_floor", test_gamepad_raw_passthrough_with_fifteen_percent_ai_intent_floor);
    registry.add_case("BaseContracts", "gamepad_ai_deadzone_preserves_mouse", test_gamepad_ai_deadzone_does_not_change_mouse_intent);
    registry.add_context_case("BaseBodyLock", "carried_axis_release_continuity", test_carried_axis_release_continuity);
    registry.add_case("BaseBodyLock", "deadzone_sized_drift_is_neutral", test_deadzone_sized_drift_is_neutral);
    registry.add_case("BaseBodyLock", "sustained_input_is_not_learned_away", test_sustained_input_is_not_learned_away);
    registry.add_case("BaseBodyLock", "reversal_and_release_are_explicit", test_reversal_and_release_are_explicit);
    registry.add_case("BaseBodyLock", "gesture_purpose_survives_target_acquisition", test_gesture_purpose_survives_target_acquisition);
    registry.add_case("BaseBodyLock", "two_dimensional_reversal_starts_new_purpose", test_two_dimensional_reversal_starts_new_purpose);
    registry.add_case("BaseBodyLock", "correction_purpose_does_not_cross_target_loss", test_correction_purpose_does_not_cross_target_loss);
}
