#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_defaults_are_safe() {
    const pipeline_contract::VisionObservationBatch observations{};
    const pipeline_contract::IntentState intent{};
    const pipeline_contract::TargetPlan plan{};

    require_true(observations.count == 0, "observation batch must default empty");
    require_true(intent.right_phase == pipeline_contract::StickPhase::Neutral,
                 "right stick must default neutral");
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::None,
                 "plan must default to no target");
    require_true(plan.mode == pipeline_contract::ControlMode::Manual,
                 "plan must default to manual");
    require_true(!plan.fire_authority, "plan must default fire authority off");
    require_true(plan.fire_suppression == pipeline_contract::FireSuppressionReason::NoTarget,
                 "plan must explain default fire suppression");
}

void test_plan_is_fixed_size_and_publishable() {
    require_true(std::is_trivially_copyable_v<pipeline_contract::TargetPlan>,
                 "target plan must be trivially copyable");
    require_true(std::is_trivially_copyable_v<pipeline_contract::VisionObservationBatch>,
                 "observation batch must be trivially copyable");
    require_true(pipeline_contract::kMaxVisionCandidates == 32,
                 "candidate capacity is part of the observation contract");
}

void test_plan_values_can_be_validated() {
    pipeline_contract::TargetPlan plan{};
    require_true(pipeline_contract::valid(plan), "default plan must validate");
    plan.aim_authority = 1.1f;
    require_true(!pipeline_contract::valid(plan), "authority above one must be rejected");
    plan.aim_authority = 0.5f;
    plan.error_px.x = std::nanf("");
    require_true(!pipeline_contract::valid(plan), "non-finite plan error must be rejected");
}

void test_desired_point_must_remain_inside_valid_region() {
    pipeline_contract::TargetPlan plan{};
    plan.has_aim_region = true;
    plan.aim_region_px = {100.0f, 120.0f, 40.0f, 80.0f};
    plan.source_aim_px = {120.0f, 150.0f};
    plan.aim_px = {120.0f, 160.0f};
    plan.desired_point_normalized = {0.5f, 0.5f};
    require_true(pipeline_contract::valid(plan),
                 "D inside R must validate");

    plan.aim_px.y = 210.0f;
    require_true(!pipeline_contract::valid(plan),
                 "D outside R must fail the plan contract");
}

}  // namespace

int main() {
    test_defaults_are_safe();
    test_plan_is_fixed_size_and_publishable();
    test_plan_values_can_be_validated();
    test_desired_point_must_remain_inside_valid_region();
    return 0;
}
