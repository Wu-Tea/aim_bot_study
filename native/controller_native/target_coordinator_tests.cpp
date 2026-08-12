#include "target_coordinator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require_true(bool value, const char* message) {
    if (!value) {
        std::cerr << "[TargetCoordinatorTests] FAIL: " << message << '\n';
        std::abort();
    }
}

bool near(float left, float right, float tolerance = 0.001f) {
    return std::fabs(left - right) <= tolerance;
}

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent;
    intent.ads = true;
    return intent;
}

pipeline_contract::IntentState correcting_intent(float x, float y) {
    auto intent = ads_intent();
    intent.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.raw_right = {x, y};
    intent.filtered_right = {x, y};
    intent.right_x.filtered = x;
    intent.right_y.filtered = y;
    return intent;
}

pipeline_contract::VisionObservationBatch selected_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float aim_x,
    float aim_y = 208.0f,
    std::uint64_t source_id = 41,
    std::uint64_t selector_generation = 7) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = selector_generation;
    batch.preferred_source_id = source_id;
    batch.count = 1;
    auto& candidate = batch.candidates[0];
    candidate.source_id = source_id;
    candidate.aim_px = {aim_x, aim_y};
    candidate.has_aim_point = true;
    candidate.aim_region_px = {aim_x - 24.0f, aim_y - 40.0f, 48.0f, 80.0f};
    candidate.aim_region_source =
        pipeline_contract::AimRegionSource::VisionGeometry;
    candidate.has_aim_region = true;
    candidate.body_box_px = candidate.aim_region_px;
    candidate.has_body_box = true;
    candidate.box_size_px = {48.0f, 112.0f};
    candidate.confidence = 0.95f;
    candidate.reliability = 0.90f;
    candidate.normalized_size = 0.25f;
    candidate.body_cue = true;
    return batch;
}

pipeline_contract::VisionObservationBatch no_source_tick() {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    return batch;
}

pipeline_contract::VisionObservationBatch fresh_no_selection(
    std::uint64_t frame_id,
    double capture_seconds,
    std::uint64_t selector_generation = 7) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = selector_generation;
    batch.preferred_source_id = 0;
    return batch;
}

pipeline_contract::VisionObservationBatch cue_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float aim_x,
    std::uint64_t selector_generation = 7) {
    auto batch = fresh_no_selection(
        frame_id, capture_seconds, selector_generation);
    batch.selector_cue_continuation = true;
    batch.count = 1;
    auto& cue = batch.candidates[0];
    cue.source_id = 0;
    cue.aim_px = {aim_x, 208.0f};
    cue.has_aim_point = true;
    cue.aim_region_px = {aim_x - 24.0f, 168.0f, 48.0f, 80.0f};
    cue.aim_region_source =
        pipeline_contract::AimRegionSource::CueTranslated;
    cue.has_aim_region = true;
    cue.confidence = 0.8f;
    cue.cue_confidence = 0.9f;
    cue.reliability = 0.7f;
    return batch;
}

void test_no_source_tick_reuses_immutable_source_plan() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(1, 1.0);
    const auto observed = coordinator.update(
        selected_frame(1, 1.0, 280.0f), intent, 1.0);
    const auto continued = coordinator.update(
        no_source_tick(), intent, 1.005);

    require_true(observed.target_id != 0, "fresh selected target must be admitted");
    require_true(continued.target_id == observed.target_id,
                 "no-source tick must keep target identity");
    require_true(near(continued.aim_px.x, observed.aim_px.x) &&
                     near(continued.error_px.x, observed.error_px.x),
                 "no-source tick must not project geometry");
    require_true(near(continued.aim_authority, observed.aim_authority),
                 "no-source tick must not decay authority");
    require_true(continued.lifecycle == observed.lifecycle,
                 "no-source tick must not invent a lifecycle transition");
    require_true(!continued.source_decision_available,
                 "no-source tick must not fabricate a source decision");
}

