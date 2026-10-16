#include "target_coordinator.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <random>

namespace {

void require_true(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 0.001f) {
    return std::fabs(left - right) <= tolerance;
}

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent;
    intent.activation = pipeline_contract::AssistActivation::Engaged;
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

void test_pre_aim_selection_admitted_without_new_source() {
    controller_native::TargetCoordinator coordinator;
    const auto idle = coordinator.update(selected_frame(1, 1.0, 280.f), {}, 1.0);
    require_true(idle.target_id != 0 && idle.aim_authority == 0.f,
        "pre-aim observation must retain selection without moving aim");
    coordinator.begin_ads_epoch(1, 1.003);
    const auto acquired = coordinator.update(no_source_tick(), ads_intent(), 1.003);
    require_true(acquired.ads_plan_admitted && acquired.ads_acquisition_active,
        "opening ADS must admit the still-current pre-aim selection without waiting for a source frame");
    require_true(acquired.target_id == idle.target_id && acquired.aim_authority > 0.f,
        "warm admission must preserve canonical identity and enable aim");
    require_true(!acquired.source_decision_available && !acquired.fire_authority &&
        !acquired.fire_requested && near(acquired.source_capture_age_ms, 3.f),
        "warm admission must not fabricate a fresh capture, source decision, or fire authority");
}

void test_pre_aim_controller_output() {
    using namespace controller_native;
    double now = 1.0;
    auto config = incident_fixture::base_config(40.f, 135.f);
    NativeGamepadController controller(config, &now);
    incident_fixture::TargetSpec spec;
    spec.observation_id = 41; spec.selector_generation = 7;
    auto snapshot = incident_fixture::observed_snapshot(spec, 1, now, 70.f, 0.f);
    snapshot.state.aim_authority = false; // VisionService idle policy
    controller.submit_vision_snapshot(snapshot);
    auto physical = incident_fixture::ads_input(); physical.left_trigger = 0.f;
    auto output = controller.build_output(physical);
    require_true(output.right_x == 0.f && controller.last_target_plan().target_id != 0,
        "idle must prepare target without moving the controller");
    now += .003; physical.left_trigger = 1.f;
    output = controller.build_output(physical);
    require_true(controller.last_target_plan().ads_plan_admitted && output.right_x > 0.f,
        "rapid full trigger must produce actual ADS output without another vision publication");
    require_true(!controller.last_target_plan().direct_person_observation && output.right_trigger == 0.f,
        "warm controller tick must not invent source evidence or fire");
    now += .001; physical.left_trigger = 0.f; physical.right_x = -.21f; physical.right_y = .13f;
    output = controller.build_output(physical);
    require_true(output.right_x == physical.right_x && output.right_y == physical.right_y,
        "release after warm admission must preserve exact manual passthrough");
}

void test_pre_aim_randomized(unsigned seed) {
    std::mt19937 rng(seed);
    for (unsigned scenario = 0; scenario < 160; ++scenario) {
        controller_native::TargetCoordinatorConfig config;
        config.max_observation_age_ms = 40.f;
        controller_native::TargetCoordinator coordinator(config);
        double now = 1.; unsigned frame = 1;
        const unsigned frames = (scenario / 8) % 2 ? 2000 : 5;
        std::uint64_t generation = 7;
        pipeline_contract::TargetPlan idle;
        for (unsigned step = 0; step < frames; ++step) {
            now += .005;
            if (rng()%37 == 0) ++generation;
            idle = coordinator.update(selected_frame(frame++, now,
                270.f + float(rng()%30), 208.f, 41, generation), {}, now);
            require_true(idle.aim_authority == 0.f, "idle history generated aim authority");
        }
        const unsigned variant = scenario % 8;
        if (variant == 1) {
            now += .005;
            coordinator.update(fresh_no_selection(frame++, now, generation), {}, now);
        } else if (variant == 2) {
            now += .005;
            coordinator.update(selected_frame(frame++, now, 470.f, 208.f, 41, generation), {}, now);
        } else if (variant == 3) {
            now += .045; // original source expires, regardless of long history
        }
        now += (.001 + double(rng()%20)*.001);
        coordinator.begin_ads_epoch(1, now);
        auto input = no_source_tick();
        auto intent = ads_intent();
        if (variant == 4) input = fresh_no_selection(frame++, now, generation);
        if (variant == 5) input = selected_frame(frame++, now, 290.f, 208.f, 42, generation+1);
        if (variant == 6) intent.right_purpose = pipeline_contract::UserAimIntentPurpose::HandoverTarget;
        if (variant == 7) { input = selected_frame(frame-1, now-.02, 280.f); }
        const auto plan = coordinator.update(input, intent, now);
        const bool expected = variant == 0 || variant == 5;
        require_true(plan.ads_plan_admitted == expected && plan.ads_acquisition_active == expected,
            "pre-aim admission ignored freshness, newer rejection, manual handover, or pickup envelope");
        if (variant == 0) {
            require_true(plan.target_id == idle.target_id && plan.selector_target_generation == generation &&
                !plan.direct_person_observation && !plan.source_decision_available &&
                !plan.fire_authority && plan.source_capture_age_ms > 0.f,
                "warm admission changed identity, source age, or source/fire evidence");
        }
        if (variant == 5) require_true(plan.selector_target_generation == generation+1 &&
            plan.direct_person_observation, "fresh replacement must take precedence over pre-aim memory");
        now += .06;
        const auto expired = coordinator.update(no_source_tick(), ads_intent(), now);
        require_true(expired.aim_authority == 0.f, "warm target extended its original source lifetime");
    }
}

void test_pre_aim_new_epoch_cannot_replay_fire() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 1.);
    auto firing = selected_frame(1, 1., 280.f);
    firing.fire_requested = firing.observed_fire_eligible = true;
    const auto previous = coordinator.update(firing, ads_intent(), 1.);
    require_true(previous.fire_requested && previous.fire_authority,
        "fixture must carry previous-epoch fire evidence");
    coordinator.update(no_source_tick(), {}, 1.001);
    coordinator.begin_ads_epoch(2, 1.002);
    const auto warm = coordinator.update(no_source_tick(), ads_intent(), 1.002);
    require_true(warm.ads_plan_admitted && !warm.fire_requested && !warm.fire_authority,
        "new ADS epoch may reuse valid geometry but never old fire evidence");
    const auto held = coordinator.update(no_source_tick(), ads_intent(), 1.003);
    require_true(held.ads_acquisition_active && !held.ads_plan_admitted && !held.fire_requested &&
        !held.fire_authority && !held.direct_person_observation,
        "warm admission must remain one-shot until fresh source evidence arrives");
    firing = selected_frame(2, 1.004, 279.f);
    firing.fire_requested = firing.observed_fire_eligible = true;
    const auto current = coordinator.update(firing, ads_intent(), 1.004);
    require_true(current.fire_requested && current.fire_authority && current.direct_person_observation,
        "fresh current-epoch observation must restore ordinary fire eligibility");
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
    require_true(cue.current_cue_observation && !cue.direct_person_observation,
                 "fresh marker pixels must have their own measurement provenance");
    const auto duplicate = coordinator.update(
        cue_frame(61, 7.006, 300.0f), intent, 7.007);
    require_true(!duplicate.current_cue_observation &&
                     near(duplicate.aim_px.x, cue.aim_px.x),
                 "a repeated marker frame must not produce a new motion sample");
    const auto gap = coordinator.update(no_source_tick(), intent, 7.008);
    require_true(!gap.current_cue_observation,
                 "retained marker geometry must not be labeled current evidence");

    const auto wrong_generation = coordinator.update(
        cue_frame(62, 7.012, 276.0f, 8), intent, 7.012);
    require_true(wrong_generation.target_id == 0 &&
                     wrong_generation.aim_authority == 0.0f &&
                     !wrong_generation.current_cue_observation,
                 "wrong-generation cue must not become generic hold authority");
}

