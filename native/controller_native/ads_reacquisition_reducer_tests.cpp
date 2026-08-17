#include "ads_reacquisition_reducer.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan bodylock_plan(float normalized_size = 0.2f) {
    pipeline_contract::TargetPlan plan{};
    plan.target_id = 4;
    plan.source_frame_id = 10;
    plan.selector_target_generation = 7;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.normalized_size = normalized_size;
    return plan;
}

pipeline_contract::VisionObservationBatch fresh_observation(float error_x) {
    pipeline_contract::VisionObservationBatch batch{};
    batch.capture_fresh = true;
    batch.frame_id = 11;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = 7;
    batch.preferred_source_id = 99;
    batch.count = 1;
    auto& candidate = batch.candidates[0];
    candidate.source_id = 99;
    candidate.has_aim_point = true;
    candidate.aim_px = {240.0f + error_x, 208.0f};
    candidate.normalized_size = 0.2f;
    candidate.reliability = 1.0f;
    return batch;
}

void test_initial_scope_with_existing_manual_target_begins_ads_immediately() {
    controller_native::AdsReacquisitionReducer reducer({150.0f, 4});
    auto manual_target = bodylock_plan();
    manual_target.mode = pipeline_contract::ControlMode::Manual;
    controller_native::AimScopeSnapshot scope{};
    scope.assist_active = true;
    scope.physical_ads_active = true;
    scope.physical_ads_pressed = true;
    scope.scope_acquired = true;
    const auto decided = reducer.on_input(
        scope,
        manual_target,
        pipeline_contract::EventSequence::from(10));
    require(decided.emitted && decided.request_created &&
                decided.begin_ads_epoch && !reducer.pending() &&
                decided.code == pipeline_contract::AdsReacquireDecisionCode::
                    InitialScopeAcquired,
            "initial LT with an already-visible target did not begin ADS");
}

void test_repress_waits_for_fresh_post_event_geometry() {
    controller_native::AdsReacquisitionReducer reducer({150.0f, 4});
    controller_native::AimScopeSnapshot scope{};
    scope.assist_active = true;
    scope.physical_ads_active = true;
    scope.physical_ads_pressed = true;
    const auto request = reducer.on_input(
        scope,
        bodylock_plan(),
        pipeline_contract::EventSequence::from(20));
    require(request.request_created && !request.begin_ads_epoch && reducer.pending(),
            "LT re-press did not become a deferred event");

    auto stale = fresh_observation(220.0f);
    stale.frame_id = 10;
    require(!reducer.on_fresh_observation(stale, bodylock_plan(), true).emitted,
            "pre-event frame rearmed ADS");
    const auto decided = reducer.on_fresh_observation(
        fresh_observation(220.0f), bodylock_plan(), true);
    require(decided.begin_ads_epoch &&
                decided.code == pipeline_contract::AdsReacquireDecisionCode::
                    RearmedOutsideBodylockEnvelope,
            "far post-event target did not rearm ADS");
}

void test_inside_dynamic_envelope_keeps_bodylock() {
    controller_native::AdsReacquisitionReducer reducer({150.0f, 4});
    controller_native::AimScopeSnapshot scope{};
    scope.physical_ads_active = true;
    scope.physical_ads_pressed = true;
    (void)reducer.on_input(
        scope, bodylock_plan(), pipeline_contract::EventSequence::from(30));
    const auto decided = reducer.on_fresh_observation(
        fresh_observation(100.0f), bodylock_plan(), true);
    require(!decided.begin_ads_epoch &&
                decided.code == pipeline_contract::AdsReacquireDecisionCode::
                    KeptBodylockWithinEnvelope &&
                decided.dynamic_bodylock_radius_px > 150.0f,
            "target-scaled BodyLock envelope was not used");
}

void test_generation_change_cancels_old_request() {
    controller_native::AdsReacquisitionReducer reducer({150.0f, 4});
    controller_native::AimScopeSnapshot scope{};
    scope.physical_ads_active = true;
    scope.physical_ads_pressed = true;
    (void)reducer.on_input(
        scope, bodylock_plan(), pipeline_contract::EventSequence::from(40));
    auto changed = fresh_observation(220.0f);
    changed.selector_target_generation = 8;
    const auto decided = reducer.on_fresh_observation(
        changed, bodylock_plan(), true);
    require(!decided.begin_ads_epoch &&
                decided.code == pipeline_contract::AdsReacquireDecisionCode::
                    CancelledByTargetGenerationChange,
            "old LT event acted on a replacement target");
}

}  // namespace

int main() {
    test_initial_scope_with_existing_manual_target_begins_ads_immediately();
    test_repress_waits_for_fresh_post_event_geometry();
    test_inside_dynamic_envelope_keeps_bodylock();
    test_generation_change_cancels_old_request();
    return 0;
}
