#include "control_frame.h"
#include "output_composer.h"
#include "ds4_output_report.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <random>
#include <chrono>

namespace {

using pipeline_contract::ControllerTickId;
using pipeline_contract::EventSequence;

void require_true(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

pipeline_contract::PreRecoilStickCommand proposal(float x, float y) {
    return pipeline_contract::PreRecoilStickCommand::from_stick({x, y});
}

pipeline_contract::FireCommand no_fire() {
    return {};
}

pipeline_contract::RecoilContribution no_recoil() {
    return {};
}

pipeline_contract::AuxiliaryDpadCommand no_dpad() {
    return {};
}

void compose_to_final(
    controller_native::OutputComposer& composer,
    const controller_native::PhysicalGamepadState& physical,
    const pipeline_contract::PreRecoilStickCommand& aim,
    const pipeline_contract::FireCommand& fire,
    const pipeline_contract::RecoilContribution& recoil,
    const pipeline_contract::AuxiliaryDpadCommand& dpad) {
    require_true(composer.seed_physical_passthrough(physical) ==
                     controller_native::OutputComposeStatus::Ok,
                 "physical seed");
    require_true(composer.apply_pre_recoil_stick(aim) ==
                     controller_native::OutputComposeStatus::Ok,
                 "pre-recoil write");
    require_true(composer.apply_fire_only(fire) ==
                     controller_native::OutputComposeStatus::Ok,
                 "fire write");
    require_true(composer.apply_recoil(recoil) ==
                     controller_native::OutputComposeStatus::Ok,
                 "recoil write");
    require_true(composer.merge_auxiliary_actions(dpad) ==
                     controller_native::OutputComposeStatus::Ok,
                 "dpad merge");
    require_true(composer.finalize() ==
                     controller_native::OutputComposeStatus::Ok,
                 "finalize");
}

void test_output_write_boundaries_and_recoil_order() {
    controller_native::OutputComposer composer;
    controller_native::PhysicalGamepadState physical{};
    physical.right_x = 0.15f;
    physical.right_y = -0.25f;
    require_true(composer.seed_physical_passthrough(physical) ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary physical seed");
    require_true(composer.apply_pre_recoil_stick(proposal(0.40f, -0.20f)) ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary aim write");
    require_true(composer.apply_pre_recoil_stick(proposal(0.10f, 0.10f)) ==
                     controller_native::OutputComposeStatus::DuplicateStage,
                 "second pre-recoil writer must be rejected");
    require_true(composer.apply_recoil({{0.1f, 0.1f}, {}, true}) ==
                     controller_native::OutputComposeStatus::OutOfOrder,
                 "recoil cannot precede fire stage");

    pipeline_contract::FireCommand fire{};
    fire.header.sequence = EventSequence::from(20);
    fire.header.controller_tick = ControllerTickId::from(20);
    fire.synthetic_active = true;
    fire.synthetic_rb = true;
    fire.synthetic_right_trigger = 0.7f;
    require_true(composer.apply_fire_only(fire) ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary fire write");
    require_true(composer.apply_recoil({{0.1f, 0.3f}, {}, true}) ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary recoil write");
    auto dpad = no_dpad();
    dpad.header.sequence = EventSequence::from(21);
    dpad.header.controller_tick = ControllerTickId::from(21);
    dpad.up = true;
    require_true(composer.merge_auxiliary_actions(dpad) ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary dpad write");
    require_true(composer.finalize() ==
                     controller_native::OutputComposeStatus::Ok,
                 "boundary finalize");
    const auto* output = composer.finalized_output();
    require_true(output != nullptr && std::fabs(output->right_x - 0.50f) < 1e-6f &&
                     std::fabs(output->right_y - 0.10f) < 1e-6f,
                 "recoil must follow exactly one pre-recoil stick");
    require_true(output->rb && std::fabs(output->right_trigger - 0.7f) < 1e-6f,
                 "fire command may write fire only");
}

void test_physical_passthrough_and_auxiliary_dpad_merge() {
    controller_native::PhysicalGamepadState physical{};
    physical.left_x = -0.80f;
    physical.left_y = 0.60f;
    physical.right_x = -0.30f;
    physical.right_y = 0.20f;
    physical.left_trigger = 0.45f;
    physical.right_trigger = 0.25f;
    physical.lb = true;
    physical.a = true;
    physical.dpad_down = true;

    controller_native::OutputComposer composer;
    auto dpad = no_dpad();
    dpad.header.sequence = EventSequence::from(30);
    dpad.header.controller_tick = ControllerTickId::from(30);
    dpad.up = true;
    compose_to_final(composer, physical, {}, no_fire(), no_recoil(), dpad);
    const auto* output = composer.finalized_output();
    require_true(output != nullptr && output->lb && output->a &&
                     output->dpad_down && output->dpad_up,
                 "physical buttons and merged D-pad must be preserved");
    require_true(std::fabs(output->left_x + 0.80f) < 1e-6f &&
                     std::fabs(output->left_y - 0.60f) < 1e-6f &&
                     std::fabs(output->right_x + 0.30f) < 1e-6f &&
                     std::fabs(output->right_y - 0.20f) < 1e-6f,
                 "absent aim command must preserve physical input");
}

void test_duplicate_finalize_is_rejected() {
    controller_native::OutputComposer composer;
    compose_to_final(composer, {}, {}, no_fire(), no_recoil(), no_dpad());
    require_true(composer.finalize() ==
                     controller_native::OutputComposeStatus::AlreadyFinalized,
                 "finalization must be one-shot");
    require_true(composer.apply_recoil(no_recoil()) ==
                     controller_native::OutputComposeStatus::AlreadyFinalized,
                 "writes after finalization must be rejected");
}

void test_control_frame_contains_only_output_boundary_values() {
    controller_native::PhysicalGamepadState physical{};
    physical.right_x = 0.25f;
    const auto frame = controller_native::ControlFrame::begin(
        physical, ControllerTickId::from(5), EventSequence::from(8));
    require_true(frame.sampled().physical.right_x == 0.25f &&
                     frame.controller_tick() == ControllerTickId::from(5) &&
                     frame.valid(),
                 "ControlFrame must preserve its immutable input identity");
}

void test_game_transfer_contract_and_randomized_roundtrip() {
    using namespace controller_native;
    GameStickTransferConfig config{true, false, .16f, 1.0f};
    const auto start = transfer_game_stick({.01f, 0}, config);
    require_true(std::fabs(start.x - .1684f) < 1e-6f && start.y == 0,
                 "one percent input must bypass configured game deadzone");
    for (bool axial : {false, true}) {
        for (float deadzone : {0.0f, .16f, .3f, .5f}) {
            for (float exponent : {1.0f, 1.5f, 2.0f, 3.0f}) {
                config = {true, axial, deadzone, exponent};
                auto zero = transfer_game_stick({}, config);
                require_true(zero.x == 0 && zero.y == 0, "neutral must never receive anti-deadzone");
                float previous = 0;
                for (int step = 1; step <= 1000; ++step) {
                    const auto wire = transfer_game_stick({step / 1000.0f, 0}, config);
                    require_true(wire.x > previous && wire.y == 0, "axis mapping must be monotone");
                    previous = wire.x;
                }
                // Independent validation seeds; include square-rim input and
                // DS4's asymmetric positive/negative quantization.
                for (unsigned seed : {29092026u, 784129u}) {
                    std::mt19937 random(seed);
                    std::uniform_real_distribution<float> axis(-1, 1);
                    for (int i = 0; i < 4096; ++i) {
                        const pipeline_contract::Vec2f input{axis(random), axis(random)};
                        const auto wire = transfer_game_stick(input, config);
                        const auto decoded = transfer_game_stick(wire, config, true);
                        require_true(std::isfinite(wire.x) && std::isfinite(wire.y) &&
                            std::fabs(wire.x) <= 1 && std::fabs(wire.y) <= 1,
                            "mapped output outside report domain");
                        require_true(std::hypot(decoded.x - input.x, decoded.y - input.y) < 2e-6f,
                            "floating feedback coordinate must roundtrip");
                        const auto quantized = transfer_game_stick(
                            {ds4_axis_value(ds4_axis(wire.x)), -ds4_axis_value(ds4_axis(-wire.y))}, config, true);
                        require_true(std::hypot(quantized.x - input.x, quantized.y - input.y) < .04f,
                            "DS4 feedback exceeds fixed quantization bound");
                        auto disabled = config;
                        disabled.enabled = false;
                        const auto original = transfer_game_stick(input, disabled);
                        require_true(original.x == input.x && original.y == input.y,
                            "disabled transfer must preserve exact passthrough");
                        if (!axial) require_true(std::fabs(wire.x * input.y - wire.y * input.x) < 2e-6f,
                            "radial mapping changed direction");
                    }
                }
            }
        }
    }
}

void test_game_transfer_final_stage_and_lifecycle() {
    using namespace controller_native;
    GamepadRuntimeConfig config;
    config.output_transfer = {true, false, .16f, 1.0f};
    config.recoil.enabled = true;
    config.recoil.feedback_amount = .2f;
    config.recoil.hipfire_multiplier = .5f;
    double now = 100;
    NativeGamepadController controller(config, &now);
    for (unsigned seed : {71327u, 942817u}) {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> axis(-1, 1);
        for (int ticks : {600, 10000}) {
            for (int i = 0; i < ticks; ++i) {
                PhysicalGamepadState physical{};
                physical.connected = i % 199 != 0;
                physical.right_x = i % 13 ? axis(random) : 0;
                physical.right_y = i % 13 ? axis(random) : 0;
                physical.left_x = .7f;
                physical.left_trigger = i % 2 ? 1.0f : 0.0f;
                physical.right_trigger = i % 3 ? 1.0f : 0.0f;
                physical.a = i % 7 == 0;
                controller.begin_tick(physical);
                const auto frame = controller.resolve_control_frame();
                OutputComposer baseline;
                OutputComposer mapped(config.output_transfer);
                require_true(baseline.compose(frame) == OutputComposeStatus::Ok &&
                    mapped.compose(frame) == OutputComposeStatus::Ok, "compose failed");
                const auto plain = *baseline.finalized_output();
                const auto output = *mapped.finalized_output();
                const auto expected = transfer_game_stick({plain.right_x, plain.right_y}, config.output_transfer);
                require_true(output.right_x == expected.x && output.right_y == expected.y,
                    "transfer must apply exactly once after recoil and arbitration");
                require_true(output.left_x == plain.left_x && output.left_trigger == plain.left_trigger &&
                    output.right_trigger == plain.right_trigger && output.a == plain.a && output.rb == plain.rb,
                    "right-stick adapter changed another control");
                controller.observe_composed_output(output);
                const auto& parts = controller.last_output_components();
                require_true(std::fabs(parts.recoil_stick.y - (plain.right_y - parts.before_recoil_stick.y)) < 2e-6f,
                    "anti-deadzone must not be misreported as recoil");
                const auto report = to_ds4_report(output);
                auto acknowledged = output;
                acknowledged.right_x = ds4_axis_value(report.bytes[2]);
                acknowledged.right_y = -ds4_axis_value(report.bytes[3]);
                controller.observe_delivered_output(acknowledged, physical.connected, now);
                require_true(mapped.finalize() == OutputComposeStatus::AlreadyFinalized,
                    "duplicate finalization must not apply the curve twice");
                now += .001;
            }
        }
    }
    // Explicit facade/neutral/recoil checks, independent of the mapping helper.
    controller.reset();
    PhysicalGamepadState physical{};
    physical.connected = true;
    physical.right_x = .1f;
    auto output = controller.build_output(physical);
    require_true(std::fabs(output.right_x - .244f) < 1e-6f && output.right_y == 0,
        "compatibility facade omitted or doubled mapping");
    physical.right_x = 0;
    output = controller.build_output(physical);
    require_true(output.right_x == 0 && output.right_y == 0, "release must become neutral on same tick");
    physical.right_trigger = 1;
    output = controller.build_output(physical);
    require_true(std::fabs(output.right_y + .244f) < 1e-6f, "hipfire recoil must be mapped after half-strength reduction");
    physical.left_trigger = 1;
    output = controller.build_output(physical);
    require_true(std::fabs(output.right_y + .328f) < 1e-6f, "ADS recoil must share the output coordinate");
}

void test_game_transfer_response_feedback() {
    using namespace controller_native;
    float scales[2]{};
    for (int enabled = 0; enabled != 2; ++enabled) {
        auto config = incident_fixture::base_config(1000, 260);
        config.ai_aim.ads_pickup_base_radius_px = 260;
        config.ai_aim.ads_snap_window_ms = 60;
        config.ai_aim.ads_completion_fresh_frames = 1000;
        config.ai_aim.ads_extension_budget_ms = 1000;
        config.ai_aim.aim_response_effect_delay_ms = 0;
        config.ai_aim.visual_authority_enabled = false;
        config.output_transfer = {enabled != 0, false, .16f, 1.0f};
        double now = 100;
        NativeGamepadController controller(config, &now);
        incident_fixture::TargetSpec target;
        target.observation_id = 512;
        target.selector_generation = 51;
        target.has_enemy_cue = target.enemy_identity_confirmed = true;
        float error = 200;
        int active_ticks = 0;
        for (int tick = 0; tick != 450; ++tick) {
            if (tick % 5 == 0) controller.submit_vision_snapshot(
                incident_fixture::observed_snapshot(target, tick / 5 + 1, now, error, 0, tick == 0));
            const auto output = controller.build_output(incident_fixture::ads_input());
            // Independently specified synthetic plant. This checks coordinate
            // bookkeeping, not an empirical BO3 transfer/response measurement.
            const float camera = enabled ? std::copysign(
                std::max(0.0f, (std::fabs(output.right_x) - .16f) / .84f), output.right_x) : output.right_x;
            if (camera > .01f) ++active_ticks;
            error = std::max(10.0f, error - camera * 500 * .001f);
            now += .001;
        }
        const auto& plan = controller.last_target_plan();
        require_true(active_ticks > 50 && plan.target_id != 0 && plan.response_confidence > 0,
            "response test must actually exercise learning and target ownership");
        scales[enabled] = plan.response_scale;
        const auto identity = plan.target_id;
        const auto epoch = controller.ads_epoch();
        const auto learned = controller.learning_snapshot();
        require_true(learned[2].accepted_samples + learned[3].accepted_samples > 0,
            "reload fixture must have actual ADS learning before clearing");
        auto revised = config;
        revised.ai_aim.ads_snap_max_ai_force = .4f;
        revised.ai_aim.body_lock_max_ai_force = .3f;
        revised.auto_fire.fire_output = "RT";
        revised.auto_fire.manual_fire_input = "RT";
        controller.apply_hot_config(revised);
        for (const auto& value : controller.learning_snapshot()) require_true(value.accepted_samples == 0 && value.confidence == 0,
            "hot reload retained estimator excitation, confidence or sample counts");
        require_true(controller.last_target_plan().target_id == identity && controller.ads_epoch() == epoch,
            "clearing learning must not rearm ADS or discard target identity");
        now += .001;
        const auto next = controller.build_output(incident_fixture::ads_input());
        require_true(std::isfinite(next.right_x) && controller.ads_epoch() == epoch && controller.last_target_plan().target_id == identity,
            "next tick after reload must keep active target/ADS invariant");
    }
    require_true(scales[0] >= 400 && scales[0] <= 600 && std::fabs(scales[0] - scales[1]) < 1,
        "wire amplification contaminated learned camera response");
}

}  // namespace

void test_hot_reload_randomized_manual_fire_and_recoil() {
    using namespace controller_native;
    for (unsigned seed : {7311u, 9401u}) {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> axis(-.6f, .6f);
        GamepadRuntimeConfig config;
        double now = 100;
        NativeGamepadController controller(config, &now);
        for (int tick = 0; tick < 4000; ++tick) {
            if (tick % 31 == 0) {
                const char* bindings[] = {"both", "RB", "RT"};
                config.auto_fire.manual_fire_input = bindings[random() % 3];
                config.auto_fire.fire_output = random() % 2 ? "RB" : "RT";
                config.recoil.enabled = random() % 2;
                config.recoil.feedback_amount = .14f + (random() % 21) * .01f;
                config.recoil.hipfire_multiplier = (random() % 11) * .1f;
                controller.apply_hot_config(config);
            }
            PhysicalGamepadState physical{};
            physical.connected = true;
            physical.right_x = axis(random);
            physical.right_y = axis(random);
            physical.left_trigger = random() % 2 ? 1.0f : 0.0f;
            physical.rb = random() % 2;
            physical.right_trigger = random() % 2 ? 1.0f : 0.0f;
            const auto& preparation = controller.begin_tick(physical);
            const bool fire = (config.auto_fire.manual_fire_input != "RT" && physical.rb) ||
                (config.auto_fire.manual_fire_input != "RB" && physical.right_trigger > .04f);
            require_true(preparation.scope.manual_fire_active == fire, "scope and chosen manual binding disagree after reload");
            const auto output = controller.build_output_from_sampled_input();
            const float recoil = config.recoil.enabled && fire ? config.recoil.feedback_amount *
                (preparation.scope.physical_ads_ready ? 1.0f : config.recoil.hipfire_multiplier) : 0;
            require_true(std::fabs(output.right_y - (physical.right_y - recoil)) < 2e-6f && output.right_x == physical.right_x,
                "new recoil must apply once while preserving target-free manual axes");
            require_true(output.rb == physical.rb && output.right_trigger == physical.right_trigger,
                "reload must never clear or synthesize raw physical fire passthrough");
            now += .001;
        }
    }
}

void register_control_pipeline_primitives_tests(
    native_test::Registry& registry) {
    registry.add_case("BaseEndToEnd", "ads_fire_delay_hot_reload_and_manual_passthrough", [] {
        using namespace controller_native;
        auto config = incident_fixture::base_config(1000, 260);
        config.auto_fire.ads_press_delay_ms = 125.5f;
        config.auto_fire.require_aim_ready = false;
        config.auto_fire.manual_fire_input = "RT";
        config.auto_fire.manual_takeover_release_seconds = 0;
        config.auto_fire.manual_takeover_resume_delay_seconds = 0;
        double now = 100;
        NativeGamepadController controller(config, &now);
        incident_fixture::TargetSpec target;
        target.observation_id = 901;
        target.selector_generation = 90;
        target.fire_authority = target.has_enemy_cue = target.enemy_identity_confirmed = true;
        std::uint64_t sequence = 1;
        auto physical = incident_fixture::ads_input();
        physical.left_trigger = .1f;
        controller.build_output(physical);
        physical.left_trigger = 1;
        auto step = [&](double at) {
            now = at;
            auto snapshot = incident_fixture::observed_snapshot(target, sequence++, now, 1, 1);
            snapshot.state.auto_fire_requested = true;
            controller.submit_vision_snapshot(snapshot);
            return controller.build_output(physical);
        };
        require_true(!step(100.06).rb, "late target must respect original light L2 press");
        require_true(controller.last_output_components().auto_fire_block_reason == "ads_press_delay",
                     "waiting must expose the dedicated fire block reason");
        const auto identity = controller.last_target_plan().target_id;
        config.auto_fire.ads_press_delay_ms = 150;
        controller.apply_hot_config(config);
        require_true(!step(100.149999).rb, "extended delay must suppress before original deadline");
        require_true(step(100 + 150.0 / 1000).rb, "fire must become eligible at original deadline");
        require_true(identity != 0 && controller.last_target_plan().target_id == identity,
                     "fire delay reload must preserve target identity");
        config.auto_fire.ads_press_delay_ms = 200;
        controller.apply_hot_config(config);
        physical.right_trigger = 1;
        const auto manual = step(100.16);
        require_true(manual.right_trigger == 1 && !manual.rb, "manual trigger must bypass automatic fire delay");
        physical.right_trigger = 0;
        config.auto_fire.ads_press_delay_ms = 120;
        controller.apply_hot_config(config);
        require_true(step(100.17).rb, "shortened expired delay must not start a new window");
        physical.left_trigger = 0;
        step(100.180); step(100.181); step(100.182);
        physical.left_trigger = 1;
        require_true(!step(100.183).rb && !step(100.25).rb, "fresh L2 press must start a fresh window");
        require_true(step(100.183 + 120.0 / 1000).rb, "fresh window must end at its own deadline");
        config.auto_fire.aim_only = false;
        config.auto_fire.ads_press_delay_ms = 5000;
        controller.apply_hot_config(config);
        physical.left_trigger = 0;
        step(100.304); step(100.305); step(100.306);
        require_true(controller.last_output_components().auto_fire_block_reason != "ads_press_delay",
                     "released physical ADS must not impose a permanent hipfire delay");
    });
    registry.add_case("BaseEndToEnd", "ads_fire_delay_randomized_short_and_long_sessions", [] {
        using namespace controller_native;
        for (auto seed : {9371u, 21893u}) {
            std::mt19937 rng(seed);
            for (int scenario = 0; scenario < 80; ++scenario) {
                auto config = incident_fixture::base_config(1000, 260);
                config.auto_fire.require_aim_ready = scenario % 2 == 0;
                config.auto_fire.manual_fire_input = "RT";
                config.auto_fire.ads_press_delay_ms = static_cast<float>(rng() % 50001) / 10;
                double now = 10 + scenario;
                const double press = now;
                NativeGamepadController controller(config, &now);
                auto physical = incident_fixture::ads_input(.06f, -.03f);
                physical.left_trigger = .1f;
                controller.build_output(physical);
                physical.left_trigger = 1;
                incident_fixture::TargetSpec target;
                target.observation_id = 901;
                target.selector_generation = 90;
                target.fire_authority = target.has_enemy_cue = target.enemy_identity_confirmed = true;
                const int ticks = scenario % 2 ? 250 : 1300;
                const int target_arrives = rng() % 80;
                bool fired = false;
                for (int tick = 1; tick <= ticks; ++tick) {
                    now += (.5 + rng() % 100 / 10.0) / 1000;
                    if (tick % 53 == 0) {
                        config.auto_fire.ads_press_delay_ms = static_cast<float>(rng() % 50001) / 10;
                        controller.apply_hot_config(config);
                    }
                    auto snapshot = tick >= target_arrives
                        ? incident_fixture::observed_snapshot(target, tick, now, 1, 1)
                        : incident_fixture::empty_snapshot(target, tick, now);
                    snapshot.state.auto_fire_requested = true;
                    controller.submit_vision_snapshot(snapshot);
                    physical.right_trigger = tick % 37 == 0 ? .72f : 0;
                    const auto output = controller.build_output(physical);
                    require_true(output.right_trigger == physical.right_trigger,
                                 "random delay/reload must preserve physical trigger exactly");
                    if (now < press + static_cast<double>(config.auto_fire.ads_press_delay_ms) / 1000)
                        require_true(!output.rb, "random hot reload must use initial physical press deadline");
                    fired = fired || output.rb;
                }
                config.auto_fire.ads_press_delay_ms = 0;
                controller.apply_hot_config(config);
                physical.right_trigger = 0;
                for (int tick = 0; tick < 300; ++tick) {
                    now += .001;
                    auto snapshot = incident_fixture::observed_snapshot(target, ticks + tick + 1, now, 1, 1);
                    snapshot.state.auto_fire_requested = true;
                    controller.submit_vision_snapshot(snapshot);
                    fired = controller.build_output(physical).rb || fired;
                }
                require_true(fired, "each scenario must demonstrate actual automatic fire after eligibility");
            }
        }
    });
    registry.add_case("BaseEndToEnd", "hipfire_ai_multiplier_keeps_ads_and_manual", [] {
        for (bool ads : {false, true}) for (int direction : {-1, 1}) {
            const auto run = [=](float multiplier) {
                auto config = controller_native::incident_fixture::base_config(100, 200);
                config.ai_aim.hipfire_multiplier = multiplier;
                config.ai_aim.aim_response_learning_enabled = false;
                config.auto_fire.manual_fire_activates_ai_aim = true;
                double now = 10;
                controller_native::NativeGamepadController controller(config, &now);
                controller_native::incident_fixture::TargetSpec spec;
                spec.observation_id = 1;
                spec.selector_generation = 991;
                spec.has_enemy_cue = spec.enemy_identity_confirmed = true;
                controller.submit_vision_snapshot(controller_native::incident_fixture::observed_snapshot(
                    spec, 1, now, direction * 18.0f, direction * 8.0f));
                auto physical = controller_native::incident_fixture::ads_input(.06f, -.03f, true);
                physical.left_trigger = ads ? 1.0f : 0.0f;
                auto output = controller.build_output(physical);
                const auto mode = controller.last_target_plan().mode;
                require_true(controller.last_target_plan().target_id != 0 && mode != pipeline_contract::ControlMode::Manual,
                    "multiplier test must admit a real target through ADS or manual-fire scope");
                const auto shaped = controller.last_output_components().shaped_assist_stick;
                return std::make_pair(output, shaped);
            };
            const auto baseline = run(1.0f);
            require_true(std::hypot(baseline.second.x, baseline.second.y) > .0001f, "AI multiplier trigger must produce actual work");
            for (float multiplier : {0.0f, .5f, .75f, 1.25f, 2.0f, 3.0f}) {
                const auto result = run(multiplier);
                const float scale = ads ? 1.0f : multiplier;
                require_true(std::fabs(result.second.x - baseline.second.x * scale) < 1e-6f &&
                    std::fabs(result.second.y - baseline.second.y * scale) < 1e-6f, "hipfire AI multiplier must scale both axes while ADS stays exact");
                if (ads) require_true(result.first.right_x == baseline.first.right_x && result.first.right_y == baseline.first.right_y,
                    "hipfire multiplier must leave final ADS actuator output unchanged");
                if (!ads && multiplier != 0 && multiplier != 1) require_true(
                    result.first.right_x != baseline.first.right_x || result.first.right_y != baseline.first.right_y,
                    "hipfire multiplier must reach actual actuator output, not only diagnostics");
                require_true(result.first.right_trigger == 1.0f && result.first.left_trigger == (ads ? 1.0f : 0.0f),
                    "AI multiplier must preserve physical fire and scope passthrough");
                if (!ads && multiplier == 0) require_true(result.first.right_x == .06f && result.first.right_y == -.03f,
                    "zero hipfire AI must restore exact manual passthrough");
            }
        }
    });
    registry.add_case("BaseEndToEnd", "hot_reload_randomized_manual_fire_recoil", test_hot_reload_randomized_manual_fire_and_recoil);
    registry.add_case("BaseContracts", "game_transfer_contract_randomized", test_game_transfer_contract_and_randomized_roundtrip);
    registry.add_case("BaseEndToEnd", "game_transfer_final_stage_lifecycle", test_game_transfer_final_stage_and_lifecycle);
    registry.add_case("BaseEndToEnd", "game_transfer_response_feedback", test_game_transfer_response_feedback);
    registry.add_case("BaseContracts", "output_write_boundaries_and_recoil_order", test_output_write_boundaries_and_recoil_order);
    registry.add_case("BaseContracts", "physical_passthrough_and_auxiliary_dpad_merge", test_physical_passthrough_and_auxiliary_dpad_merge);
    registry.add_case("BaseContracts", "duplicate_finalize_is_rejected", test_duplicate_finalize_is_rejected);
    registry.add_case("BaseContracts", "control_frame_contains_only_output_boundary_values", test_control_frame_contains_only_output_boundary_values);
}