// Freeze this temporal trace before changing production state ownership.
// Cue is new visual evidence; empty/repeated/stale deliveries are not.
void test_cue_gap_expiry_reacquisition(const native_test::TestContext& context) {
    const auto directory = context.artifact_directory / context.case_name;
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "traces.csv");
    require_true(bool(csv), "cue trace open failed");
    csv << std::setprecision(17)
        << "grace,event,reacquire_ms,tick,id,frame,observation,lifecycle,mode,cue,current_cue,direct,gap,prediction,age,capture_age,remaining,authority,x,y,vx,vy,acquisition,active,terminal,diagnostic_age,gap_reason\n";
    for (float grace : {0.f, 32.f}) for (int event = 0; event < 4; ++event)
    for (int reacquire_ms : {30, 70}) {
        controller_native::TargetCoordinatorConfig config;
        config.missing_observation_grace_ms = grace;
        controller_native::TargetCoordinator coordinator(config);
        const auto intent = ads_intent();
        coordinator.begin_ads_epoch(1, 2.0);
        controller_native::TargetControlFeedback feedback;
        feedback.continuation_valid = true;
        feedback.continuation_offset_px = {-2, 1};
        std::uint64_t original_id = 0, acquisition_id = 0;
        for (int tick = 0; tick <= 80; ++tick) {
            const double now = 2.002 + tick / 1000.0;
            auto batch = no_source_tick();
            if (tick == 0) batch = selected_frame(1, 2.0, 270);
            if (tick == 10) batch = cue_frame(2, 2.010, 274);
            if (tick == 12) {
                if (event == 1) batch = fresh_no_selection(3, now);
                if (event == 2) batch = cue_frame(2, 2.010, 310);
                if (event == 3) batch = cue_frame(3, 1.9, 310);
            }
            if (tick == reacquire_ms) batch = selected_frame(4, now - .002, 276);
            const auto p = coordinator.update(batch, intent, now, feedback);
            if (tick == 0) {
                original_id = p.target_id; acquisition_id = p.target_acquisition_id;
                require_true(original_id != 0 && p.ads_acquisition_active, "fixture must start acquiring");
            }
            if (tick == 10) require_true(p.target_id == original_id && p.current_cue_observation &&
                near(p.source_capture_age_ms, 2), "current cue owns its capture time and existing identity");
            if (tick >= 10 && tick < reacquire_ms) {
                require_true(near(p.identity_diagnostics.source_age_ms, float((now - 2.010) * 1000)),
                    "missing, duplicate and stale frames cannot renew cue evidence age");
                require_true(!p.direct_person_observation && !p.fire_authority && !p.fire_requested,
                    "cue and missing evidence cannot acquire direct-person/fire authority");
                if (tick != 10) require_true(!p.current_cue_observation, "retained cue is not current evidence");
            }
            if (tick == 59 && reacquire_ms == 70) require_true(p.target_id == 0 && p.aim_authority == 0 &&
                p.acquisition_terminal_reason == pipeline_contract::AdsDecisionReason::TargetLost,
                "expired cue must release identity and consume unfinished ADS");
            if (tick == reacquire_ms) {
                require_true(p.direct_person_observation && !p.cue_continuation && !p.observation_gap &&
                    near(p.source_capture_age_ms, 2), "fresh direct evidence restores its own provenance");
                require_true((p.target_id == original_id) == (reacquire_ms == 30),
                    "only reacquisition before loss may retain the local identity");
                require_true(p.target_acquisition_id == acquisition_id &&
                    p.ads_acquisition_active == (reacquire_ms == 30), "reacquisition must not mint another ADS chance");
            }
            csv << grace << ',' << event << ',' << reacquire_ms << ',' << tick << ',' << p.target_id
                << ',' << p.source_frame_id << ',' << p.source_observation_id << ',' << int(p.lifecycle)
                << ',' << int(p.mode) << ',' << p.cue_continuation << ',' << p.current_cue_observation
                << ',' << p.direct_person_observation << ',' << p.observation_gap << ',' << p.gap_prediction_valid
                << ',' << p.observation_age_ms << ',' << p.source_capture_age_ms << ',' << p.observation_gap_remaining_ms
                << ',' << p.aim_authority << ',' << p.aim_px.x << ',' << p.aim_px.y
                << ',' << p.velocity_px_per_sec.x << ',' << p.velocity_px_per_sec.y
                << ',' << p.target_acquisition_id << ',' << p.ads_acquisition_active
                << ',' << int(p.acquisition_terminal_reason) << ',' << p.identity_diagnostics.source_age_ms
                << ',' << p.identity_diagnostics.gap_reason << '\n';
        }
    }
    require_true(bool(csv), "cue trace write failed");
}

