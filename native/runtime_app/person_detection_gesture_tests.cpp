#include "person_detection_gesture.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

#define REQUIRE(condition) require((condition), __LINE__)

void require(bool condition, int line) {
    if (!condition) {
        std::cerr << "require failed at line " << line << std::endl;
        std::abort();
    }
}

using Gesture = runtime_app::PersonDetectionGesture;

Gesture::Clock::time_point at_ms(int milliseconds) {
    return Gesture::Clock::time_point{} +
        std::chrono::milliseconds(milliseconds);
}

pipeline_contract::TargetPlan markable_plan(std::uint64_t generation = 7) {
    pipeline_contract::TargetPlan plan;
    plan.source_frame_id = 101;
    plan.source_observation_id = 1001;
    plan.target_id = 1;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.direct_person_observation = true;
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    plan.source_aim_px = {320.0f, 250.0f};
    plan.aim_px = {320.0f, 250.0f};
    plan.aim_region_px = {280.0f, 210.0f, 80.0f, 100.0f};
    plan.aim_region_source = pipeline_contract::AimRegionSource::VisionGeometry;
    plan.has_aim_region = true;
    plan.desired_point_normalized = {0.5f, 0.4f};
    plan.error_px = {0.0f, 0.0f};
    plan.confidence = 0.9f;
    plan.reliability = 0.9f;
    plan.visual_authority = 1.0f;
    plan.aim_authority = 1.0f;
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    plan.selector_target_generation = generation;
    plan.enemy_cue_current = true;
    plan.enemy_identity_confirmed = true;
    plan.enemy_cue_checked = true;
    return plan;
}

void request_l3(Gesture& gesture, int milliseconds = 0) {
    gesture.update_activation(true, 0.0f, at_ms(milliseconds));
}

void release_inputs(Gesture& gesture, int milliseconds) {
    gesture.update_activation(false, 0.0f, at_ms(milliseconds));
}

void confirm_twice(
    Gesture& gesture,
    const pipeline_contract::TargetPlan& plan,
    int first_ms) {
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(first_ms)));
    REQUIRE(gesture.observe_fresh_plan(plan, at_ms(first_ms + 6)));
}

void test_no_request_never_marks() {
    Gesture gesture;
    const auto plan = markable_plan();
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(0)));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(6)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(6)));
}

void test_two_fresh_final_plans_emit_one_bounded_press() {
    Gesture gesture;
    request_l3(gesture);
    const auto plan = markable_plan();

    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(1)));
    REQUIRE(gesture.observe_fresh_plan(plan, at_ms(7)));
    REQUIRE(gesture.merge_dpad_up(false, at_ms(7)));
    REQUIRE(gesture.merge_dpad_up(false, at_ms(56)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(57)));
}

void test_crosshair_must_be_inside_r() {
    Gesture gesture;
    request_l3(gesture);
    auto plan = markable_plan();
    plan.error_px = {-100.0f, 0.0f};  // crosshair x = 420

    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(7)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(7)));
    REQUIRE(gesture.status(at_ms(7)).block_reason ==
            runtime_app::PersonMarkBlockReason::CrosshairOutsideAimRegion);
}

void test_historical_plan_cannot_trigger_without_fresh_observation() {
    Gesture gesture;
    const auto plan = markable_plan();
    request_l3(gesture);
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    release_inputs(gesture, 2);
    // Controller-rate replay ticks call neither observe_fresh_plan nor any
    // evidence-hold API, so elapsed time cannot complete confirmation.
    gesture.update_activation(false, 0.0f, at_ms(100));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(100)));
}

