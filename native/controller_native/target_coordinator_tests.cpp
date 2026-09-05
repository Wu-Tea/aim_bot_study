#include "target_coordinator.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
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
    const auto waiting = coordinator.update(
        no_source_tick(), intent, 2.020);
    const auto resumed = coordinator.update(
        selected_frame(12, 2.030, 268.0f), intent, 2.030);

    require_true(observed.aim_authority > 0.0f, "fresh target must own aim authority");
    require_true(lost.target_id == 0 && lost.aim_authority == 0.0f,
                 "fresh selector no-selection must remove target authority");
    require_true(lost.mode == pipeline_contract::ControlMode::Manual,
                 "fresh no-target plan must return to manual output");
    require_true(lost.ads_acquisition_active &&
                     waiting.ads_acquisition_active &&
                     waiting.mode == pipeline_contract::ControlMode::Manual,
                 "brief same-generation miss must pause without consuming ADS");
    require_true(resumed.mode == pipeline_contract::ControlMode::AdsAcquire &&
                     resumed.ads_acquisition_active &&
                     resumed.target_acquisition_id ==
                         observed.target_acquisition_id,
                 "fresh same-generation evidence must resume the same acquisition");
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
    const auto same_identity_after_abort = coordinator.update(
        selected_frame(31, 4.030, 274.0f), intent, 4.030);

    require_true(near(before_expiry.aim_authority, observed.aim_authority),
                 "authority must remain flat before the hard age gate");
    require_true(expired.target_id == 0 && expired.aim_authority == 0.0f,
                 "hard source-age gate must release stale authority");
    require_true(expired.mode == pipeline_contract::ControlMode::Manual &&
                     expired.acquisition_terminal_reason ==
                         pipeline_contract::AdsDecisionReason::TargetLost,
                 "source-age abort must remain Manual and explicit");
    require_true(same_identity_after_abort.mode ==
                     pipeline_contract::ControlMode::BodyLockFollow &&
                     same_identity_after_abort.target_id != 0 &&
                     same_identity_after_abort.aim_authority > 0.0f &&
                     !same_identity_after_abort.ads_acquisition_active &&
                     same_identity_after_abort.target_acquisition_id ==
                         observed.target_acquisition_id,
                 "fresh same-identity recovery must resume BodyLock without minting another snap");
}