void test_identity_wait_loss_and_replacement_boundaries() {
    for (int gap : {0, 1, 2}) for (bool changed : {false, true}) for (bool pulse : {false, true}) {
        controller_native::TargetCoordinator coordinator;
        const auto intent = ads_intent();
        coordinator.begin_ads_epoch(8, 8.0);
        const auto first = coordinator.update(selected_frame(70, 8.0, 268), intent, 8.0);
        require_true(first.target_id != 0 && first.ads_acquisition_active, "fixture must acquire a target");
        auto previous = first;
        if (gap != 0) {
            const double missing_at = gap == 1 ? 8.006 : 8.060;
            previous = coordinator.update(fresh_no_selection(71, missing_at), intent, missing_at);
            require_true(previous.target_id == 0 && previous.ads_acquisition_active == (gap == 1),
                "temporary evidence wait must differ from expired identity release");
        }
        const double next_at = gap == 2 ? 8.070 : 8.012;
        auto next = selected_frame(72, next_at, 270, 208, 52, changed ? 8 : 7);
        next.selector_target_changed = pulse;
        const auto result = coordinator.update(next, intent, next_at);
        require_true(result.target_id != 0 && (result.target_id != first.target_id) == (changed || gap == 2),
            "frame-local source ids and raw change pulses must not replace a retained generation");
        require_true(result.selector_target_changed == changed,
            "durable generation, including replacement after loss, must own the effective switch");
        require_true(result.target_acquisition_id == first.target_acquisition_id &&
            result.physical_ads_epoch == first.physical_ads_epoch &&
            result.ads_acquisition_active == (!changed && gap != 2),
            "replacement or loss must not mint another ADS acquisition during held input");
        require_true(result.identity_diagnostics.previous_id == previous.target_id &&
            result.identity_diagnostics.resulting_id == result.target_id,
            "identity diagnostics must compare complete consecutive output plans");
    }
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


void test_intermittent_selection_diagnostics(const native_test::TestContext& context) {
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("intermittent_selection.csv"));
    report << "seed,duration_ms,case,misses,bridges,releases,rebuilds\n";
    for (unsigned seed : {174805u, 917431u}) for (int duration : {800,8000})
    for (int scenario=0; scenario<16; ++scenario) for (int mode=0; mode<4; ++mode) {
        std::mt19937 random(seed+scenario);
        controller_native::TargetCoordinatorConfig config;
        config.settle_frames=1;
        config.missing_observation_grace_ms=32;
        config.max_observation_age_ms=50;
        controller_native::TargetCoordinator coordinator(config);
        auto intent=ads_intent();
        double now=10;
        coordinator.begin_ads_epoch(1,now);
        auto plan=coordinator.update(selected_frame(1,now,240),intent,now);
        require_true(plan.mode==pipeline_contract::ControlMode::BodyLockFollow,"fixture must consume Snap first");
        std::uint64_t frame=1;
        int misses=0,bridges=0,releases=0,rebuilds=0;
        for (int elapsed=7;elapsed<duration;) {
            const int dt=5+random()%4;
            now=10.+elapsed*.001;
            const auto previous=plan.target_id;
            const bool miss=random()%7==0;
            controller_native::TargetControlFeedback feedback;
            feedback.continuation_valid=mode!=1;
            if (miss) {
                auto batch=fresh_no_selection(++frame,now);
                if (mode==2) batch.rejected_friendly_count=1;
                if (mode==3) now+=.060;
                plan=coordinator.update(batch,intent,now,feedback);
                ++misses;
                bridges+=plan.observation_gap && plan.gap_prediction_valid;
                releases+=previous!=0 && plan.target_id==0;
                require_true(plan.source_decision_reason==(mode==3 ? pipeline_contract::AdsDecisionReason::StaleCapture : pipeline_contract::AdsDecisionReason::SelectorNoSelection),
                    "consumed Snap must not hide the actual no-selection reason");
                if (previous!=0) {
                    const auto* reason=plan.identity_diagnostics.gap_reason;
                    if(mode==0) require_true(plan.target_id==previous && plan.observation_gap && std::string(reason)=="bridged",
                        "fresh same-generation short miss with camera history must preserve identity");
                    else if (mode==1) require_true(plan.target_id==previous && plan.observation_gap &&
                        !plan.gap_prediction_valid && plan.aim_authority==0 && std::string(reason)=="camera_history_unavailable",
                        "missing camera history must suspend prediction without destroying same-target identity");
                    else require_true(plan.target_id==0 &&
                        std::string(reason)==(mode==2?"friendly_rejection":"source_expired"),
                        "release must name the actual failed continuation boundary");
                }
                // Restore fresh current measurements, never replay old geometry.
                elapsed+=mode==3?60:0;
                now+=dt*.001;
                plan=coordinator.update(selected_frame(++frame,now,240.f+float(random()%21)-10.f),intent,now,feedback);
                rebuilds+=previous!=0 && plan.target_id!=previous;
                require_true(plan.target_id!=0 && plan.mode==pipeline_contract::ControlMode::BodyLockFollow,
                    "same-generation recovery must not mint a second Snap");
            } else {
                plan=coordinator.update(selected_frame(++frame,now,240.f+float(random()%21)-10.f),intent,now,feedback);
            }
            elapsed+=dt+(miss?dt:0);
        }
        require_true(misses>0,"fixture must exercise a dropout");
        require_true(mode<2 ? rebuilds==0 : rebuilds==misses,"identity rebuilding must follow the diagnosed release");
        report<<seed<<','<<duration<<','<<mode<<','<<misses<<','<<bridges<<','<<releases<<','<<rebuilds<<'\n';
    }
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

// Geometry-policy fixture, not an anatomical or game-plant replay. The box
// matches the selector's 40% point and 22%-58% vertical aim region.
pipeline_contract::VisionObservationBatch approach_frame(
    std::uint64_t frame, double now, float x, float y,
    std::uint64_t generation = 7) {
    auto batch = selected_frame(frame, now, x, y, 41, generation);
    auto& c = batch.candidates[0];
    c.body_box_px = {x - 30.0f, y - 80.0f, 60.0f, 200.0f};
    c.box_size_px = {60.0f, 200.0f};
    c.aim_region_px = {x - 16.8f, y - 36.0f, 33.6f, 72.0f};
    return batch;
}

void test_bounded_acquisition_point_geometry(const native_test::TestContext& context) {
    const auto intent = ads_intent();
    controller_native::TargetCoordinator above;
    above.begin_ads_epoch(1, 1.0);
    const auto first = above.update(approach_frame(1, 1.0, 240, 260), intent, 1.0);
    if (first.target_id == 0 || first.mode != pipeline_contract::ControlMode::AdsAcquire ||
        !first.direct_person_observation || !first.has_aim_region) {
        native_test::invalid_fixture("above-entry target was not admitted with fresh geometry");
    }
    const float saved = first.source_aim_px.y - first.aim_px.y;
    const auto idle = above.update(no_source_tick(), intent, 1.001);
    // Camera movement places the chosen D on the crosshair. It must not cause
    // a second downward leg back to the source-default chest/abdomen point.
    // Freeze the camera trajectory before candidate output: the selector's
    // 72px region permits a 7.2px conservative offset in this fixture.
    const float centered_source_y = 215.2f;
    auto landed = above.update(approach_frame(2, 1.01, 240, centered_source_y), intent, 1.01);
    for (int i = 3; i <= 9; ++i) {
        const double now = 1.0 + (i - 1) * .01;
        landed = above.update(approach_frame(i, now, 240, centered_source_y), intent, now);
    }
    auto cue = approach_frame(10, 1.09, 240, centered_source_y);
    cue.selector_cue_continuation = true;
    cue.preferred_source_id = 0;
    cue.candidates[0].source_id = 0;
    cue.candidates[0].cue_confidence = .9f;
    cue.candidates[0].aim_region_source = pipeline_contract::AimRegionSource::CueTranslated;
    const auto carried = above.update(cue, intent, 1.09);
    const auto restored = above.update(approach_frame(11, 1.10, 240, centered_source_y), intent, 1.10);
    const auto corrected = above.update(approach_frame(12, 1.11, 240, centered_source_y),
        correcting_intent(0, -.2f), 1.11);
    const auto replaced = above.update(approach_frame(13, 1.12, 240, 180, 8), intent, 1.12);

    const auto control = [&](float x, float y, bool fallback) {
        controller_native::TargetCoordinator coordinator;
        coordinator.begin_ads_epoch(1, 2.0);
        auto b = approach_frame(1, 2.0, x, y);
        if (fallback) b.candidates[0].aim_region_source =
            pipeline_contract::AimRegionSource::BodyBoxFallback;
        const auto p = coordinator.update(b, intent, 2.0);
        if (p.target_id == 0 || !p.direct_person_observation)
            native_test::invalid_fixture("counterfactual target was not admitted");
        return p;
    };
    const auto below = control(240, 156, false);
    const auto left = control(280, 208, false);
    const auto right = control(200, 208, false);
    const auto diagonal = control(280, 260, false);
    const auto fallback = control(240, 260, true);
    if (carried.lifecycle != pipeline_contract::TargetLifecycle::CueContinuation ||
        restored.target_id != first.target_id || replaced.target_id == first.target_id) {
        native_test::invalid_fixture("cue/identity controls did not execute their intended branches");
    }
    std::ofstream report(context.artifact_path("bounded-acquisition-point.json"));
    report << "{\"trigger_admitted\":true,\"source_y\":" << first.source_aim_px.y
        << ",\"selected_y\":" << first.aim_px.y << ",\"saved_vertical_px\":" << saved
        << ",\"landed_error_y\":" << landed.error_px.y
        << ",\"landed_bodylock\":" << (landed.mode == pipeline_contract::ControlMode::BodyLockFollow)
        << ",\"cue_error_y\":" << carried.error_px.y
        << ",\"restored_error_y\":" << restored.error_px.y
        << ",\"manual_y_delta\":" << corrected.aim_px.y - restored.aim_px.y
        << ",\"replacement_y\":" << replaced.aim_px.y
        << ",\"below_y\":" << below.aim_px.y << ",\"left_y\":" << left.aim_px.y
        << ",\"right_y\":" << right.aim_px.y << ",\"diagonal_y\":" << diagonal.aim_px.y
        << ",\"fallback_y\":" << fallback.aim_px.y << "}\n";
    report.close();
    require_true(saved >= 1.0f && saved <= 8.0f,
        "above-entry path must shorten within the conservative 4%-of-body budget");
    require_true(near(first.aim_px.x, 240) && near(first.source_aim_px.y, 260) &&
        near(first.velocity_px_per_sec.y, 0), "automatic D selection must not rewrite source geometry/motion");
    require_true(near(idle.aim_px.y, first.aim_px.y), "no-source tick changed chosen point");
    require_true(near(landed.error_px.y, 0) && landed.target_id == first.target_id &&
        landed.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "landing must hand off without a second downward pull");
    require_true(carried.lifecycle == pipeline_contract::TargetLifecycle::CueContinuation &&
        near(carried.error_px.y, 0) && near(restored.error_px.y, 0),
        "cue/fresh transitions recentered automatic D");
    require_true(corrected.aim_px.y > restored.aim_px.y &&
        corrected.desired_point_source == pipeline_contract::DesiredPointSource::UserCorrected,
        "automatic D blocked deliberate manual correction");
    require_true(replaced.target_id != first.target_id && near(replaced.aim_px.y, 180),
        "replacement inherited old automatic/manual D");
    require_true(near(below.aim_px.y, 156) && near(left.aim_px.y, 208) &&
        near(right.aim_px.y, 208) && near(diagonal.aim_px.y, 260) &&
        near(fallback.aim_px.y, 260), "counterfactual changed without an upper central approach");
}

}  // namespace

