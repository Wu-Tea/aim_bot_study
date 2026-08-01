#include "vector_intent_fuser.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using controller_native::FusionCandidate;
using controller_native::FusionFallbackReason;
using controller_native::VectorIntentFuser;
using controller_native::VectorIntentFusionInput;

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

VectorIntentFusionInput input_for(
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai) {
    VectorIntentFusionInput input{};
    input.manual_stick = manual;
    input.shaped_ai_stick = ai;
    input.manual_confidence = 1.0f;
    input.plan.target_id = 7;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.plan.reliability = 1.0f;
    input.plan.confidence = 1.0f;
    input.plan.aim_authority = 1.0f;
    input.plan.error_px = {24.0f, 0.0f};
    return input;
}

void test_aligned_input_keeps_full_mix() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({0.20f, 0.0f}, {0.30f, 0.0f}), 0.001f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "aligned input must retain the complete AI proposal");
    require_near(decision.fused_stick.x, 0.50f, 0.0001f,
                 "aligned manual and AI input must add normally");
}

void test_opposing_input_preserves_manual_and_continuously_retires_ai() {
    VectorIntentFuser fuser;
    float previous_ai = 2.0f;
    for (float manual : {-0.10f, -0.20f, -0.30f, -0.40f, -0.46f}) {
        controller_native::VectorIntentFusionDecision decision{};
        for (int tick = 0; tick < 8; ++tick) {
            decision = fuser.update(
                input_for({manual, 0.0f}, {0.50f, 0.0f}), 0.001f);
        }
        const float effective_ai = decision.fused_stick.x - manual;
        require_true(effective_ai <= previous_ai + 0.025f,
                     "AI brake reasserted discontinuously as counter-steer increased");
        require_near(decision.applied_manual_weight, 1.0f, 0.0001f,
                     "fusion must never rewrite physical manual input");
        previous_ai = effective_ai;
    }
    require_true(previous_ai <= 0.208f,
                 "opposing AI must preserve the configured share of manual input");
}

void test_escape_threshold_is_not_a_control_switch() {
    VectorIntentFuser fuser;
    float previous = 0.0f;
    float maximum_threshold_jump = 0.0f;
    bool first = true;
    for (float manual : {-0.30f, -0.36f, -0.42f, -0.46f,
                         -0.42f, -0.36f, -0.30f}) {
        const auto decision = fuser.update(
            input_for({manual, 0.0f}, {0.50f, 0.0f}), 0.001f);
        if (!first && std::fabs(manual) >= 0.42f) {
            maximum_threshold_jump = std::max(
                maximum_threshold_jump,
                std::fabs(decision.fused_stick.x - previous));
        }
        previous = decision.fused_stick.x;
        first = false;
    }
    require_true(maximum_threshold_jump <= 0.12f,
                 "manual threshold crossing created an output impulse");
}

void test_fresh_vision_does_not_override_countersteer() {
    VectorIntentFuser normal;
    VectorIntentFuser fresh;
    auto normal_input = input_for({-0.30f, 0.0f}, {0.50f, 0.20f});
    auto fresh_input = normal_input;
    fresh_input.fresh_single_target_observation = true;
    const auto a = normal.update(normal_input, 0.001f);
    const auto b = fresh.update(fresh_input, 0.001f);
    require_near(a.fused_stick.x, b.fused_stick.x, 0.0001f,
                 "fresh Vision must not create a second ownership pulse");
    require_near(a.fused_stick.y, 0.20f, 0.0001f,
                 "AI motion orthogonal to the conflict must remain complete");
}

void test_reacquire_and_target_change_fail_safe_to_manual() {
    VectorIntentFuser fuser;
    (void)fuser.update(input_for({}, {0.50f, 0.0f}), 0.001f);

    auto changed = input_for({-0.20f, 0.10f}, {0.50f, 0.0f});
    changed.plan.target_id = 8;
    const auto target_change = fuser.update(changed, 0.001f);
    require_true(target_change.reason == FusionFallbackReason::TargetChanged,
                 "target identity transition must be explicit");
    require_near(target_change.fused_stick.x, -0.20f, 0.0001f,
                 "new target must not inherit old AI ownership");

    changed.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto reacquire = fuser.update(changed, 0.001f);
    require_true(reacquire.reason == FusionFallbackReason::Reacquiring,
                 "reacquisition must fail safe to manual ownership");
    require_near(reacquire.fused_stick.y, 0.10f, 0.0001f,
                 "reacquisition must preserve physical input exactly");
}