void test_deadlines_and_center_cross_handoff_once() {
    const auto intent = ads_intent();

    controller_native::TargetCoordinatorConfig ceiling_config;
    ceiling_config.settle_radius_px = 1.0f;
    ceiling_config.settle_frames = 100;
    ceiling_config.ads_nominal_acquisition_ms = 10.0f;
    ceiling_config.ads_target_wait_ms = 20.0f;
    ceiling_config.ads_extension_budget_ms = 20.0f;
    controller_native::TargetCoordinator ceiling(ceiling_config);
    ceiling.begin_ads_epoch(40, 4.5);
    (void)ceiling.update(
        selected_frame(400, 4.5, 280.0f), intent, 4.5);
    const auto after_ceiling = ceiling.update(
        selected_frame(401, 4.531, 280.0f), intent, 4.531);
    require_true(after_ceiling.mode ==
                     pipeline_contract::ControlMode::AdsAcquire &&
                     after_ceiling.ads_acquisition_active &&
                     after_ceiling.ads_acquisition_state ==
                         pipeline_contract::AdsAcquisitionState::AcquiringManualSafe &&
                     after_ceiling.acquisition_terminal_reason ==
                         pipeline_contract::AdsDecisionReason::None &&
                     after_ceiling.ads_decision_reason ==
                         pipeline_contract::AdsDecisionReason::ExtensionBudgetElapsed,
                 "extension budget must enter non-terminal manual-safe ADS");

    controller_native::TargetCoordinator waiting(ceiling_config);
    waiting.begin_ads_epoch(42, 4.7);
    const auto before_wait_deadline = waiting.update(
        fresh_no_selection(420, 4.719), intent, 4.719);
    const auto after_wait_deadline = waiting.update(
        fresh_no_selection(421, 4.721), intent, 4.721);
    const auto late_target = waiting.update(
        selected_frame(422, 4.730, 280.0f), intent, 4.730);
    require_true(before_wait_deadline.ads_acquisition_state ==
                     pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget &&
                     after_wait_deadline.mode ==
                         pipeline_contract::ControlMode::Manual &&
                     after_wait_deadline.ads_acquisition_state ==
                         pipeline_contract::AdsAcquisitionState::Completed &&
                     after_wait_deadline.acquisition_terminal_reason ==
                         pipeline_contract::AdsDecisionReason::NoTarget &&
                     late_target.mode ==
                         pipeline_contract::ControlMode::BodyLockFollow &&
                     !late_target.ads_plan_admitted &&
                     !late_target.ads_acquisition_active,
                 "expired target wait admitted a late ADS Snap");

    controller_native::TargetCoordinatorConfig cross_config;
    cross_config.settle_radius_px = 8.0f;
    cross_config.settle_frames = 100;
    cross_config.ads_nominal_acquisition_ms = 5.0f;
    cross_config.ads_extension_budget_ms = 100.0f;
    controller_native::TargetCoordinator cross(cross_config);
    cross.begin_ads_epoch(41, 4.6);
    (void)cross.update(
        selected_frame(410, 4.6, 248.0f), intent, 4.6);
    const auto after_cross = cross.update(
        selected_frame(411, 4.610, 234.0f), intent, 4.610);
    require_true(after_cross.mode ==
                     pipeline_contract::ControlMode::BodyLockFollow &&
                     !after_cross.ads_acquisition_active &&
                     after_cross.acquisition_terminal_reason ==
                         pipeline_contract::AdsDecisionReason::CenterCross &&
                     after_cross.ads_decision_reason ==
                         pipeline_contract::AdsDecisionReason::CenterCross,
                 "meaningful radial center cross must hand off to BodyLock");
    const auto held_lt_follow = cross.update(
        selected_frame(412, 4.620, 235.0f), intent, 4.620);
    require_true(held_lt_follow.mode ==
                     pipeline_contract::ControlMode::BodyLockFollow &&
                     !held_lt_follow.ads_acquisition_active &&
                     held_lt_follow.target_acquisition_id ==
                         after_cross.target_acquisition_id &&
                     held_lt_follow.acquisition_terminal_reason ==
                         pipeline_contract::AdsDecisionReason::CenterCross,
                 "held LT must keep the consumed snap in BodyLock without rearming ADS");
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

void test_selector_generation_replacement_preserves_single_snap_token() {
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
                     second.target_acquisition_id == first.target_acquisition_id &&
                     second.mode == pipeline_contract::ControlMode::BodyLockFollow &&
                     !second.ads_acquisition_active &&
                     second.aim_authority > 0.0f,
                 "replacement must consume the existing snap token and continue as BodyLock");
}

void test_replacement_after_consumed_snap_stays_bodylock() {
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
                     second.target_acquisition_id == first.target_acquisition_id,
                 "replacement must not allocate a second acquisition under held LT");
    require_true(second.mode == pipeline_contract::ControlMode::BodyLockFollow &&
                     !second.ads_acquisition_active &&
                     second.aim_authority > 0.0f,
                 "held-LT transfer must retain BodyLock without restarting ADS");
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

