#include "aim_dynamics_shaper.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan active_plan() {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.confidence = 1.0f;
    plan.aim_authority = 1.0f;
    return plan;
}

void test_step_and_reversal_are_bounded() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    auto output = shaper.shape({1.0f, 0.0f}, {}, plan, 0.01f);
    require_true(output.x > 0.0f && output.x <= 0.081f,
                 "initial AI step must obey slew limit");
    const float before = output.x;
    output = shaper.shape({-1.0f, 0.0f}, {}, plan, 0.01f);
    require_true(std::fabs(output.x - before) <= 0.081f,
                 "reversal must not jump across the axis");
}

void test_reversal_uses_normal_rise_rate_without_extra_phase_lag() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    pipeline_contract::Vec2f output{};
    for (int index = 0; index < 8; ++index) {
        output = shaper.shape({1.0f, 0.0f}, {}, plan, 0.001f);
    }
    const float before = output.x;
    output = shaper.shape({-1.0f, 0.0f}, {}, plan, 0.001f);
    require_true(
        output.x > 0.0f,
        "a one-tick reversal must not flip the delivered AI direction");
    require_true(
        before - output.x > 0.060f && before - output.x <= 0.065f,
        "reversal must use normal slew instead of adding a slow decay phase");
}

void test_plan_loss_decays_instead_of_dropping() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 20; ++i) shaper.shape({0.6f, 0.0f}, {}, plan, 0.01f);
    const auto before = shaper.current();
    pipeline_contract::TargetPlan missing{};
    const auto after = shaper.shape({}, {}, missing, 0.01f);
    require_true(after.x > 0.0f && after.x < before.x,
                 "plan loss must produce smooth decay");
}

void test_coasting_never_ramps_blind_assist() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto coast = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    require_true(coast.x <= observed.x + 0.0001f,
                 "coasting must not increase assist without a new observation");
}

void test_confirmed_wrong_axis_can_ramp_smoothly_while_coasting() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto coast = shaper.shape(
        {0.8f, 0.8f}, {}, plan, 0.001f, {1.0f, 0.0f});
    require_true(coast.x > observed.x,
                 "confirmed wrong-way X must keep ramping between observations");
    require_true(coast.x - observed.x <= 0.025f,
                 "confirmed wrong-way coast ramp must use the cautious slew rate");
    require_true(std::fabs(coast.y) <= 0.0001f,
                 "unconfirmed Y must retain blind-rise protection");
}

void test_confirmed_wrong_axis_does_not_ramp_when_assist_is_weaker() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    pipeline_contract::IntentState intent{};
    intent.filtered_right.x = -0.40f;
    const auto observed = shaper.shape({0.20f, 0.0f}, intent, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto coast = shaper.shape(
        {0.20f, 0.0f}, intent, plan, 0.001f, {1.0f, 0.0f});
    require_true(coast.x <= observed.x + 0.0001f,
                 "weak assist must not ramp against stronger manual input");
}

void test_manual_opposition_reduces_slew_target() {
    controller_native::AimDynamicsShaper neutral_shaper;
    controller_native::AimDynamicsShaper manual_shaper;
    const auto plan = active_plan();
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    correction.right_x.confidence = 1.0f;
    const auto neutral = neutral_shaper.shape({1.0f, 0.0f}, {}, plan, 0.05f);
    const auto opposed = manual_shaper.shape({1.0f, 0.0f}, correction, plan, 0.05f);
    require_true(opposed.x < neutral.x * 0.6f,
                 "single shaper must arbitrate confident manual opposition");
}

void test_helpful_manual_input_reduces_but_keeps_assist() {
    controller_native::AimDynamicsShaper neutral_shaper;
    controller_native::AimDynamicsShaper manual_shaper;
    const auto plan = active_plan();
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = 0.5f;
    correction.right_confidence = 1.0f;
    correction.right_x.confidence = 1.0f;
    pipeline_contract::Vec2f neutral{};
    pipeline_contract::Vec2f cooperative{};
    for (int i = 0; i < 12; ++i) {
        neutral = neutral_shaper.shape({1.0f, 0.0f}, {}, plan, 0.05f);
        cooperative = manual_shaper.shape({1.0f, 0.0f}, correction, plan, 0.05f);
    }
    require_true(cooperative.x > 0.0f && cooperative.x < neutral.x,
                 "helpful manual input must avoid double-driving while retaining assist");
}

}  // namespace

int main() {
    try {
        test_step_and_reversal_are_bounded();
        test_reversal_uses_normal_rise_rate_without_extra_phase_lag();
        test_plan_loss_decays_instead_of_dropping();
        test_coasting_never_ramps_blind_assist();
        test_confirmed_wrong_axis_can_ramp_smoothly_while_coasting();
        test_confirmed_wrong_axis_does_not_ramp_when_assist_is_weaker();
        test_manual_opposition_reduces_slew_target();
        test_helpful_manual_input_reduces_but_keeps_assist();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AimDynamicsShaperTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