void register_target_coordinator_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseEndToEnd", "cue_gap_expiry_reacquisition", test_cue_gap_expiry_reacquisition);
    registry.add_context_case("BaseBodyLock", "intermittent_selection_diagnostics", test_intermittent_selection_diagnostics);
    registry.add_context_case("BaseAds", "bounded_acquisition_point_geometry", test_bounded_acquisition_point_geometry);
    registry.add_case("BaseBodyLock", "randomized_gap_deadlines_and_invalidations", [] {
        for(unsigned seed : {10632u,938117u}) {
            std::mt19937 random(seed);
            for(int scenario=0;scenario<80;++scenario) {
                controller_native::TargetCoordinatorConfig config;config.missing_observation_grace_ms=32;
                config.max_observation_age_ms=scenario%3 ? 50.f : 20.f;
                controller_native::TargetCoordinator coordinator(config);
                auto intent=ads_intent();coordinator.begin_ads_epoch(1,2.0);
                double now=2.,capture=2.;unsigned frame=1;
                auto first=coordinator.update(selected_frame(frame++,now,290),intent,now);
                controller_native::TargetControlFeedback feedback;feedback.continuation_valid=true;
                for(int tick=0;tick<(scenario%2 ? 2000 : 128);++tick) {
                    now+=.001;
                    const bool observed=random()%41==0;
                    auto batch=observed ? selected_frame(frame++,now,290) : fresh_no_selection(frame++,now);
                    if(observed) capture=now;
                    feedback.continuation_offset_px={-static_cast<float>((now-capture)*50),0};
                    const auto plan=coordinator.update(batch,intent,now,feedback);
                    if(plan.observation_gap) {
                        require_true((now-capture)*1000<=std::min(32.f,config.max_observation_age_ms)+1e-5,
                            "prediction cannot exceed original source-age budget");
                        require_true(!plan.direct_person_observation && !plan.fire_authority,
                            "random misses never become observation/fire evidence");
                    }
                }
                for(int invalid=0;invalid<5;++invalid) {
                    coordinator.reset();coordinator.begin_ads_epoch(1,4.);
                    coordinator.update(selected_frame(1,4.,290),intent,4.);
                    auto miss=fresh_no_selection(2,4.006);auto changed=intent;
                    feedback.continuation_valid=true;
                    if(invalid==0)miss.rejected_friendly_count=1;
                    if(invalid==1)miss.selector_target_generation=8;
                    if(invalid==2)miss.selector_identity_protocol=false;
                    if(invalid==3)feedback.continuation_valid=false;
                    if(invalid==4)changed.right_purpose=pipeline_contract::UserAimIntentPurpose::HandoverTarget;
                    require_true(coordinator.update(miss,changed,4.006,feedback).aim_authority==0,
                        "explicit invalidation, missing delivery evidence and handover must not bridge");
                }
            }
        }
    });
    registry.add_case("BaseBodyLock", "bounded_32ms_miss_continuation", [] {
        controller_native::TargetCoordinatorConfig config;
        config.missing_observation_grace_ms=32;
        controller_native::TargetCoordinator coordinator(config);
        auto intent=ads_intent();coordinator.begin_ads_epoch(2,2.0);
        const auto observed=coordinator.update(selected_frame(10,2.0,270),intent,2.0);
        controller_native::TargetControlFeedback feedback;
        feedback.continuation_valid=true;feedback.continuation_offset_px={-3,1};
        const auto gap=coordinator.update(fresh_no_selection(11,2.006),intent,2.006,feedback);
        require_true(gap.target_id==observed.target_id && gap.aim_authority>0,
            "a same-identity 6ms miss must retain continuous aim");
        require_true(near(gap.error_px.x,observed.error_px.x-3) &&
            near(gap.error_px.y,observed.error_px.y+1),"gap uses delivered camera displacement");
        require_true(!gap.direct_person_observation && !gap.fire_authority,
            "prediction is not a fresh person or automatic-fire authority");
        const auto retained=coordinator.update(fresh_no_selection(12,2.031),intent,2.031,feedback);
        require_true(retained.target_id==observed.target_id && retained.aim_authority>0,
            "repeated misses remain within original capture deadline");
        const auto expired=coordinator.update(no_source_tick(),intent,2.033,feedback);
        require_true(expired.aim_authority==0,"32ms is a hard capture-age deadline, not renewable per miss");
    });
    registry.add_case("BaseBodyLock", "gap_identity_prediction_and_expiry_are_separate", [] {
        controller_native::TargetCoordinatorConfig config;
        config.missing_observation_grace_ms=32;config.settle_frames=1;
        controller_native::TargetCoordinator coordinator(config);
        auto intent=ads_intent();coordinator.begin_ads_epoch(2,2.0);
        const auto first=coordinator.update(selected_frame(1,2.,240),intent,2.);
        const auto held=coordinator.update(fresh_no_selection(2,2.006),intent,2.006);
        require_true(held.target_id==first.target_id && held.observation_gap && !held.gap_prediction_valid &&
            held.aim_authority==0 && !held.fire_authority,"identity must survive independently of prediction authority");
        controller_native::TargetControlFeedback feedback;feedback.continuation_valid=true;
        feedback.continuation_offset_px={10000,0};
        const auto unsupported=coordinator.update(fresh_no_selection(3,2.012),intent,2.012,feedback);
        require_true(unsupported.target_id==first.target_id && unsupported.aim_authority==0 &&
            near(unsupported.aim_px.x,first.aim_px.x),"unbounded extrapolation cannot move held identity geometry");
        const auto recovered=coordinator.update(selected_frame(4,2.018,244),intent,2.018);
        require_true(recovered.target_id==first.target_id && recovered.direct_person_observation &&
            !recovered.observation_gap && recovered.mode==pipeline_contract::ControlMode::BodyLockFollow,
            "fresh pixels resume the same identity without another Snap");
        coordinator.update(fresh_no_selection(5,2.024),intent,2.024);
        const auto expired=coordinator.update(fresh_no_selection(6,2.052),intent,2.052);
        require_true(expired.target_id==0 && expired.aim_authority==0,
            "repeated misses cannot renew identity beyond original capture deadline");
    });
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
    registry.add_case("BaseAds", "identity_wait_loss_and_replacement", test_identity_wait_loss_and_replacement_boundaries);
    registry.add_case("BaseAds", "selector_replacement_preserves_single_snap", test_selector_generation_replacement_preserves_single_snap_token);
    registry.add_case("BaseAds", "replacement_after_consumed_snap_stays_bodylock", test_replacement_after_consumed_snap_stays_bodylock);
    registry.add_case("BaseBodyLock", "manual_correction_moves_d_only_inside_region", test_manual_correction_moves_d_only_inside_r);
    registry.add_case("BaseBodyLock", "cue_carries_corrected_desired_point", test_cue_carries_corrected_d_instead_of_replacing_it);
    registry.add_case("BaseBodyLock", "target_replacement_resets_corrected_point", test_target_replacement_resets_corrected_d);
    registry.add_case("BaseBodyLock", "firing_downward_does_not_arm_handover", test_firing_downward_input_moves_d_without_arming_handover);
    registry.add_case("BaseAds", "size_scaled_pickup_revalidated_per_epoch", test_size_scaled_pickup_is_revalidated_only_on_new_ads_epoch);
    registry.add_case("BaseAds", "pre_aim_selection_admitted_without_new_source", test_pre_aim_selection_admitted_without_new_source);
    registry.add_case("BaseAds", "pre_aim_controller_output", test_pre_aim_controller_output);
    registry.add_case("BaseAds", "pre_aim_new_epoch_cannot_replay_fire", test_pre_aim_new_epoch_cannot_replay_fire);
    registry.add_case("BaseAds", "pre_aim_random_development", [] { test_pre_aim_randomized(20261012); });
    registry.add_case("BaseAds", "pre_aim_random_validation", [] { test_pre_aim_randomized(0x71935ac2); });
    registry.add_case("BaseBodyLock", "unconfirmed_budget_keeps_safety_ceiling", test_unconfirmed_bodylock_keeps_conservative_safety_budget);
}
