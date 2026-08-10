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

void test_reversal_discharges_before_opposite_rise() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    pipeline_contract::Vec2f output{};
    for (int index = 0; index < 8; ++index) {
        output = shaper.shape({1.0f, 0.0f}, {}, plan, 0.001f);
    }
    const float before = output.x;
    output = shaper.shape({-1.0f, 0.0f}, {}, plan, 0.001f);
    require_true(before > 0.0f && output.x >= 0.0f && output.x < before,
                 "a reversal must discharge stale AI work without crossing zero");
}

void test_plan_loss_decays_stale_force_without_reversal() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 20; ++i) shaper.shape({0.6f, 0.0f}, {}, plan, 0.01f);
    const auto before = shaper.current();
    pipeline_contract::TargetPlan missing{};
    const auto after = shaper.shape({}, {}, missing, 0.01f);
    require_true(before.x > 0.0f && after.x >= 0.0f && after.x < before.x,
                 "plan loss must monotonically discharge stale AI force");
}

void test_cue_continuation_never_ramps_without_observation() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto continued = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    require_true(continued.x <= observed.x + 0.0001f,
                 "cue continuation must not increase assist without a new observation");
}

void test_cue_continuation_cannot_add_force_from_downstream_hint() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, {}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto continued = shaper.shape(
        {0.8f, 0.8f}, {}, plan, 0.001f, {1.0f, 0.0f});
    require_true(continued.x <= observed.x + 0.0001f,
                 "cue continuation must not add force through a downstream hint");
    require_true(std::fabs(continued.y) <= 0.0001f,
                 "unconfirmed Y must retain blind-rise protection");
}

void test_confirmed_wrong_axis_does_not_ramp_when_assist_is_weaker() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    pipeline_contract::IntentState intent{};
    intent.filtered_right.x = -0.40f;
    const auto observed = shaper.shape({0.20f, 0.0f}, intent, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto continued = shaper.shape(
        {0.20f, 0.0f}, intent, plan, 0.001f, {1.0f, 0.0f});
    require_true(continued.x <= observed.x + 0.0001f,
                 "weak assist must not ramp against stronger manual input");
}

void test_manual_ownership_is_not_duplicated_in_shaper() {
    controller_native::AimDynamicsShaper neutral_shaper;
    controller_native::AimDynamicsShaper manual_shaper;
    const auto plan = active_plan();
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    correction.right_x.confidence = 1.0f;
    const auto neutral = neutral_shaper.shape({1.0f, 0.0f}, {}, plan, 0.05f);
    const auto opposed = manual_shaper.shape({1.0f, 0.0f}, correction, plan, 0.05f);
    require_true(std::fabs(opposed.x - neutral.x) <= 0.0001f,
                 "manual ownership must be resolved only by the fusion stage");
}

void test_shaper_never_amplifies_work_after_controller_reduces_request() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 12; ++i) {
        (void)shaper.shape({0.60f, 0.0f}, {}, plan, 0.01f);
    }
    const auto reduced = shaper.shape({0.10f, 0.0f}, {}, plan, 0.001f);
    require_true(reduced.x >= 0.10f && reduced.x < 0.60f,
                 "a reduced request must produce monotonic bounded discharge");
}

void test_shaper_reversal_must_discharge_old_direction_first() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 12; ++i) {
        (void)shaper.shape({0.60f, 0.0f}, {}, plan, 0.01f);
    }
    const auto discharge = shaper.shape({-0.60f, 0.0f}, {}, plan, 0.01f);
    require_true(discharge.x >= 0.0f && discharge.x < 0.60f,
                 "a requested reversal must discharge before driving the opposite direction");
}

void test_ads_to_bodylock_same_direction_does_not_carry_ads_force() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 42;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({1.34f, 0.0f}, {}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.0f}, {}, bodylock, 0.01f);
    require_true(output.x <= 0.581f,
                 "BodyLock handoff carried stale same-direction ADS force");
}

void test_ads_to_bodylock_opposite_direction_does_not_carry_ads_force() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 43;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({-1.34f, 0.0f}, {}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.0f}, {}, bodylock, 0.01f);
    require_true(output.x >= -0.081f,
                 "BodyLock handoff carried stale opposite-direction ADS force");
}

void test_ads_to_bodylock_vector_handoff_respects_both_components() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 44;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({1.34f, -1.0f}, {}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.20f}, {}, bodylock, 0.01f);
    require_true(output.x <= 0.581f && output.y <= 0.281f,
                 "BodyLock handoff retained stale force on a vector component");
    const float output_length = std::hypot(output.x, output.y);
    const float request_length = std::hypot(0.50f, 0.20f);
    require_true(output_length <= request_length * 1.15f + 0.001f,
                 "BodyLock handoff exceeded the new vector request envelope");
}

void test_target_change_does_not_smooth_old_target_force_into_new_target() {
    controller_native::AimDynamicsShaper shaper;
    auto old_target = active_plan();
    old_target.target_id = 100;
    for (int index = 0; index < 20; ++index) {
        (void)shaper.shape({0.90f, 0.0f}, {}, old_target, 0.01f);
    }
    auto new_target = old_target;
    new_target.target_id = 200;
    const auto output = shaper.shape({-0.20f, 0.0f}, {}, new_target, 0.01f);
    require_true(output.x >= -0.201f && output.x <= 0.201f,
                 "new target inherited stale shaper force from the old target");
}

void test_initial_target_acquisition_uses_normal_slew() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto idle = shaper.shape({}, {}, plan, 0.01f);
    require_true(std::fabs(idle.x) <= 0.0001f,
                 "idle shaper must start at a neutral AI baseline");
    plan.target_id = 300;
    const auto acquired = shaper.shape({0.90f, 0.0f}, {}, plan, 0.01f);
    require_true(acquired.x > 0.0f && acquired.x <= 0.081f,
                 "initial target acquisition bypassed the normal slew envelope");
}

void test_non_handoff_mode_change_keeps_normal_slew() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    plan.target_id = 301;
    for (int index = 0; index < 20; ++index) {
        (void)shaper.shape({0.60f, 0.0f}, {}, plan, 0.01f);
    }
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto output = shaper.shape({0.10f, 0.0f}, {}, plan, 0.01f);
    require_true(output.x < 0.60f && output.x > 0.50f,
                 "a non-handoff mode change bypassed normal decay slew");
}

}  // namespace

int main() {
    try {
        test_step_and_reversal_are_bounded();
        test_reversal_discharges_before_opposite_rise();
        test_plan_loss_decays_stale_force_without_reversal();
        test_cue_continuation_never_ramps_without_observation();
        test_cue_continuation_cannot_add_force_from_downstream_hint();
        test_confirmed_wrong_axis_does_not_ramp_when_assist_is_weaker();
        test_manual_ownership_is_not_duplicated_in_shaper();
        test_shaper_never_amplifies_work_after_controller_reduces_request();
        test_shaper_reversal_must_discharge_old_direction_first();
        test_ads_to_bodylock_same_direction_does_not_carry_ads_force();
        test_ads_to_bodylock_opposite_direction_does_not_carry_ads_force();
        test_ads_to_bodylock_vector_handoff_respects_both_components();
        test_target_change_does_not_smooth_old_target_force_into_new_target();
        test_initial_target_acquisition_uses_normal_slew();
        test_non_handoff_mode_change_keeps_normal_slew();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AimDynamicsShaperTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