void test_size_scaled_pickup_is_revalidated_only_on_new_ads_epoch() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 500.0f;
    config.ads_pickup_base_radius_px = 150.0f;

    // A selector identity retained from the previous physical scope must not
    // inherit ADS admission after it has moved outside the current envelope.
    controller_native::TargetCoordinator cross_epoch(config);
    cross_epoch.begin_ads_epoch(13, 13.0);
    const auto first = cross_epoch.update(
        selected_frame(120, 13.0, 400.0f), ads_intent(), 13.0);
    require_true(first.ads_plan_admitted,
                 "inside first-scope target was not admitted");
    (void)cross_epoch.update(
        selected_frame(121, 13.006, 460.0f), {}, 13.006);
    cross_epoch.begin_ads_epoch(14, 13.010);
    const auto outside = cross_epoch.update(
        selected_frame(122, 13.010, 460.0f), ads_intent(), 13.010);
    require_true(!outside.ads_plan_admitted &&
                     !outside.ads_acquisition_active &&
                     outside.mode == pipeline_contract::ControlMode::Manual &&
                     near(outside.aim_authority, 0.0f),
                 "new ADS epoch inherited far-target authority");
    require_true(outside.source_decision_available &&
                     outside.source_decision_outcome ==
                         pipeline_contract::SourceDecisionOutcome::Rejected &&
                     outside.source_decision_reason ==
                         pipeline_contract::AdsDecisionReason::
                             OutsidePickupEnvelope,
                 "far current-epoch candidate lost its pickup rejection reason");

    // Rejection keeps the same epoch armed; a later fresh frame may enter the
    // envelope and earn admission instead of forcing an early release.
    const auto entered = cross_epoch.update(
        selected_frame(123, 13.016, 410.0f), ads_intent(), 13.016);
    require_true(entered.ads_plan_admitted && entered.ads_acquisition_active,
                 "armed ADS epoch did not admit a later eligible frame");

    // Once an epoch has validly admitted its target, distance is not allowed
    // to become a second gain control for that same ADS positioning job.
    controller_native::TargetCoordinator admitted_continuation(config);
    admitted_continuation.begin_ads_epoch(15, 15.0);
    const auto admitted = admitted_continuation.update(
        selected_frame(130, 15.0, 320.0f), ads_intent(), 15.0);
    const auto continued = admitted_continuation.update(
        selected_frame(131, 15.006, 460.0f), ads_intent(), 15.006);
    require_true(admitted.ads_plan_admitted &&
                     continued.ads_acquisition_active &&
                     continued.mode ==
                         pipeline_contract::ControlMode::AdsAcquire &&
                     continued.aim_authority > 0.0f,
                 "same-epoch continuation was incorrectly weakened by distance");

    // A close target may legitimately be farther from the reticle because its
    // observed body height expands the same shared pickup envelope.
    controller_native::TargetCoordinator close_large(config);
    close_large.begin_ads_epoch(16, 16.0);
    auto large_frame = selected_frame(140, 16.0, 570.0f, 256.0f);
    large_frame.frame_width_px = 640.0f;
    large_frame.frame_height_px = 512.0f;
    large_frame.candidates[0].aim_region_px =
        {460.0f, 64.0f, 220.0f, 480.0f};
    large_frame.candidates[0].body_box_px =
        large_frame.candidates[0].aim_region_px;
    large_frame.candidates[0].box_size_px = {220.0f, 480.0f};
    large_frame.candidates[0].normalized_size = 480.0f / 512.0f;
    const auto large = close_large.update(large_frame, ads_intent(), 16.0);
    require_true(large.ads_plan_admitted && large.ads_acquisition_active,
                 "close large target lost its expanded pickup envelope");
}

void test_unconfirmed_bodylock_keeps_conservative_safety_budget() {
    controller_native::TargetCoordinatorConfig config;
    config.settle_frames = 1;
    config.visual_authority_enabled = true;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(17, 17.0);

    auto direct = selected_frame(150, 17.0, 240.0f);
    direct.selector_enemy_cue_current = false;
    direct.selector_enemy_identity_confirmed = false;
    direct.selector_enemy_cue_checked = true;
    direct.candidates[0].reliability = 0.95f;
    const auto observed = coordinator.update(direct, ads_intent(), 17.0);

    require_true(observed.mode ==
                     pipeline_contract::ControlMode::BodyLockFollow &&
                     observed.lifecycle ==
                         pipeline_contract::TargetLifecycle::Observed,
                 "fixture must complete ADS into directly observed BodyLock");
    require_true(near(observed.visual_authority, 0.076f, 0.001f),
                 "visual enemy evidence must remain explicitly conservative");
    require_true(near(observed.aim_authority, observed.visual_authority),
                 "unconfirmed BodyLock must retain its visual safety ceiling");

    auto cue = cue_frame(151, 17.006, 240.0f);
    cue.selector_enemy_cue_current = false;
    cue.selector_enemy_identity_confirmed = false;
    cue.selector_enemy_cue_checked = true;
    const auto continued = coordinator.update(cue, ads_intent(), 17.006);
    require_true(continued.lifecycle ==
                     pipeline_contract::TargetLifecycle::CueContinuation,
                 "counterfactual must enter cue-only continuation");
    require_true(continued.aim_authority <= observed.aim_authority,
                 "cue-only continuation must not exceed direct-observation authority");
}

}  // namespace