void test_fresh_no_selection_drops_generic_authority() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(2, 2.0);
    const auto observed = coordinator.update(
        selected_frame(10, 2.0, 270.0f), intent, 2.0);
    const auto lost = coordinator.update(
        fresh_no_selection(11, 2.006), intent, 2.006);

    require_true(observed.aim_authority > 0.0f, "fresh target must own aim authority");
    require_true(lost.target_id == 0 && lost.aim_authority == 0.0f,
                 "fresh selector no-selection must remove target authority");
    require_true(lost.mode == pipeline_contract::ControlMode::Manual,
                 "fresh no-target plan must return to manual output");
    require_true(lost.source_decision_available &&
                     lost.source_decision_outcome ==
                         pipeline_contract::SourceDecisionOutcome::Rejected &&
                     lost.source_decision_reason ==
                         pipeline_contract::AdsDecisionReason::SelectorNoSelection,
                 "fresh no-selection must retain its evidence reason");
}

void test_fresh_empty_frame_drops_target() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(3, 3.0);
    (void)coordinator.update(selected_frame(20, 3.0, 265.0f), intent, 3.0);
    auto empty = fresh_no_selection(21, 3.006);
    empty.selector_identity_protocol = false;
    const auto lost = coordinator.update(empty, intent, 3.006);
    require_true(lost.target_id == 0 && lost.aim_authority == 0.0f,
                 "fresh empty detector frame must release the old target immediately");
    require_true(lost.source_decision_reason ==
                     pipeline_contract::AdsDecisionReason::InvalidSelectorProtocol,
                 "fresh frame without selector identity must fail closed");
}

void test_hard_source_age_gate_releases_without_gradual_decay() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 20.0f;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(4, 4.0);
    const auto observed = coordinator.update(
        selected_frame(30, 4.0, 275.0f), intent, 4.0);
    const auto before_expiry = coordinator.update(
        no_source_tick(), intent, 4.019);
    const auto expired = coordinator.update(
        no_source_tick(), intent, 4.021);

    require_true(near(before_expiry.aim_authority, observed.aim_authority),
                 "authority must remain flat before the hard age gate");
    require_true(expired.target_id == 0 && expired.aim_authority == 0.0f,
                 "hard source-age gate must release stale authority");
}

void test_velocity_updates_only_on_fresh_capture() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(5, 5.0);
    (void)coordinator.update(selected_frame(40, 5.0, 260.0f), intent, 5.0);
    const auto moved = coordinator.update(
        selected_frame(41, 5.010, 270.0f), intent, 5.010);
    const auto between = coordinator.update(
        no_source_tick(), intent, 5.015);

    require_true(moved.velocity_px_per_sec.x > 900.0f,
                 "fresh inter-capture displacement must update velocity");
    require_true(near(between.aim_px.x, 270.0f),
                 "controller ticks must not integrate fresh velocity into position");
    require_true(near(between.velocity_px_per_sec.x,
                      moved.velocity_px_per_sec.x),
                 "controller tick must preserve source-derived velocity metadata");
}

void test_duplicate_and_stale_captures_cannot_replace_geometry() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(6, 6.0);
    const auto first = coordinator.update(
        selected_frame(50, 6.0, 266.0f), intent, 6.0);

    auto duplicate = selected_frame(50, 6.0, 330.0f);
    const auto duplicate_plan = coordinator.update(duplicate, intent, 6.005);
    require_true(near(duplicate_plan.aim_px.x, first.aim_px.x),
                 "duplicate capture must not update geometry");

    auto stale = selected_frame(51, 5.0, 340.0f);
    const auto stale_plan = coordinator.update(stale, intent, 6.010);
    require_true(near(stale_plan.aim_px.x, first.aim_px.x),
                 "stale capture must not update geometry");
}

void test_same_generation_cue_is_only_continuity_path() {
    controller_native::TargetCoordinator coordinator;
    auto intent = ads_intent();
    intent.fire = true;
    coordinator.begin_ads_epoch(7, 7.0);
    auto observed_batch = selected_frame(60, 7.0, 270.0f);
    observed_batch.fire_requested = true;
    observed_batch.observed_fire_eligible = true;
    const auto observed = coordinator.update(observed_batch, intent, 7.0);
    const auto cue = coordinator.update(
        cue_frame(61, 7.006, 274.0f), intent, 7.006);

    require_true(cue.target_id == observed.target_id && cue.cue_continuation,
                 "same-generation cue must continue only the owned target");
    require_true(cue.lifecycle == pipeline_contract::TargetLifecycle::CueContinuation,
                 "cue lifecycle must remain explicitly distinguishable");
    require_true(near(cue.aim_px.x, 274.0f),
                 "cue evidence may update current geometry");
    require_true(!cue.fire_authority && !cue.fire_requested,
                 "cue continuation must remain aim-only");

    const auto wrong_generation = coordinator.update(
        cue_frame(62, 7.012, 276.0f, 8), intent, 7.012);
    require_true(wrong_generation.target_id == 0 &&
                     wrong_generation.aim_authority == 0.0f,
                 "wrong-generation cue must not become generic hold authority");
}

