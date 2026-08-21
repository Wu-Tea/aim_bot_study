#include "control_frame.h"
#include "output_composer.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

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
    require_true(composer.merge_auxiliary_dpad(dpad) ==
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
    require_true(composer.merge_auxiliary_dpad(dpad) ==
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

}  // namespace

void register_control_pipeline_primitives_tests(
    native_test::Registry& registry) {
    registry.add_case("BaseContracts", "output_write_boundaries_and_recoil_order", test_output_write_boundaries_and_recoil_order);
    registry.add_case("BaseContracts", "physical_passthrough_and_auxiliary_dpad_merge", test_physical_passthrough_and_auxiliary_dpad_merge);
    registry.add_case("BaseContracts", "duplicate_finalize_is_rejected", test_duplicate_finalize_is_rejected);
    registry.add_case("BaseContracts", "control_frame_contains_only_output_boundary_values", test_control_frame_contains_only_output_boundary_values);
}