void test_direct_person_and_current_enemy_cue_are_both_required() {
    Gesture not_person;
    request_l3(not_person);
    auto plan = markable_plan();
    plan.direct_person_observation = false;
    REQUIRE(!not_person.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(not_person.status(at_ms(1)).block_reason ==
            runtime_app::PersonMarkBlockReason::NotDirectPerson);

    Gesture no_cue;
    request_l3(no_cue);
    plan = markable_plan();
    plan.enemy_cue_current = false;
    REQUIRE(!no_cue.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(no_cue.status(at_ms(1)).block_reason ==
            runtime_app::PersonMarkBlockReason::NoCurrentEnemyCue);
}

void test_selector_rejected_candidate_plan_never_marks() {
    Gesture gesture;
    request_l3(gesture);
    auto plan = markable_plan();
    // Selector corpse/friendly regressions publish no direct current person
    // plan. Prove the downstream digital gate fails closed even if unrelated
    // cue-looking pixels are present in the synthetic boundary fixture.
    plan.direct_person_observation = false;
    plan.reliability = 0.20f;
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(7)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(7)));
    REQUIRE(gesture.status(at_ms(7)).block_reason ==
            runtime_app::PersonMarkBlockReason::NotDirectPerson);
}

void test_cue_only_continuation_never_marks() {
    Gesture gesture;
    request_l3(gesture);
    auto plan = markable_plan();
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    plan.cue_continuation = true;
    plan.direct_person_observation = false;
    plan.source_observation_id = 0;

    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(7)));
    REQUIRE(gesture.status(at_ms(7)).block_reason ==
            runtime_app::PersonMarkBlockReason::CueContinuation);
}

void test_invalid_d_or_r_never_marks() {
    Gesture invalid_region;
    request_l3(invalid_region);
    auto plan = markable_plan();
    plan.aim_region_px.w = 0.0f;
    REQUIRE(!invalid_region.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(!invalid_region.merge_dpad_up(false, at_ms(1)));

    Gesture d_outside;
    request_l3(d_outside);
    plan = markable_plan();
    plan.aim_px.x = 500.0f;
    REQUIRE(!d_outside.observe_fresh_plan(plan, at_ms(1)));
    REQUIRE(d_outside.status(at_ms(1)).block_reason ==
            runtime_app::PersonMarkBlockReason::InvalidPlan);
}

void test_generation_change_restarts_two_frame_confirmation() {
    Gesture gesture;
    request_l3(gesture);
    const auto a = markable_plan(10);
    const auto b = markable_plan(11);

    REQUIRE(!gesture.observe_fresh_plan(a, at_ms(1)));
    REQUIRE(!gesture.observe_fresh_plan(b, at_ms(7)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(7)));
    REQUIRE(gesture.observe_fresh_plan(b, at_ms(13)));
}

void test_l3_and_lt_share_once_per_generation_budget() {
    Gesture gesture;
    request_l3(gesture);
    const auto plan = markable_plan(20);
    confirm_twice(gesture, plan, 1);

    release_inputs(gesture, 60);
    gesture.update_activation(false, 0.06f, at_ms(70));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(71)));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(77)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(77)));
    REQUIRE(gesture.status(at_ms(77)).block_reason ==
            runtime_app::PersonMarkBlockReason::AlreadyMarkedGeneration);
}

void test_l3_request_uses_configured_cooldown() {
    Gesture gesture{std::chrono::milliseconds(300)};
    request_l3(gesture);
    confirm_twice(gesture, markable_plan(25), 1);

    release_inputs(gesture, 60);
    request_l3(gesture, 100);
    REQUIRE(!gesture.status(at_ms(100)).request_pending);

    release_inputs(gesture, 101);
    request_l3(gesture, 299);
    REQUIRE(!gesture.status(at_ms(299)).request_pending);

    release_inputs(gesture, 300);
    request_l3(gesture, 300);
    REQUIRE(gesture.status(at_ms(300)).request_pending);
}

void test_lt_bypasses_l3_cooldown_for_new_generation() {
    Gesture gesture;
    request_l3(gesture);
    confirm_twice(gesture, markable_plan(30), 1);

    release_inputs(gesture, 60);
    gesture.update_activation(false, 0.06f, at_ms(70));
    confirm_twice(gesture, markable_plan(31), 71);
    REQUIRE(gesture.status(at_ms(77)).last_marked_generation == 31);
}

