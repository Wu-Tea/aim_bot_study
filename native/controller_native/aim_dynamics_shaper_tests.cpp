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

void test_manual_opposition_reduces_slew_target() {
    controller_native::AimDynamicsShaper neutral_shaper;
    controller_native::AimDynamicsShaper manual_shaper;
    const auto plan = active_plan();
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    const auto neutral = neutral_shaper.shape({1.0f, 0.0f}, {}, plan, 0.05f);
    const auto opposed = manual_shaper.shape({1.0f, 0.0f}, correction, plan, 0.05f);
    require_true(opposed.x < neutral.x * 0.6f,
                 "single shaper must arbitrate confident manual opposition");
}

}  // namespace

int main() {
    try {
        test_step_and_reversal_are_bounded();
        test_plan_loss_decays_instead_of_dropping();
        test_manual_opposition_reduces_slew_target();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AimDynamicsShaperTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