void test_reliability_boundary_does_not_drop_and_reassert_ai() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.55f, 0.0f});
    input.plan.reliability = 0.70f;
    auto previous = fuser.update(input, 0.001f).fused_stick;

    for (const float reliability : {0.64f, 0.68f, 0.63f, 0.72f}) {
        input.plan.reliability = reliability;
        const auto decision = fuser.update(input, 0.001f);
        require_true(
            std::fabs(decision.fused_stick.x - previous.x) <= 0.081f,
            "reliability boundary created a full AI off/on output impulse");
        previous = decision.fused_stick;
    }
}

void test_reacquire_to_observed_reenters_through_existing_slew() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    auto previous = fuser.update(input, 0.001f).fused_stick;

    input.manual_stick = {0.05f, -0.02f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto reacquiring = fuser.update(input, 0.001f);
    require_true(
        std::hypot(reacquiring.fused_stick.x - previous.x,
                   reacquiring.fused_stick.y - previous.y) <= 0.201f,
        "entering reacquiring hard-cut the existing AI output");
    previous = reacquiring.fused_stick;

    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    const auto observed = fuser.update(input, 0.001f);
    require_true(
        std::hypot(observed.fused_stick.x - previous.x,
                   observed.fused_stick.y - previous.y) <= 0.081f,
        "reacquire to observed cold-started full AI instead of reasserting by slew");
}

void test_reacquiring_release_edge_is_bounded() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    const auto active = fuser.update(input, 0.001f);
    input.manual_stick = {0.05f, 0.0f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto released = fuser.update(input, 0.001f);
    require_true(
        std::fabs(released.fused_stick.x - active.fused_stick.x) <= 0.201f,
        "reacquiring release exceeded the deliberate manual release envelope");
    require_true(released.applied_ai_weight > 0.0f,
                 "reacquiring release discarded all output continuity state");
}

void test_full_manual_escape_preempts_reacquiring_release_slew() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.manual_stick = {-1.0f, 0.0f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto escaped = fuser.update(input, 0.001f);
    require_true(escaped.manual_escape,
                 "full manual escape must preempt reacquiring release slew");
    require_true(escaped.reason == FusionFallbackReason::ManualEscape,
                 "reacquiring full escape must report manual ownership");
    require_true(escaped.candidate == FusionCandidate::ManualOnly,
                 "reacquiring full escape must disallow AI ownership");
    require_near(escaped.fused_stick.x, -1.0f, 0.0001f,
                 "reacquiring must not delay full physical counter-steer");
}

void test_full_manual_escape_cannot_be_blocked_by_saturated_ai() {
    VectorIntentFuser fuser;
    const auto input = input_for({-1.0f, 0.0f}, {1.34f, 0.0f});
    const auto escaped = fuser.update(input, 0.001f);
    require_true(escaped.manual_escape,
                 "saturated shaped AI must not block full manual escape");
    require_true(escaped.candidate == FusionCandidate::ManualOnly,
                 "saturated AI escape must transfer ownership to manual");
    require_near(escaped.fused_stick.x, -1.0f, 0.0001f,
                 "saturated AI escape must preserve exact physical input");
}

void test_diagonal_manual_escape_is_preserved_exactly() {
    VectorIntentFuser fuser;
    const auto input = input_for({-0.50f, 0.40f}, {0.40f, -0.30f});
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.manual_escape,
                 "deliberate diagonal input must be classified as escape");
    require_true(decision.candidate == FusionCandidate::ManualOnly,
                 "manual escape must disallow AI-owned candidates");
    require_near(decision.fused_stick.x, input.manual_stick.x, 0.0001f,
                 "manual escape X must remain exact physical input");
    require_near(decision.fused_stick.y, input.manual_stick.y, 0.0001f,
                 "manual escape Y must remain exact physical input");
}

void test_nonfinite_input_returns_exact_physical_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, -0.10f}, {0.30f, 0.20f});
    input.plan.error_px.x = std::numeric_limits<float>::quiet_NaN();
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fallback,
                 "non-finite plan must use the finite manual fallback");
    require_near(decision.fused_stick.x, 0.20f, 0.0001f,
                 "non-finite fallback X must preserve physical input");
    require_near(decision.fused_stick.y, -0.10f, 0.0001f,
                 "non-finite fallback Y must preserve physical input");
}