namespace {

void test_desired_point_uses_wall_time_at_all_supported_cadences() {
    float reference = 0.0f;
    for (const int hz : {500, 1000, 2000}) {
        controller_native::TargetCoordinator coordinator;
        const auto intent = correcting_intent(0.0f, -0.8f);
        auto batch = selected_frame(1, 1.0, 240.0f);
        batch.candidates[0].box_size_px.y = 80.0f;
        coordinator.begin_ads_epoch(1, 1.0);
        const auto initial = coordinator.update(batch, intent, 1.0);
        auto final = initial;
        for (int tick = 1; tick <= hz * 30 / 1000; ++tick) {
            final = coordinator.update(no_source_tick(), intent, 1.0 + double(tick) / hz);
        }
        require_true(final.target_id == initial.target_id && initial.target_id != 0,
                     "cadence fixture lost the same target");
        const float distance = final.aim_px.y - initial.aim_px.y;
        if (hz == 500) reference = distance;
        require_true(reference > 10.0f && near(distance, reference, 0.002f),
                     "desired-point integration changes with controller cadence");
    }
}

void test_boundary_exit_uses_wall_time_at_all_supported_cadences() {
    double reference = 0.0;
    for (const int hz : {500, 1000, 2000}) {
        controller_native::TargetCoordinator coordinator;
        const auto intent = correcting_intent(0.0f, -0.8f);
        coordinator.begin_ads_epoch(1, 1.0);
        coordinator.update(selected_frame(1, 1.0, 240.0f), intent, 1.0);
        double exit_ms = 0.0;
        for (int tick = 1; tick <= hz; ++tick) {
            const double now = 1.0 + double(tick) / hz;
            const auto batch = (tick % (hz / 100)) == 0
                ? selected_frame(1 + tick / (hz / 100), now, 240.0f)
                : no_source_tick();
            const auto plan = coordinator.update(batch, intent, now);
            if (plan.manual_exit_requested) {
                exit_ms = (now - 1.0) * 1000.0;
                break;
            }
        }
        if (hz == 500) reference = exit_ms;
        require_true(exit_ms > 100.0 && exit_ms < 250.0 &&
                         std::fabs(exit_ms - reference) <= 2.001,
                     "manual boundary hold duration changes with controller cadence");
    }
}

}  // namespace

void register_target_coordinator_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "desired_point_wall_time_across_cadences", test_desired_point_uses_wall_time_at_all_supported_cadences);
    registry.add_case("BaseBodyLock", "boundary_exit_wall_time_across_cadences", test_boundary_exit_uses_wall_time_at_all_supported_cadences);
    registry.add_case("BaseBodyLock", "no_source_tick_reuses_immutable_plan", test_no_source_tick_reuses_immutable_source_plan);
    registry.add_case("BaseBodyLock", "fresh_no_selection_drops_authority", test_fresh_no_selection_drops_generic_authority);
    registry.add_case("BaseBodyLock", "fresh_empty_frame_drops_target", test_fresh_empty_frame_drops_target);
    registry.add_case("BaseRuntimeFreshness", "hard_source_age_releases_without_decay", test_hard_source_age_gate_releases_without_gradual_decay);
    registry.add_case("BaseAds", "deadlines_and_center_cross_handoff_once", test_deadlines_and_center_cross_handoff_once);
    registry.add_case("BaseBodyLock", "velocity_updates_only_on_fresh_capture", test_velocity_updates_only_on_fresh_capture);
    registry.add_case("BaseRuntimeFreshness", "duplicate_and_stale_captures_cannot_replace_geometry", test_duplicate_and_stale_captures_cannot_replace_geometry);
    registry.add_case("BaseBodyLock", "same_generation_cue_is_only_continuity_path", test_same_generation_cue_is_only_continuity_path);
    registry.add_case("BaseAds", "selector_replacement_preserves_single_snap", test_selector_generation_replacement_preserves_single_snap_token);
    registry.add_case("BaseAds", "replacement_after_consumed_snap_stays_bodylock", test_replacement_after_consumed_snap_stays_bodylock);
    registry.add_case("BaseBodyLock", "manual_correction_moves_d_only_inside_region", test_manual_correction_moves_d_only_inside_r);
    registry.add_case("BaseBodyLock", "cue_carries_corrected_desired_point", test_cue_carries_corrected_d_instead_of_replacing_it);
    registry.add_case("BaseBodyLock", "target_replacement_resets_corrected_point", test_target_replacement_resets_corrected_d);
    registry.add_case("BaseBodyLock", "firing_downward_does_not_arm_handover", test_firing_downward_input_moves_d_without_arming_handover);
    registry.add_case("BaseAds", "size_scaled_pickup_revalidated_per_epoch", test_size_scaled_pickup_is_revalidated_only_on_new_ads_epoch);
    registry.add_case("BaseBodyLock", "unconfirmed_budget_keeps_safety_ceiling", test_unconfirmed_bodylock_keeps_conservative_safety_budget);
}