void test_lt_request_uses_configured_cooldown() {
    Gesture gesture{
        std::chrono::milliseconds(1000),
        std::chrono::milliseconds(300)};
    gesture.update_activation(false, 0.06f, at_ms(0));
    confirm_twice(gesture, markable_plan(25), 1);

    gesture.update_activation(false, 0.0f, at_ms(60));
    gesture.update_activation(false, 0.06f, at_ms(100));
    REQUIRE(!gesture.status(at_ms(100)).request_pending);

    gesture.update_activation(false, 0.0f, at_ms(101));
    gesture.update_activation(false, 0.06f, at_ms(299));
    REQUIRE(!gesture.status(at_ms(299)).request_pending);

    gesture.update_activation(false, 0.0f, at_ms(300));
    gesture.update_activation(false, 0.06f, at_ms(300));
    REQUIRE(gesture.status(at_ms(300)).request_pending);
}

void test_same_generation_can_mark_in_a_new_vision_scope() {
    Gesture gesture;
    request_l3(gesture);
    const auto plan = markable_plan(1);
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1), 10));
    REQUIRE(gesture.observe_fresh_plan(plan, at_ms(7), 10));

    release_inputs(gesture, 60);
    gesture.update_activation(false, 0.06f, at_ms(70));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(71), 11));
    REQUIRE(gesture.observe_fresh_plan(plan, at_ms(77), 11));
    const auto status = gesture.status(at_ms(77));
    REQUIRE(status.last_marked_scope == 11);
    REQUIRE(status.last_marked_generation == 1);
}

void test_request_expires_but_does_not_reuse_old_evidence() {
    Gesture gesture;
    request_l3(gesture);
    const auto plan = markable_plan();
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(1)));
    gesture.update_activation(true, 0.0f, at_ms(251));
    REQUIRE(!gesture.observe_fresh_plan(plan, at_ms(252)));
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(252)));
    REQUIRE(gesture.status(at_ms(252)).block_reason ==
            runtime_app::PersonMarkBlockReason::RequestExpired);
}

void test_physical_dpad_up_always_passes_through() {
    Gesture gesture;
    REQUIRE(gesture.merge_dpad_up(true, at_ms(0)));
    REQUIRE(gesture.merge_dpad_up(true, at_ms(1000)));
}

void test_reset_clears_pending_press_and_generation_memory() {
    Gesture gesture;
    request_l3(gesture);
    confirm_twice(gesture, markable_plan(40), 1);
    gesture.reset();
    REQUIRE(!gesture.merge_dpad_up(false, at_ms(8)));

    request_l3(gesture, 9);
    confirm_twice(gesture, markable_plan(40), 10);
    REQUIRE(gesture.merge_dpad_up(false, at_ms(16)));
}

}  // namespace

int main() {
    test_no_request_never_marks();
    test_two_fresh_final_plans_emit_one_bounded_press();
    test_crosshair_must_be_inside_r();
    test_historical_plan_cannot_trigger_without_fresh_observation();
    test_direct_person_and_current_enemy_cue_are_both_required();
    test_selector_rejected_candidate_plan_never_marks();
    test_cue_only_continuation_never_marks();
    test_invalid_d_or_r_never_marks();
    test_generation_change_restarts_two_frame_confirmation();
    test_l3_and_lt_share_once_per_generation_budget();
    test_l3_request_uses_configured_cooldown();
    test_lt_bypasses_l3_cooldown_for_new_generation();
    test_lt_request_uses_configured_cooldown();
    test_same_generation_can_mark_in_a_new_vision_scope();
    test_request_expires_but_does_not_reuse_old_evidence();
    test_physical_dpad_up_always_passes_through();
    test_reset_clears_pending_press_and_generation_memory();
    std::cout << "person mark final-plan gate tests passed\n";
    return 0;
}