void test_remaining_work_rotation_does_not_reproject_stable_manual_input() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.38f, 0.02f}, {0.24f, 0.0f});
    input.plan.error_px = {30.0f, 2.0f};
    input.plan.remaining_work_px = input.plan.error_px;
    input.plan.remaining_work_confidence = 1.0f;
    input.plan.remaining_work_valid = true;
    input.fresh_single_target_observation = true;

    const auto before = fuser.update(input, 0.024f);
    input.plan.error_px = {2.0f, -30.0f};
    input.plan.remaining_work_px = input.plan.error_px;
    input.plan.delivered_camera_motion_since_capture_px = {28.0f, 32.0f};
    const auto after = fuser.update(input, 0.001f);
    require_true(
        std::hypot(after.fused_stick.x - before.fused_stick.x,
                   after.fused_stick.y - before.fused_stick.y) < 0.05f,
        "rotating Remaining error must not rotate the manual projection basis");
}

void test_visual_reference_rotation_is_not_a_one_tick_output_rotation() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.38f, 0.02f}, {0.24f, 0.0f});
    input.plan.error_px = {30.0f, 2.0f};
    input.fresh_single_target_observation = true;

    const auto before = fuser.update(input, 0.024f);
    input.plan.error_px = {2.0f, -30.0f};
    const auto after = fuser.update(input, 0.001f);
    require_true(
        std::hypot(after.fused_stick.x - before.fused_stick.x,
                   after.fused_stick.y - before.fused_stick.y) < 0.10f,
        "a one-frame visual direction change must not rotate output instantly");
}

void test_target_change_reenters_from_manual_baseline() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.plan.target_id = 8;
    input.manual_stick = {0.05f, 0.0f};
    const auto changed = fuser.update(input, 0.001f);
    require_true(changed.reason == FusionFallbackReason::TargetChanged,
                 "target change must still be explicit");

    const auto reentered = fuser.update(input, 0.001f);
    require_true(
        std::fabs(reentered.fused_stick.x - changed.fused_stick.x) <= 0.081f,
        "new target re-entry inherited a full old/new AI impulse");
}

void test_no_target_replacement_uses_manual_admission_tick() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.plan.target_id = 0;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::None;
    input.manual_stick = {-0.04f, 0.03f};
    const auto no_target = fuser.update(input, 0.001f);
    require_near(no_target.fused_stick.x, -0.04f, 0.0001f,
                 "no-target re-entry baseline must preserve manual X");

    input.plan.target_id = 9;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    const auto admitted = fuser.update(input, 0.001f);
    require_true(admitted.reason == FusionFallbackReason::TargetChanged,
                 "replacement after a no-target gap must remain an explicit target change");
    require_near(admitted.fused_stick.x, input.manual_stick.x, 0.0001f,
                 "replacement admission tick must preserve exact manual X");
    require_near(admitted.fused_stick.y, input.manual_stick.y, 0.0001f,
                 "replacement admission tick must preserve exact manual Y");

    const auto reentered = fuser.update(input, 0.001f);
    require_true(
        std::hypot(reentered.fused_stick.x - admitted.fused_stick.x,
                   reentered.fused_stick.y - admitted.fused_stick.y) <= 0.081f,
        "replacement target re-entry bypassed the post-admission slew");
}

void test_no_target_is_exact_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.17f, -0.23f}, {0.60f, 0.60f});
    input.plan.target_id = 0;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::None;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.17f, 0.0001f,
                 "no-target X must be exact manual");
    require_near(decision.fused_stick.y, -0.23f, 0.0001f,
                 "no-target Y must be exact manual");
}

}  // namespace

int main() {
    try {
        test_aligned_input_keeps_full_mix();
        test_opposing_input_preserves_manual_and_continuously_retires_ai();
        test_escape_threshold_is_not_a_control_switch();
        test_fresh_vision_does_not_override_countersteer();
        test_reacquire_and_target_change_fail_safe_to_manual();
        test_reliability_boundary_does_not_drop_and_reassert_ai();
        test_reacquire_to_observed_reenters_through_existing_slew();
        test_reacquiring_release_edge_is_bounded();
        test_full_manual_escape_preempts_reacquiring_release_slew();
        test_full_manual_escape_cannot_be_blocked_by_saturated_ai();
        test_diagonal_manual_escape_is_preserved_exactly();
        test_nonfinite_input_returns_exact_physical_manual();
        test_remaining_work_rotation_does_not_reproject_stable_manual_input();
        test_visual_reference_rotation_is_not_a_one_tick_output_rotation();
        test_target_change_reenters_from_manual_baseline();
        test_no_target_replacement_uses_manual_admission_tick();
        test_no_target_is_exact_manual();
        std::cout << "[VectorIntentFuserTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[VectorIntentFuserTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