void test_selector_generation_replacement_resets_target_identity() {
    controller_native::TargetCoordinator coordinator;
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(8, 8.0);
    const auto first = coordinator.update(
        selected_frame(70, 8.0, 268.0f, 208.0f, 41, 7),
        intent,
        8.0);
    auto replacement = selected_frame(71, 8.006, 300.0f, 208.0f, 52, 8);
    replacement.selector_target_changed = true;
    const auto second = coordinator.update(replacement, intent, 8.006);

    require_true(second.target_id != 0 && second.target_id != first.target_id,
                 "selector generation replacement must allocate new target identity");
    require_true(second.selector_target_changed,
                 "plan must expose the consumed replacement boundary");
    require_true(near(second.aim_px.x, 300.0f),
                 "replacement must consume current source geometry directly");
    require_true(second.target_acquisition_id != 0 &&
                     second.target_acquisition_id != first.target_acquisition_id &&
                     second.mode == pipeline_contract::ControlMode::AdsAcquire &&
                     second.ads_acquisition_active,
                 "replacement must receive a fresh full-authority ADS acquisition");
}

void test_replacement_rearms_ads_after_previous_target_consumed() {
    controller_native::TargetCoordinatorConfig config;
    config.settle_frames = 1;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(81, 8.1);
    const auto first = coordinator.update(
        selected_frame(710, 8.1, 240.0f, 208.0f, 71, 17),
        intent,
        8.1);
    require_true(first.mode == pipeline_contract::ControlMode::BodyLockFollow &&
                     !first.ads_acquisition_active,
                 "fixture must consume the first target's settled ADS job");

    auto replacement = selected_frame(
        711, 8.106, 330.0f, 208.0f, 72, 18);
    replacement.selector_target_changed = true;
    const auto second = coordinator.update(replacement, intent, 8.106);
    require_true(second.target_id != first.target_id &&
                     second.target_acquisition_id != 0 &&
                     second.target_acquisition_id != first.target_acquisition_id,
                 "replacement must allocate a new identity-scoped acquisition");
    require_true(second.mode == pipeline_contract::ControlMode::AdsAcquire &&
                     second.ads_acquisition_active &&
                     second.aim_authority >= 0.999f,
                 "held-LT transfer must re-enter full ADS instead of BodyLock");
}

void test_manual_correction_moves_d_only_inside_r() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 500.0f;
    config.desired_point_traversal_ms = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = correcting_intent(0.0f, -0.80f);
    coordinator.begin_ads_epoch(9, 9.0);
    const auto observed = coordinator.update(
        selected_frame(80, 9.0, 240.0f, 208.0f), intent, 9.0);
    const auto corrected = coordinator.update(
        no_source_tick(), intent, 9.050);

    require_true(corrected.manual_correction_y,
                 "single-target downward intent must be interpreted as D correction");
    require_true(near(corrected.source_aim_px.y, observed.source_aim_px.y),
                 "manual correction must not mutate source geometry");
    require_true(corrected.aim_px.y > observed.aim_px.y,
                 "downward correction must move D toward larger screen Y");
    require_true(corrected.aim_px.y >= corrected.aim_region_px.y &&
                     corrected.aim_px.y <=
                         corrected.aim_region_px.y + corrected.aim_region_px.h,
                 "D must remain inside R");
    require_true(corrected.desired_point_source ==
                     pipeline_contract::DesiredPointSource::UserCorrected,
                 "plan must expose user-corrected D ownership");
}

void test_cue_carries_corrected_d_instead_of_replacing_it() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 500.0f;
    config.desired_point_traversal_ms = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = correcting_intent(0.0f, -0.80f);
    coordinator.begin_ads_epoch(10, 10.0);
    (void)coordinator.update(
        selected_frame(90, 10.0, 240.0f, 208.0f), intent, 10.0);
    const auto corrected = coordinator.update(
        no_source_tick(), intent, 10.050);

    auto cue_batch = cue_frame(91, 10.056, 245.0f);
    cue_batch.candidates[0].aim_px.y = 180.0f;
    cue_batch.candidates[0].aim_region_px = {221.0f, 173.0f, 48.0f, 80.0f};
    const auto cue = coordinator.update(cue_batch, ads_intent(), 10.056);

    require_true(cue.cue_continuation,
                 "same-generation cue must preserve target ownership");
    require_true(near(
                     cue.desired_point_normalized.y,
                     corrected.desired_point_normalized.y,
                     0.001f),
                 "cue must carry the same target-relative D coordinate");
    require_true(!near(cue.aim_px.y, cue.source_aim_px.y, 1.0f),
                 "cue source point must not overwrite a corrected D");
    require_true(cue.aim_region_source ==
                     pipeline_contract::AimRegionSource::CueTranslated,
                 "cue-translated R must be explicit in the plan");
}

void test_target_replacement_resets_corrected_d() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 500.0f;
    config.desired_point_traversal_ms = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = correcting_intent(0.0f, -0.80f);
    coordinator.begin_ads_epoch(11, 11.0);
    (void)coordinator.update(
        selected_frame(100, 11.0, 240.0f, 208.0f, 41, 7),
        intent,
        11.0);
    const auto corrected = coordinator.update(
        no_source_tick(), intent, 11.050);
    require_true(corrected.desired_point_normalized.y > 0.5f,
                 "test trigger must establish a corrected D");

    auto replacement = selected_frame(
        101, 11.056, 280.0f, 220.0f, 52, 8);
    replacement.selector_target_changed = true;
    const auto replaced = coordinator.update(replacement, ads_intent(), 11.056);
    require_true(replaced.target_id != corrected.target_id,
                 "replacement must allocate a new I");
    require_true(near(replaced.aim_px.y, replaced.source_aim_px.y),
                 "new I must reset D to the new Vision default");
    require_true(replaced.desired_point_source ==
                     pipeline_contract::DesiredPointSource::VisionDefault,
                 "new I must not inherit user correction state");
}

void test_firing_downward_input_moves_d_without_arming_handover() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 500.0f;
    config.desired_point_traversal_ms = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(12, 12.0);
    const auto initial = coordinator.update(
        selected_frame(110, 12.0, 240.0f, 208.0f),
        ads_intent(),
        12.0);

    auto downward = correcting_intent(0.0f, -0.80f);
    downward.fire = true;
    controller_native::TargetControlFeedback firing;
    firing.firing_recently = true;
    auto held = coordinator.update(
        no_source_tick(), downward, 12.050, firing);
    require_true(held.desired_point_normalized.y >
                     initial.desired_point_normalized.y,
                 "firing pull-down did not move D downward inside R");
    require_true(held.manual_correction_y,
                 "firing pull-down lost its D-correction semantics");

    for (int tick = 1; tick <= 8; ++tick) {
        held = coordinator.update(
            no_source_tick(), downward, 12.050 + tick * 0.050, firing);
    }
    require_true(near(held.desired_point_normalized.y, 1.0f),
                 "firing pull-down did not reach the lower edge of R");
    require_true(!held.manual_exit_requested,
                 "firing pull-down armed a target handover at R's lower edge");
}

}  // namespace

int main() {
    test_no_source_tick_reuses_immutable_source_plan();
    test_fresh_no_selection_drops_generic_authority();
    test_fresh_empty_frame_drops_target();
    test_hard_source_age_gate_releases_without_gradual_decay();
    test_velocity_updates_only_on_fresh_capture();
    test_duplicate_and_stale_captures_cannot_replace_geometry();
    test_same_generation_cue_is_only_continuity_path();
    test_selector_generation_replacement_resets_target_identity();
    test_replacement_rearms_ads_after_previous_target_consumed();
    test_manual_correction_moves_d_only_inside_r();
    test_cue_carries_corrected_d_instead_of_replacing_it();
    test_target_replacement_resets_corrected_d();
    test_firing_downward_input_moves_d_without_arming_handover();
    std::cout << "[TargetCoordinatorTests] PASS\n";
    return 0;
}
