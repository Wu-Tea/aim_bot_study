#include "target_coordinator.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::VisionObservationBatch frame(
    std::uint64_t frame_id,
    double time,
    std::uint64_t source_id,
    float x,
    float y,
    float reliability = 0.9f) {
    pipeline_contract::VisionObservationBatch batch{};
    batch.frame_id = frame_id;
    batch.preferred_source_id = source_id;
    batch.source_time_seconds = time;
    batch.publish_time_seconds = time;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.count = 1;
    batch.candidates[0].source_id = source_id;
    batch.candidates[0].aim_px = {x, y};
    batch.candidates[0].box_size_px = {40.0f, 80.0f};
    batch.candidates[0].confidence = reliability;
    batch.candidates[0].reliability = reliability;
    batch.candidates[0].normalized_size = 0.2f;
    batch.candidates[0].body_cue = true;
    return batch;
}

pipeline_contract::IntentState ads_intent(double time) {
    pipeline_contract::IntentState intent{};
    intent.ads = true;
    intent.sample_time_seconds = time;
    return intent;
}

pipeline_contract::IntentState firing_ads_intent(double time) {
    auto intent = ads_intent(time);
    intent.fire = true;
    return intent;
}

pipeline_contract::VisionObservationBatch empty_fresh_frame(
    std::uint64_t frame_id,
    double time) {
    pipeline_contract::VisionObservationBatch batch{};
    batch.frame_id = frame_id;
    batch.source_time_seconds = time;
    batch.publish_time_seconds = time;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    return batch;
}

pipeline_contract::VisionObservationBatch replay_tick(double time) {
    pipeline_contract::VisionObservationBatch batch{};
    batch.source_time_seconds = 0.0;
    batch.publish_time_seconds = 0.0;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = false;
    (void)time;
    return batch;
}

void test_coasting_actuation_lease_retires_before_identity_hold() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(
        frame(1, 1.000, 10, 300.0f, 208.0f),
        ads_intent(1.000),
        1.000);
    const auto target_id = plan.target_id;
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
                 "cover-retreat fixture must establish a fresh target");
    require_true(plan.aim_authority > 0.70f,
                 "cover-retreat fixture must establish material AI authority");

    // A 12.5ms single-frame miss is the continuity grace window. Then keep
    // publishing fresh empty frames so this fixture exercises the real
    // no-target/Coasting path rather than a stale control tick.
    plan = coordinator.update(
        empty_fresh_frame(2, 1.0125),
        ads_intent(1.0125),
        1.0125);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
                 "single fresh miss must enter Coasting");
    require_true(plan.target_id == target_id,
                 "short miss must preserve identity during the grace window");
    require_true(plan.aim_authority > 0.70f,
                 "12.5ms single miss must retain near-full actuation authority");

    plan = coordinator.update(
        empty_fresh_frame(3, 1.0250),
        ads_intent(1.0250),
        1.0250);
    plan = coordinator.update(
        empty_fresh_frame(4, 1.0400),
        ads_intent(1.0400),
        1.0400);
    plan = coordinator.update(
        empty_fresh_frame(5, 1.0550),
        ads_intent(1.0550),
        1.0550);
    plan = coordinator.update(
        empty_fresh_frame(6, 1.0660),
        ads_intent(1.0660),
        1.0660);

    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
                 "identity must still coast before the 180ms hold expires");
    require_true(plan.target_id == target_id,
                 "actuation retirement must not retire identity association");
    require_true(plan.reliability > 0.45f,
                 "cover-retreat must preserve the independent reliability lease");
    require_true(
        plan.aim_authority < 0.05f,
        "Coasting actuation must retire by roughly 65ms instead of following hold_ms");
}

void test_36ms_moving_occlusion_reacquires_without_false_stop() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(
        frame(1, 2.000, 20, 280.0f, 208.0f),
        ads_intent(2.000),
        2.000);
    const auto target_id = plan.target_id;
    const auto acquisition_id = plan.target_acquisition_id;
    plan = coordinator.update(
        frame(2, 2.010, 20, 286.0f, 208.0f),
        ads_intent(2.010),
        2.010);
    plan = coordinator.update(
        frame(3, 2.020, 20, 292.0f, 208.0f),
        ads_intent(2.020),
        2.020);

    plan = coordinator.update(
        empty_fresh_frame(4, 2.056),
        ads_intent(2.056),
        2.056);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
                 "36ms moving-target occlusion must remain in continuity hold");
    require_true(plan.target_id == target_id,
                 "36ms occlusion must not drop the canonical target");
    require_true(plan.reliability > 0.70f,
                 "36ms occlusion must retain enough evidence for reacquisition");
    require_true(
        plan.target_acquisition_id == acquisition_id &&
            plan.acquisition_elapsed_ms > 55.0f &&
            plan.acquisition_elapsed_ms < 57.0f,
        "same-target occlusion must not reset the admission-relative timer");

    plan = coordinator.update(
        frame(5, 2.057, 20, 307.0f, 208.0f),
        ads_intent(2.057),
        2.057);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring,
                 "moving target after 36ms must use same-owner Reacquiring");
    require_true(plan.target_id == target_id,
                 "same target after 36ms must retain canonical identity");
    require_true(std::fabs(plan.aim_px.x - 307.0f) < 0.01f,
                 "fresh moving-target position must remain authoritative");
    require_true(plan.mode != pipeline_contract::ControlMode::Manual,
                 "36ms moving occlusion must not false-stop the assisted mode");
    require_true(plan.aim_authority > 0.70f,
                 "same-target reacquire must restore fresh actuation authority");
    require_true(plan.target_acquisition_id == acquisition_id,
                 "same-target reacquire must preserve acquisition identity");
}

void test_different_target_after_expired_hold_uses_new_admission() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(
        frame(1, 3.000, 30, 300.0f, 208.0f),
        ads_intent(3.000),
        3.000);
    const auto old_target_id = plan.target_id;
    (void)coordinator.update(
        empty_fresh_frame(2, 3.010),
        ads_intent(3.010),
        3.010);

    plan = coordinator.update(
        empty_fresh_frame(3, 3.200),
        ads_intent(3.200),
        3.200);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::None,
                 "expired identity hold must release to None");
    require_true(plan.target_id == 0,
                 "expired identity hold must clear the old target id");

    plan = coordinator.update(
        frame(4, 3.201, 31, 350.0f, 208.0f),
        ads_intent(3.201),
        3.201);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
                 "different target must be admitted as a fresh observation");
    require_true(plan.target_id != 0 && plan.target_id != old_target_id,
                 "different target must receive a new canonical identity");
    require_true(plan.source_observation_id == 31,
                 "different-target admission must export the new source id");
}

void test_single_owner_coasts_and_reacquires_same_identity() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(frame(1, 0.00, 10, 300.0f, 208.0f), ads_intent(0.00), 0.00);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
                 "fresh target must be observed");
    require_true(plan.source_observation_id == 10,
                 "observed plan must export the exact selected observation identity");
    const auto target_id = plan.target_id;

    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_id = 2;
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    missing.capture_fresh = true;
    plan = coordinator.update(missing, ads_intent(0.05), 0.05);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
                 "short occlusion must coast one owner");
    require_true(plan.target_id == target_id, "coasting must retain identity");

    plan = coordinator.update(frame(3, 0.08, 99, 304.0f, 208.0f), ads_intent(0.08), 0.08);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring,
                 "nearby new detector id must reacquire existing target");
    require_true(plan.target_id == target_id, "detector id churn must not change plan identity");
    require_true(plan.source_observation_id == 99,
                 "reacquired plan must export the newly committed observation identity");
    require_true(std::fabs(plan.error_px.x - 64.0f) < 12.0f,
                 "reacquisition innovation must be bounded");
}

void test_hold_expires_to_safe_manual_plan() {
    controller_native::TargetCoordinator coordinator;
    coordinator.update(frame(1, 0.0, 1, 300.0f, 208.0f), ads_intent(0.0), 0.0);
    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    const auto plan = coordinator.update(missing, ads_intent(0.40), 0.40);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::None,
                 "expired hold must release the target");
    require_true(plan.mode == pipeline_contract::ControlMode::Manual,
                 "released plan must be manual");
    require_true(plan.aim_authority == 0.0f, "released plan must have zero authority");
}

void test_selector_owned_batch_does_not_acquire_unselected_candidate() {
    controller_native::TargetCoordinator coordinator;
    auto candidate_only = frame(1, 0.0, 77, 300.0f, 208.0f);
    candidate_only.selector_identity_protocol = true;
    candidate_only.preferred_source_id = 0;

    auto plan = coordinator.update(
        candidate_only, ads_intent(0.0), 0.0);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::None,
                 "selector-owned candidate without a selection must stay manual");
    require_true(plan.aim_authority == 0.0f,
                 "unselected detector candidate must not gain blind aim authority");

    candidate_only.frame_id = 2;
    candidate_only.source_time_seconds = 0.01;
    candidate_only.publish_time_seconds = 0.01;
    candidate_only.preferred_source_id = 77;
    plan = coordinator.update(candidate_only, ads_intent(0.01), 0.01);
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
                 "an explicit selector choice must remain eligible for acquisition");
}

void test_motion_labels_jump_then_fall() {
    controller_native::TargetCoordinator coordinator;
    auto intent = ads_intent(0.0);
    coordinator.update(frame(1, 0.00, 1, 240.0f, 220.0f), intent, 0.00);
    coordinator.update(frame(2, 0.02, 1, 240.0f, 210.0f), intent, 0.02);
    auto plan = coordinator.update(frame(3, 0.04, 1, 240.0f, 198.0f), intent, 0.04);
    require_true(plan.motion == pipeline_contract::TargetMotion::Jump,
                 "persistent upward image motion must classify jump");
    coordinator.update(frame(4, 0.06, 1, 240.0f, 202.0f), intent, 0.06);
    coordinator.update(frame(5, 0.08, 1, 240.0f, 214.0f), intent, 0.08);
    plan = coordinator.update(frame(6, 0.10, 1, 240.0f, 228.0f), intent, 0.10);
    require_true(plan.motion == pipeline_contract::TargetMotion::Fall,
                 "persistent downward image motion must classify fall");
}

void test_jump_cue_adds_causal_vertical_acceleration_projection() {
    auto prepare = [](controller_native::TargetCoordinator& coordinator) {
        coordinator.update(
            frame(1, 1.00, 1, 240.0f, 220.0f),
            ads_intent(1.00), 1.00);
        coordinator.update(
            frame(2, 1.01, 1, 240.0f, 210.0f),
            ads_intent(1.01), 1.01);
    };
    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;

    controller_native::TargetCoordinator baseline;
    controller_native::TargetCoordinator jump_model;
    controller_native::TargetCoordinator manual_owned;
    baseline.set_causal_player_motion_enabled_for_benchmark(false, false);
    jump_model.set_causal_player_motion_enabled_for_benchmark(false, false);
    manual_owned.set_causal_player_motion_enabled_for_benchmark(false, false);
    prepare(baseline);
    prepare(jump_model);
    prepare(manual_owned);

    controller_native::TargetControlFeedback jump_feedback{};
    jump_feedback.player_jump_action_age_ms = 100.0f;
    const auto baseline_plan = baseline.update(
        missing, ads_intent(1.02), 1.02);
    const auto jump_plan = jump_model.update(
        missing, ads_intent(1.02), 1.02, jump_feedback);
    require_true(
        jump_plan.error_px.y < baseline_plan.error_px.y - 0.5f,
        "jump cue must integrate measured vertical acceleration between vision frames");

    auto manual_intent = ads_intent(1.02);
    manual_intent.filtered_right.y = 0.20f;
    manual_intent.right_y.confidence = 1.0f;
    const auto manual_plan = manual_owned.update(
        missing, manual_intent, 1.02, jump_feedback);
    require_true(
        std::fabs(manual_plan.error_px.y - baseline_plan.error_px.y) < 0.1f,
        "manual camera ownership must suppress duplicate jump extrapolation");
}

void test_player_motion_oracle_separates_realized_camera_error() {
    controller_native::TargetCoordinator baseline;
    controller_native::TargetCoordinator oracle;
    baseline.update(
        frame(1, 1.00, 1, 240.0f, 220.0f),
        ads_intent(1.00), 1.00);
    oracle.update(
        frame(1, 1.00, 1, 240.0f, 220.0f),
        ads_intent(1.00), 1.00);

    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    controller_native::TargetControlFeedback feedback{};
    feedback.has_player_motion_oracle = true;
    feedback.has_player_motion_rate_oracle = true;
    feedback.player_error_delta_px = {0.0f, -4.0f};
    feedback.player_error_rate_px_per_sec = {0.0f, -400.0f};

    const auto baseline_plan = baseline.update(
        missing, ads_intent(1.01), 1.01);
    const auto oracle_plan = oracle.update(
        missing, ads_intent(1.01), 1.01, feedback);
    require_true(
        std::fabs(
            oracle_plan.error_px.y -
            (baseline_plan.error_px.y - 4.0f)) < 0.01f,
        "oracle must apply the exact realized player-motion error delta");
    require_true(
        std::fabs(oracle_plan.error_rate_px_per_sec.y + 400.0f) < 0.01f,
        "oracle must expose player-motion rate to the control horizon");
}

void test_causal_slide_model_separates_state_and_forecast() {
    controller_native::TargetCoordinator coordinator;
    coordinator.set_causal_player_motion_enabled_for_benchmark(true, true);
    coordinator.update(
        frame(1, 1.00, 1, 240.0f, 220.0f),
        ads_intent(1.00), 1.00);
    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;

    controller_native::TargetControlFeedback onset{};
    onset.player_slide_action_age_ms = 0.0f;
    coordinator.update(missing, ads_intent(1.001), 1.001, onset);
    controller_native::TargetControlFeedback falling{};
    falling.player_slide_action_age_ms = 60.0f;
    const auto plan = coordinator.update(
        missing, ads_intent(1.061), 1.061, falling);
    require_true(
        plan.error_px.y < 12.0f,
        "causal slide model must apply realized upward screen displacement");
    require_true(
        plan.player_motion_forecast_px.y < 0.0f &&
        plan.player_motion_confidence > 0.0f,
        "causal slide model must export a separate short forecast");
    require_true(
        std::fabs(plan.error_rate_px_per_sec.y) < 0.01f,
        "causal slide forecast must not leak into raw target velocity");
}

void test_player_motion_forecast_only_bridges_between_vision_frames() {
    controller_native::TargetCoordinator coordinator;
    coordinator.set_causal_player_motion_enabled_for_benchmark(false, true);
    coordinator.update(
        frame(1, 1.000, 1, 240.0f, 220.0f),
        ads_intent(1.000), 1.000);

    controller_native::TargetControlFeedback fresh_feedback{};
    fresh_feedback.player_slide_action_age_ms = 10.0f;
    const auto fresh_plan = coordinator.update(
        frame(2, 1.010, 1, 240.0f, 220.0f),
        ads_intent(1.010), 1.010, fresh_feedback);
    require_true(
        fresh_plan.player_motion_confidence == 0.0f,
        "fresh Vision must own the observed point without duplicate event forecast");

    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    controller_native::TargetControlFeedback partial_feedback{};
    partial_feedback.player_slide_action_age_ms = 16.0f;
    const auto partial_plan = coordinator.update(
        missing, ads_intent(1.016), 1.016, partial_feedback);
    controller_native::TargetControlFeedback bridged_feedback{};
    bridged_feedback.player_slide_action_age_ms = 24.0f;
    const auto bridged_plan = coordinator.update(
        missing, ads_intent(1.024), 1.024, bridged_feedback);
    require_true(
        partial_plan.player_motion_confidence > 0.0f &&
        bridged_plan.player_motion_confidence >
            partial_plan.player_motion_confidence,
        "event forecast authority must ramp continuously across the Vision gap");
}

void test_selector_replacement_preserves_held_player_motion_state() {
    auto prepare = [](controller_native::TargetCoordinator& coordinator) {
        coordinator.set_causal_player_motion_enabled_for_benchmark(true, true);
        coordinator.set_motion_velocity_alpha_for_benchmark(0.0f);
        coordinator.begin_ads_epoch(122, 60.000);
        auto first = frame(1, 60.000, 11, 340.0f, 208.0f, 0.5f);
        first.selector_identity_protocol = true;
        first.selector_target_generation = 1;
        (void)coordinator.update(first, ads_intent(60.000), 60.000);

        auto second = frame(2, 60.010, 11, 340.0f, 208.0f, 0.5f);
        second.selector_identity_protocol = true;
        second.selector_target_generation = 1;
        controller_native::TargetControlFeedback jump{};
        jump.player_jump_action_age_ms = 100.0f;
        (void)coordinator.update(second, ads_intent(60.010), 60.010, jump);
    };

    controller_native::TargetCoordinator replacement;
    controller_native::TargetCoordinator same_identity;
    prepare(replacement);
    prepare(same_identity);

    auto replacement_frame = frame(3, 60.020, 99, 270.0f, 208.0f, 0.5f);
    replacement_frame.selector_identity_protocol = true;
    replacement_frame.selector_target_generation = 2;
    replacement_frame.selector_target_changed = true;
    auto same_frame = replacement_frame;
    same_frame.selector_target_generation = 1;
    same_frame.selector_target_changed = false;
    controller_native::TargetControlFeedback held_jump{};
    held_jump.player_jump_action_age_ms = 120.0f;
    const auto replacement_plan = replacement.update(
        replacement_frame, ads_intent(60.020), 60.020, held_jump);
    (void)same_identity.update(
        same_frame, ads_intent(60.020), 60.020, held_jump);

    controller_native::TargetControlFeedback held_jump_next{};
    held_jump_next.player_jump_action_age_ms = 130.0f;
    const auto replacement_replay = replacement.update(
        replay_tick(60.030), ads_intent(60.030), 60.030, held_jump_next);
    const auto same_identity_replay = same_identity.update(
        replay_tick(60.030), ads_intent(60.030), 60.030, held_jump_next);
    require_true(
        replacement_plan.player_motion_forecast_px.y > 0.0f &&
            replacement_replay.player_motion_forecast_px.y > 0.0f,
        "held jump forecast must remain available across selector replacement");
    require_true(
        std::fabs(
            replacement_replay.player_motion_forecast_px.y -
            same_identity_replay.player_motion_forecast_px.y) < 0.01f,
        "replacement must preserve the global jump amplitude/forecast state");
    require_true(
        std::fabs(
            replacement_replay.error_px.y -
            same_identity_replay.error_px.y) < 0.01f,
        "replacement must not drop one realized held-jump delta by clearing its event state");
}

void test_selector_replacement_preserves_held_slide_motion_state() {
    auto prepare = [](controller_native::TargetCoordinator& coordinator) {
        coordinator.set_causal_player_motion_enabled_for_benchmark(true, true);
        coordinator.set_motion_velocity_alpha_for_benchmark(0.0f);
        coordinator.begin_ads_epoch(123, 61.000);
        auto first = frame(1, 61.000, 11, 340.0f, 208.0f, 0.5f);
        first.selector_identity_protocol = true;
        first.selector_target_generation = 1;
        (void)coordinator.update(first, ads_intent(61.000), 61.000);

        auto second = frame(2, 61.010, 11, 340.0f, 208.0f, 0.5f);
        second.selector_identity_protocol = true;
        second.selector_target_generation = 1;
        controller_native::TargetControlFeedback slide{};
        slide.player_slide_action_age_ms = 60.0f;
        (void)coordinator.update(second, ads_intent(61.010), 61.010, slide);
    };

    controller_native::TargetCoordinator replacement;
    controller_native::TargetCoordinator same_identity;
    prepare(replacement);
    prepare(same_identity);

    auto replacement_frame = frame(3, 61.020, 99, 270.0f, 208.0f, 0.5f);
    replacement_frame.selector_identity_protocol = true;
    replacement_frame.selector_target_generation = 2;
    replacement_frame.selector_target_changed = true;
    auto same_frame = replacement_frame;
    same_frame.selector_target_generation = 1;
    same_frame.selector_target_changed = false;
    controller_native::TargetControlFeedback held_slide{};
    held_slide.player_slide_action_age_ms = 70.0f;
    const auto replacement_plan = replacement.update(
        replacement_frame, ads_intent(61.020), 61.020, held_slide);
    (void)same_identity.update(
        same_frame, ads_intent(61.020), 61.020, held_slide);

    controller_native::TargetControlFeedback held_slide_next{};
    held_slide_next.player_slide_action_age_ms = 80.0f;
    const auto replacement_replay = replacement.update(
        replay_tick(61.030), ads_intent(61.030), 61.030, held_slide_next);
    const auto same_identity_replay = same_identity.update(
        replay_tick(61.030), ads_intent(61.030), 61.030, held_slide_next);
    require_true(
        replacement_plan.player_motion_forecast_px.y < 0.0f &&
            replacement_replay.player_motion_forecast_px.y < 0.0f,
        "held slide forecast must remain available across selector replacement");
    require_true(
        std::fabs(
            replacement_replay.player_motion_forecast_px.y -
            same_identity_replay.player_motion_forecast_px.y) < 0.01f,
        "replacement must preserve the global slide amplitude/forecast state");
    require_true(
        std::fabs(
            replacement_replay.error_px.y - same_identity_replay.error_px.y) <
            0.01f,
        "replacement must not drop one realized held-slide delta by clearing event state");
}

void test_ads_handoff_waits_for_settle() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(frame(1, 0.00, 1, 340.0f, 208.0f), ads_intent(0.00), 0.00);
    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "large initial error must use ADS acquisition");
    for (int i = 1; i <= 20; ++i) {
        const double time = i * 0.02;
        plan = coordinator.update(frame(i + 1, time, 1, 243.0f, 208.0f), ads_intent(time), time);
    }
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "fresh settled frames must hand off to BodyLock");
    plan = coordinator.update(frame(30, 0.42, 1, 260.0f, 208.0f), ads_intent(0.42), 0.42);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "BodyLock must use a wider exit band than its ADS entry band");
}

void test_bodylock_cannot_rearm_ads_within_one_held_epoch() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    config.settle_radius_px = 8.0f;
    config.bodylock_exit_radius_px = 48.0f;
    controller_native::TargetCoordinator coordinator(config);

    coordinator.begin_ads_epoch(1, 1.000);
    auto plan = coordinator.update(
        frame(1, 1.000, 1, 242.0f, 208.0f), ads_intent(1.000), 1.000);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "opening target must settle into BodyLock");
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed,
        "handoff must expose a distinct Completed state before Consumed");
    const auto acquisition_id = plan.target_acquisition_id;

    plan = coordinator.update(
        frame(2, 1.050, 2, 300.0f, 208.0f), ads_intent(1.050), 1.050);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "held ADS epoch must not rearm snap for a new far target");
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Consumed &&
        plan.target_acquisition_id == acquisition_id &&
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::AdsAlreadyConsumed,
        "held-LT replacement must advance to Consumed without rearming");
}

void test_ads_ownership_ceiling_is_independent_from_arrival_horizon() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 220.0f;
    config.bodylock_activation_radius_px = 80.0f;
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    pipeline_contract::TargetPlan plan{};
    plan = coordinator.update(
        frame(1, 0.130, 1, 340.0f, 208.0f), ads_intent(0.130), 0.130);
    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "arrival horizon must not terminate ADS ownership");
    require_true(std::fabs(plan.ads_epoch_elapsed_ms - 130.0f) < 0.1f,
                 "plan must expose physical ADS epoch elapsed time");
    plan = coordinator.update(
        frame(2, 0.265, 1, 300.0f, 208.0f), ads_intent(0.265), 0.265);
    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "nominal acquisition must extend while the target is still closing");
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "135ms admission-relative acquisition must enter the explicit extended state");
    plan = coordinator.update(
        frame(3, 0.355, 1, 300.0f, 208.0f), ads_intent(0.355), 0.355);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "admission-relative hard ceiling must consume ADS at 220ms");
}

void test_ads_timed_fallback_consumes_snap_without_far_bodylock_pull() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 20.0f;
    config.bodylock_activation_radius_px = 80.0f;
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);
    (void)coordinator.update(
        frame(1, 0.000, 1, 340.0f, 208.0f), ads_intent(0.000), 0.000);
    const auto plan = coordinator.update(
        frame(2, 0.021, 1, 340.0f, 208.0f), ads_intent(0.021), 0.021);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "ADS snap must be permanently consumed at its total deadline");
    require_true(plan.aim_authority == 0.0f,
                 "far residual must remain manual until it enters BodyLock range");
}

void test_late_target_gets_nominal_window_from_admission() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 220.0f;
    config.bodylock_activation_radius_px = 80.0f;
    config.settle_frames = 5;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    (void)coordinator.update(
        empty_fresh_frame(1, 0.100), ads_intent(0.100), 0.100);
    (void)coordinator.update(
        empty_fresh_frame(2, 0.180), ads_intent(0.180), 0.180);

    // The first eligible target appears 182ms after the physical ADS edge.
    // Its nominal acquisition window must begin here, not at the LT edge.
    auto plan = coordinator.update(
        frame(3, 0.182, 7, 340.0f, 208.0f),
        ads_intent(0.182),
        0.182);
    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "late target must start a fresh ADS acquisition");

    plan = coordinator.update(
        frame(4, 0.300, 7, 340.0f, 208.0f),
        ads_intent(0.300),
        0.300);
    require_true(
        plan.mode == pipeline_contract::ControlMode::AdsAcquire,
        "late target must retain its 135ms nominal window from admission");
    require_true(
        plan.acquisition_elapsed_ms > 117.0f &&
            plan.acquisition_elapsed_ms < 119.0f,
        "late target acquisition elapsed time must be admission-relative");
}

void test_ads_base_range_rejects_small_far_target() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_activation_radius_px = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    auto small_far = frame(1, 0.010, 1, 360.0f, 208.0f);
    small_far.candidates[0].normalized_size = 0.0f;
    const auto plan = coordinator.update(
        small_far, ads_intent(0.010), 0.010);

    require_true(
        plan.lifecycle == pipeline_contract::TargetLifecycle::None,
        "small target outside the configured ADS range must remain manual");
}

void test_close_target_geometry_expands_ads_initial_range() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_activation_radius_px = 100.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    auto close_target = frame(1, 0.010, 1, 360.0f, 208.0f);
    close_target.candidates[0].normalized_size = 0.50f;
    const auto plan = coordinator.update(
        close_target, ads_intent(0.010), 0.010);

    require_true(
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
            plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
            plan.aim_authority > 0.0f,
        "large close target must expand the initial ADS activation range");
}

void test_close_target_geometry_expands_bodylock_continuation_range() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 20.0f;
    config.bodylock_activation_radius_px = 120.0f;
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    auto close_target = frame(1, 0.000, 1, 240.0f, 18.0f);
    close_target.candidates[0].box_size_px = {140.0f, 374.0f};
    close_target.candidates[0].normalized_size = 0.90f;
    (void)coordinator.update(close_target, ads_intent(0.000), 0.000);
    close_target = frame(2, 0.050, 1, 240.0f, 18.0f);
    close_target.candidates[0].box_size_px = {140.0f, 374.0f};
    close_target.candidates[0].normalized_size = 0.90f;
    const auto plan = coordinator.update(
        close_target, ads_intent(0.050), 0.050);

    require_true(
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "test setup must consume ADS into BodyLock");
    require_true(
        std::fabs(plan.error_px.y) > config.bodylock_activation_radius_px,
        "test target must sit outside the legacy fixed BodyLock radius");
    require_true(
        plan.aim_authority > 0.0f,
        "a close target that fills the capture must retain BodyLock authority");
}

void test_close_target_geometry_does_not_expand_blind_coast_range() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 20.0f;
    config.bodylock_activation_radius_px = 120.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(1, 0.0);

    auto close_target = frame(1, 0.000, 1, 240.0f, 18.0f);
    close_target.candidates[0].box_size_px = {140.0f, 374.0f};
    close_target.candidates[0].normalized_size = 0.90f;
    (void)coordinator.update(close_target, ads_intent(0.000), 0.000);
    close_target = frame(2, 0.050, 1, 240.0f, 18.0f);
    close_target.candidates[0].box_size_px = {140.0f, 374.0f};
    close_target.candidates[0].normalized_size = 0.90f;
    (void)coordinator.update(close_target, ads_intent(0.050), 0.050);

    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    missing.capture_fresh = true;
    const auto coast = coordinator.update(
        missing, ads_intent(0.060), 0.060);
    require_true(
        coast.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "test setup must enter tracker-only coast");
    require_true(
        coast.aim_authority == 0.0f,
        "stale close-target geometry must not widen blind BodyLock pulls");
}

void test_ads_handoff_rejects_a_predicted_high_speed_crossing() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    config.settle_radius_px = 8.0f;
    config.handoff_prediction_seconds = 0.040f;
    controller_native::TargetCoordinator coordinator(config);

    controller_native::TargetControlFeedback feedback{};
    feedback.previous_delivered_stick = {0.80f, 0.0f};
    feedback.aim_response_px_per_stick_second = 500.0f;
    feedback.aim_response_confidence = 1.0f;
    const auto plan = coordinator.update(
        frame(1, 1.0, 1, 244.0f, 208.0f), ads_intent(1.0), 1.0, feedback);

    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "inside-radius ADS must not hand off while delivered input predicts crossing");
    require_true(plan.predicted_terminal_error_px.x < -8.0f,
                 "capture telemetry must expose the predicted opposite-side residual");
}

void test_ads_handoff_accepts_stable_in_radius_capture() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    config.settle_radius_px = 8.0f;
    controller_native::TargetCoordinator coordinator(config);

    const auto plan = coordinator.update(
        frame(1, 2.0, 1, 244.0f, 208.0f), ads_intent(2.0), 2.0);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "stable fresh evidence inside the capture set must hand off to BodyLock");
}

void test_ads_handoff_allows_tangential_target_motion() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.settle_radius_px = 8.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.update(frame(1, 3.00, 1, 244.0f, 202.0f), ads_intent(3.00), 3.00);
    const auto plan = coordinator.update(
        frame(2, 3.01, 1, 244.0f, 208.0f), ads_intent(3.01), 3.01);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "tangential target motion must hand off to BodyLock follow");
}

void test_bodylock_exit_band_is_independent_from_ads_capture_radius() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    config.settle_radius_px = 8.0f;
    config.bodylock_exit_radius_px = 48.0f;
    controller_native::TargetCoordinator coordinator(config);
    auto plan = coordinator.update(
        frame(1, 4.00, 1, 243.0f, 208.0f), ads_intent(4.00), 4.00);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "test setup must enter BodyLock");
    plan = coordinator.update(
        frame(2, 4.01, 1, 270.0f, 208.0f), ads_intent(4.01), 4.01);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "ADS capture radius must not shrink the BodyLock exit band");
}

void test_moving_target_gets_a_bounded_expanded_capture_set() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.settle_radius_px = 8.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.update(frame(1, 5.00, 1, 248.0f, 208.0f), ads_intent(5.00), 5.00);
    auto plan = coordinator.update(
        frame(2, 5.01, 1, 250.0f, 208.0f), ads_intent(5.01), 5.01);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "moving near target should hand off inside a bounded dynamic capture set");
}

void test_control_rate_gaps_do_not_compound_reliability_decay() {
    controller_native::TargetCoordinator coordinator;
    coordinator.update(frame(1, 1.000, 1, 243.0f, 208.0f, 1.0f), ads_intent(1.000), 1.000);
    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    pipeline_contract::TargetPlan plan{};
    for (int tick = 1; tick <= 9; ++tick) {
        const double time = 1.000 + tick * 0.001;
        plan = coordinator.update(missing, ads_intent(time), time);
    }
    require_true(plan.reliability > 0.94f,
                 "1000 Hz control ticks between vision frames must decay from the last observation once");
}

void test_control_rate_gaps_preserve_ads_settle_progress() {
    controller_native::TargetCoordinator coordinator;
    pipeline_contract::TargetPlan plan{};
    for (int vision = 0; vision < 5; ++vision) {
        const double observed_time = 1.000 + vision * 0.010;
        plan = coordinator.update(
            frame(vision + 1, observed_time, 1, 243.0f, 208.0f),
            ads_intent(observed_time),
            observed_time);
        for (int tick = 1; tick < 10; ++tick) {
            const double time = observed_time + tick * 0.001;
            pipeline_contract::VisionObservationBatch missing{};
            missing.frame_width_px = 480.0f;
            missing.frame_height_px = 416.0f;
            plan = coordinator.update(missing, ads_intent(time), time);
        }
    }
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "control-rate coast ticks must not erase vision-rate ADS settle progress");
}

void test_100hz_motion_stays_finite_at_1000hz_control_rate() {
    controller_native::TargetCoordinator coordinator;
    pipeline_contract::TargetPlan plan{};
    for (int vision = 0; vision < 80; ++vision) {
        const double observed_time = 1.0 + vision * 0.010;
        plan = coordinator.update(
            frame(vision + 1, observed_time, 7, 240.0f + vision * 2.0f, 208.0f),
            ads_intent(observed_time), observed_time);
        for (int tick = 1; tick < 10; ++tick) {
            const double time = observed_time + tick * 0.001;
            pipeline_contract::VisionObservationBatch no_new_frame{};
            no_new_frame.frame_width_px = 480.0f;
            no_new_frame.frame_height_px = 416.0f;
            plan = coordinator.update(no_new_frame, ads_intent(time), time);
        }
    }
    require_true(plan.source_frame_id == 80,
                 "all stable-source 100 Hz observations must remain consumable");
    require_true(std::isfinite(plan.aim_px.x) && std::isfinite(plan.velocity_px_per_sec.x),
                 "1000 Hz prediction between observations must remain finite");
}

void test_replayed_vision_frame_is_consumed_once() {
    controller_native::TargetCoordinator coordinator;
    coordinator.update(
        frame(1, 2.000, 7, 240.0f, 208.0f),
        ads_intent(2.000), 2.000);
    auto repeated = frame(2, 2.010, 7, 242.0f, 208.0f);
    auto plan = coordinator.update(
        repeated, ads_intent(2.010), 2.010);
    const float velocity_after_fresh_frame =
        plan.velocity_px_per_sec.x;
    const float position_after_fresh_frame = plan.aim_px.x;

    for (int tick = 1; tick <= 9; ++tick) {
        const double now = 2.010 + tick * 0.001;
        plan = coordinator.update(
            repeated, ads_intent(now), now);
    }

    require_true(
        std::fabs(
            plan.velocity_px_per_sec.x -
            velocity_after_fresh_frame) < 0.01f,
        "one Vision frame must update target velocity only once");
    require_true(
        plan.aim_px.x > position_after_fresh_frame,
        "controller-rate replay must propagate, not pull back to a stale point");
}

void test_duplicate_and_backward_frames_cannot_reenter_as_observations() {
    controller_native::TargetCoordinator coordinator;
    (void)coordinator.update(
        frame(10, 30.000, 7, 280.0f, 208.0f),
        ads_intent(30.000), 30.000);
    auto plan = coordinator.update(
        frame(11, 30.010, 7, 286.0f, 208.0f),
        ads_intent(30.010), 30.010);
    const float accepted_x = plan.aim_px.x;

    auto duplicate = frame(11, 30.010, 7, 120.0f, 208.0f);
    plan = coordinator.update(duplicate, ads_intent(30.011), 30.011);
    require_true(
        plan.source_frame_id == 11,
        "duplicate frame must not replace the accepted source frame");
    require_true(
        plan.aim_px.x > accepted_x - 2.0f,
        "duplicate frame with changed pixels must be ignored, not remeasured");

    auto backward_time = frame(12, 30.005, 7, 110.0f, 208.0f);
    plan = coordinator.update(backward_time, ads_intent(30.012), 30.012);
    require_true(
        plan.source_frame_id == 11,
        "newer frame id with an older capture time must still be rejected");
    require_true(
        plan.aim_px.x > accepted_x - 3.0f,
        "capture-time rollback must not pull the tracker to stale pixels");

    auto backward = frame(9, 30.020, 7, 100.0f, 208.0f);
    plan = coordinator.update(backward, ads_intent(30.020), 30.020);
    require_true(
        plan.source_frame_id == 11,
        "backward frame id must not roll back tracker source identity");
    require_true(
        plan.aim_px.x > accepted_x - 4.0f,
        "backward frame must not pull the tracker to stale pixels");
}

void test_overage_capture_is_ignored_and_capture_age_is_preserved() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 50.0f;
    controller_native::TargetCoordinator coordinator(config);
    auto plan = coordinator.update(
        frame(1, 40.000, 9, 280.0f, 208.0f),
        ads_intent(40.030), 40.030);
    require_true(
        plan.observation_age_ms > 29.0f && plan.observation_age_ms < 31.0f,
        "tracker age must start at capture time, not controller consume time");

    plan = coordinator.update(
        frame(2, 40.040, 9, 100.0f, 208.0f),
        ads_intent(40.100), 40.100);
    require_true(
        plan.source_frame_id == 1,
        "capture older than the realtime budget must not become authoritative");
    require_true(
        plan.aim_px.x > 250.0f,
        "overage capture must not create a delayed jump to stale pixels");
}

void test_velocity_alpha_is_normalized_to_vision_interval() {
    using controller_native::motion_velocity_alpha_for_interval;
    const float reference = motion_velocity_alpha_for_interval(
        0.2f, 0.011f, 0.011f);
    const float high_rate = motion_velocity_alpha_for_interval(
        0.2f, 0.011f, 0.005f);
    const float low_rate = motion_velocity_alpha_for_interval(
        0.2f, 0.011f, 0.020f);
    require_true(std::fabs(reference - 0.2f) < 1e-6f,
                 "reference cadence must preserve historical tracker gain");
    require_true(high_rate > 0.09f && high_rate < 0.10f,
                 "200Hz must use the equivalent smaller per-frame gain");
    require_true(low_rate > 0.33f && low_rate < 0.34f,
                 "slower Vision must use the equivalent larger per-frame gain");
    const float two_high_rate_steps =
        1.0f - (1.0f - high_rate) * (1.0f - high_rate);
    const float one_100hz_step = motion_velocity_alpha_for_interval(
        0.2f, 0.011f, 0.010f);
    require_true(std::fabs(two_high_rate_steps - one_100hz_step) < 1e-5f,
                 "equal elapsed time must produce equal cumulative response");
}

pipeline_contract::TargetPlan run_constant_motion_at_interval(
    double interval_seconds) {
    controller_native::TargetCoordinator coordinator;
    pipeline_contract::TargetPlan plan{};
    constexpr double start_seconds = 20.0;
    constexpr double velocity_px_per_second = 120.0;
    const int frames = static_cast<int>(
        std::lround(1.0 / interval_seconds));
    for (int index = 0; index <= frames; ++index) {
        const double elapsed = index * interval_seconds;
        const double time = start_seconds + elapsed;
        plan = coordinator.update(
            frame(
                index + 1, time, 7,
                240.0f + static_cast<float>(
                    elapsed * velocity_px_per_second),
                208.0f),
            ads_intent(time), time);
    }
    return plan;
}

void test_constant_motion_response_is_cadence_invariant() {
    const auto at_100hz = run_constant_motion_at_interval(0.010);
    const auto at_200hz = run_constant_motion_at_interval(0.005);
    require_true(
        std::fabs(
            at_100hz.velocity_px_per_sec.x -
            at_200hz.velocity_px_per_sec.x) < 0.5f,
        "100Hz and 200Hz must converge to the same physical velocity");
    require_true(
        std::fabs(at_100hz.velocity_px_per_sec.x - 120.0f) < 1.0f,
        "time-normalized tracker must converge to true constant velocity");
}

void test_left_intent_enters_plan_through_learned_response() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 8.000);
    for (int i = 0; i < 80; ++i) {
        coordinator.observe_control_response({0.5f, -100.0f, true, false});
    }
    auto intent = ads_intent(0.0);
    intent.filtered_left.x = 0.5f;
    intent.left_confidence = 1.0f;
    const auto plan = coordinator.update(frame(1, 0.0, 1, 240.0f, 208.0f), intent, 0.0);
    require_true(plan.left_motion_response_scale < -190.0f,
                 "plan must carry signed learned left-stick response separately");
    require_true(plan.error_rate_px_per_sec.x < -90.0f,
                 "left intent must affect planned relative motion");
    require_true(plan.horizon[0].error_px.x < 0.0f,
                 "short plan must include left-stick feed-forward");
}

void test_aim_response_feedback_is_separate_from_left_motion_response() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 9.000);
    for (int i = 0; i < 80; ++i) {
        coordinator.observe_control_response({0.5f, -100.0f, true, false});
    }
    controller_native::TargetControlFeedback feedback{};
    feedback.aim_response_px_per_stick_second = 730.0f;
    feedback.aim_response_confidence = 0.8f;
    const auto plan = coordinator.update(
        frame(1, 0.0, 1, 240.0f, 208.0f), ads_intent(0.0), 0.0, feedback);
    require_true(std::fabs(plan.response_scale - 730.0f) < 0.01f,
                 "aim controller must receive the learned right-stick camera response");
    require_true(std::fabs(plan.left_motion_response_scale - plan.response_scale) > 100.0f,
                 "left motion and right-stick camera response must not share one scale");
}

void test_nonfresh_empty_ticks_preserve_observed_fire_plan() {
    controller_native::TargetCoordinator coordinator;
    auto observed = frame(1, 10.000, 77, 240.0f, 208.0f);
    observed.fire_requested = true;
    observed.observed_fire_eligible = true;
    auto plan = coordinator.update(observed, ads_intent(10.000), 10.000);
    require_true(plan.fire_authority, "strong observed frame must authorize fire");

    pipeline_contract::VisionObservationBatch no_publication{};
    no_publication.frame_width_px = 480.0f;
    no_publication.frame_height_px = 416.0f;
    no_publication.capture_fresh = false;
    for (int tick = 1; tick <= 9; ++tick) {
        const double time = 10.000 + tick * 0.001;
        plan = coordinator.update(no_publication, ads_intent(time), time);
        require_true(
            plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
            "no-publication tick must preserve the established aim continuity path");
        require_true(
            plan.fire_requested && plan.fire_authority,
            "frame gap must preserve live fire eligibility");
    }
}

void test_fresh_processed_miss_revokes_fire_immediately() {
    controller_native::TargetCoordinator coordinator;
    auto observed = frame(1, 11.000, 77, 240.0f, 208.0f);
    observed.fire_requested = true;
    observed.observed_fire_eligible = true;
    auto plan = coordinator.update(observed, ads_intent(11.000), 11.000);
    require_true(plan.fire_authority, "precondition: observed target must authorize fire");

    pipeline_contract::VisionObservationBatch miss{};
    miss.frame_id = 2;
    miss.frame_width_px = 480.0f;
    miss.frame_height_px = 416.0f;
    miss.capture_fresh = true;
    plan = coordinator.update(miss, ads_intent(11.010), 11.010);
    require_true(
        plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "processed miss may retain aim-only coast");
    require_true(
        !plan.fire_requested && !plan.fire_authority,
        "processed miss must revoke synthetic fire immediately");
}

void test_anonymous_in_radius_hold_preserves_identity_and_fire_request() {
    controller_native::TargetCoordinator coordinator;
    auto observed = frame(1, 12.000, 77, 240.0f, 208.0f);
    observed.fire_requested = true;
    observed.observed_fire_eligible = true;
    auto plan = coordinator.update(observed, ads_intent(12.000), 12.000);
    const auto target_id = plan.target_id;

    auto anonymous = frame(2, 12.010, 0, 242.0f, 208.0f);
    anonymous.fire_requested = true;
    anonymous.observed_fire_eligible = true;
    plan = coordinator.update(anonymous, ads_intent(12.010), 12.010);
    require_true(plan.target_id == target_id, "anonymous hold must retain plan identity");
    require_true(plan.fire_requested && plan.fire_authority,
                 "strong anonymous in-radius hold must retain fire request");

    auto named = frame(3, 12.020, 77, 244.0f, 208.0f);
    named.fire_requested = true;
    named.observed_fire_eligible = true;
    plan = coordinator.update(named, ads_intent(12.020), 12.020);
    require_true(plan.target_id == target_id,
                 "anonymous hold must not discard the named source owner");
}

void test_vision_fire_authority_is_not_rejected_by_reliability_weight() {
    controller_native::TargetCoordinator coordinator;
    auto observed = frame(1, 13.000, 9, 240.0f, 208.0f, 0.65f);
    observed.fire_requested = true;
    observed.observed_fire_eligible = true;
    const auto plan = coordinator.update(observed, ads_intent(13.000), 13.000);
    require_true(plan.fire_authority,
                 "coordinator must trust Vision's observed fire eligibility");
}

void test_delivered_camera_work_is_consumed_during_control_rate_prediction() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(
        frame(1, 20.000, 44, 300.0f, 208.0f),
        ads_intent(20.000),
        20.000);
    require_true(std::fabs(plan.error_px.x - 60.0f) < 0.001f,
                 "fresh Vision must anchor the initial work");

    pipeline_contract::VisionObservationBatch no_publication{};
    no_publication.frame_width_px = 480.0f;
    no_publication.frame_height_px = 416.0f;
    controller_native::TargetControlFeedback feedback{};
    feedback.apply_delivered_camera_work = true;
    feedback.delivered_camera_work_delta_px = {5.0f, -3.0f};
    plan = coordinator.update(
        no_publication,
        ads_intent(20.010),
        20.010,
        feedback);
    require_true(std::fabs(plan.error_px.x - 55.0f) < 0.001f,
                 "delivered rightward work must reduce positive X work once");
    require_true(std::fabs(plan.error_px.y - 3.0f) < 0.001f,
                 "delivered upward work must move negative screen-Y work toward zero");

    plan = coordinator.update(
        frame(2, 20.020, 44, 295.0f, 211.0f),
        ads_intent(20.020),
        20.020);
    require_true(std::fabs(plan.velocity_px_per_sec.x) < 0.01f,
                 "fresh Vision matching delivered camera work must not learn camera motion as target velocity");
    require_true(std::fabs(plan.velocity_px_per_sec.y) < 0.01f,
                 "camera-attributed Y motion must not leak into target velocity");
}

void test_fresh_vision_reanchors_to_work_delivered_since_capture() {
    controller_native::TargetCoordinator coordinator;
    (void)coordinator.update(
        frame(1, 21.000, 44, 300.0f, 208.0f),
        ads_intent(21.000),
        21.000);

    controller_native::TargetControlFeedback feedback{};
    feedback.has_delivered_camera_work_since_capture = true;
    feedback.delivered_camera_work_since_capture_px = {5.0f, -3.0f};
    feedback.remaining_work_confidence = 0.55f;
    const auto plan = coordinator.update(
        frame(2, 21.010, 44, 300.0f, 208.0f),
        ads_intent(21.010),
        21.010,
        feedback);

    require_true(plan.remaining_work_valid,
                 "fresh capture must publish a valid remaining-work state");
    require_true(std::fabs(plan.error_px.x - 55.0f) < 0.001f,
                 "work delivered after capture must be removed from fresh X error");
    require_true(std::fabs(plan.error_px.y - 3.0f) < 0.001f,
                 "fresh screen-Y work must use the controller sign exactly once");
    require_true(
        std::fabs(
            plan.delivered_camera_motion_since_capture_px.x - 5.0f) <
            0.001f,
        "plan must expose delivered motion for telemetry and consumers");
}

void test_bodylock_velocity_obeys_target_acceleration_limit() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 1.0);
    auto plan = coordinator.update(
        frame(1, 1.00, 1, 244.0f, 208.0f),
        firing_ads_intent(1.00), 1.00);
    for (std::uint64_t id = 2; id <= 8; ++id) {
        const double time = 1.0 + static_cast<double>(id - 1) * 0.01;
        plan = coordinator.update(
            frame(id, time, 1, 244.0f, 208.0f),
            firing_ads_intent(time), time);
    }
    require_true(
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "fixture must enter BodyLock before acceleration stress");
    const auto before = plan.velocity_px_per_sec;
    plan = coordinator.update(
        frame(9, 1.08, 1, 262.0f, 190.0f),
        ads_intent(1.08), 1.08);
    const float velocity_delta = std::hypot(
        plan.velocity_px_per_sec.x - before.x,
        plan.velocity_px_per_sec.y - before.y);
    require_true(
        velocity_delta <= 30.1f,
        "BodyLock target velocity must respect the 3000 px/s^2 vector limit");
}

void test_bodylock_bounds_impulse_without_delayed_release() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 1.0);
    auto plan = coordinator.update(
        frame(1, 1.00, 1, 244.0f, 208.0f),
        ads_intent(1.00), 1.00);
    for (std::uint64_t id = 2; id <= 8; ++id) {
        const double time = 1.0 + static_cast<double>(id - 1) * 0.01;
        plan = coordinator.update(
            frame(id, time, 1, 244.0f, 208.0f),
            ads_intent(time), time);
    }
    require_true(
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "fixture must enter BodyLock before robust observation update");

    plan = coordinator.update(
        frame(9, 1.08, 1, 252.0f, 200.0f),
        firing_ads_intent(1.08), 1.08);
    require_true(
        std::fabs(plan.aim_px.x - 252.0f) < 0.5f &&
            std::fabs(plan.aim_px.y - 200.0f) < 0.5f,
        "a fresh firing position must remain authoritative");
    require_true(
        std::hypot(plan.velocity_px_per_sec.x, plan.velocity_px_per_sec.y) <=
            80.0f,
        "a first gun-kick residual must have bounded velocity influence");
    plan = coordinator.update(
        frame(10, 1.09, 1, 244.0f, 208.0f),
        firing_ads_intent(1.09), 1.09);
    require_true(
        std::fabs(plan.aim_px.x - 244.0f) < 0.5f &&
            std::fabs(plan.aim_px.y - 208.0f) < 0.5f,
        "a recovering impulse must return without delayed residual release");

    plan = coordinator.update(
        frame(11, 1.10, 1, 250.0f, 208.0f),
        firing_ads_intent(1.10), 1.10);
    require_true(
        std::fabs(plan.aim_px.x - 250.0f) < 0.5f,
        "persistent fresh position must be accepted continuously");
    plan = coordinator.update(
        frame(12, 1.11, 1, 256.0f, 208.0f),
        firing_ads_intent(1.11), 1.11);
    require_true(
        std::fabs(plan.aim_px.x - 256.0f) < 0.5f,
        "second persistent fresh position must remain authoritative");
    plan = coordinator.update(
        frame(13, 1.12, 1, 262.0f, 208.0f),
        firing_ads_intent(1.12), 1.12);
    require_true(
        std::fabs(plan.aim_px.x - 262.0f) < 0.5f,
        "persistent motion must not be held at the old predicted position");
}

void test_bodylock_reacquire_preserves_fresh_position_without_velocity_impulse() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 1.0);
    auto plan = coordinator.update(
        frame(1, 1.00, 1, 244.0f, 208.0f),
        firing_ads_intent(1.00), 1.00);
    for (std::uint64_t id = 2; id <= 8; ++id) {
        const double time = 1.0 + static_cast<double>(id - 1) * 0.01;
        plan = coordinator.update(
            frame(id, time, 1, 244.0f, 208.0f),
            firing_ads_intent(time), time);
    }
    require_true(
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "fixture must enter BodyLock before the occlusion");

    pipeline_contract::VisionObservationBatch missing{};
    missing.frame_id = 9;
    missing.frame_width_px = 480.0f;
    missing.frame_height_px = 416.0f;
    missing.capture_fresh = true;
    plan = coordinator.update(
        missing, firing_ads_intent(1.08), 1.08);
    require_true(
        plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "fresh miss must enter the short BodyLock coast");

    plan = coordinator.update(
        frame(10, 1.09, 1, 244.0f, 191.0f),
        firing_ads_intent(1.09), 1.09);
    require_true(
        plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring,
        "same target must retain the reacquiring lifecycle");
    require_true(
        std::fabs(plan.aim_px.x - 244.0f) < 0.5f &&
            std::fabs(plan.aim_px.y - 191.0f) < 0.5f,
        "same-target reacquire must preserve its fresh position");

    plan = coordinator.update(
        frame(11, 1.10, 1, 244.0f, 208.0f),
        firing_ads_intent(1.10), 1.10);
    require_true(
        std::fabs(plan.aim_px.x - 244.0f) < 0.5f &&
            std::fabs(plan.aim_px.y - 208.0f) < 0.5f,
        "reacquire recovery must not emit a counter-pulse");
}

void test_ads_fire_impulse_uses_same_robust_observation_update() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 1.0);
    auto plan = coordinator.update(
        frame(1, 1.00, 1, 320.0f, 208.0f),
        ads_intent(1.00), 1.00);
    plan = coordinator.update(
        frame(2, 1.01, 1, 320.0f, 208.0f),
        ads_intent(1.01), 1.01);
    require_true(
        plan.mode == pipeline_contract::ControlMode::AdsAcquire,
        "fixture must remain in ADS acquisition");
    controller_native::TargetControlFeedback feedback{};
    feedback.firing_recently = true;
    plan = coordinator.update(
        frame(3, 1.02, 1, 340.0f, 188.0f),
        ads_intent(1.02), 1.02, feedback);
    require_true(
        std::fabs(plan.aim_px.x - 340.0f) < 0.5f &&
            std::fabs(plan.aim_px.y - 188.0f) < 0.5f,
        "ADS must preserve a fresh position while firing");
    require_true(
        std::hypot(plan.velocity_px_per_sec.x, plan.velocity_px_per_sec.y) <=
            80.0f,
        "ADS firing velocity admission must remain bounded");
}

void test_firing_fresh_position_remains_authoritative_across_center() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1, 2.0);

    (void)coordinator.update(
        frame(1, 2.000, 104, 251.0f, 208.0f),
        firing_ads_intent(2.000),
        2.000);

    pipeline_contract::TargetPlan plan{};
    const float observations[] = {232.0f, 224.0f, 212.0f};
    for (std::uint64_t index = 0; index < 3; ++index) {
        const double time = 2.010 + static_cast<double>(index) * 0.010;
        plan = coordinator.update(
            frame(index + 2, time, 104, observations[index], 208.0f),
            firing_ads_intent(time),
            time);
        require_true(
            plan.error_px.x < 0.0f,
            "fresh firing position must cross the center immediately");
        require_true(
            std::fabs(plan.aim_px.x - observations[index]) < 0.01f,
            "fresh firing position must remain authoritative");
    }

    require_true(
        std::fabs(plan.velocity_px_per_sec.x) <= 80.0f,
        "firing velocity admission must remain bounded independently of position");
}

void test_ads_waiting_and_admission_use_separate_clocks() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_max_acquisition_ms = 220.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(9, 10.000);

    auto plan = coordinator.update(
        empty_fresh_frame(1, 10.050),
        ads_intent(10.050),
        10.050);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget,
        "no-target ADS must remain ArmedWaitingForTarget");
    require_true(plan.target_acquisition_id == 0 &&
                     plan.acquisition_elapsed_ms == 0.0f,
                 "no-target ADS must not start the target acquisition timer");
    require_true(std::fabs(plan.ads_epoch_elapsed_ms - 50.0f) < 0.1f,
                 "physical ADS epoch time must remain independently observable");

    plan = coordinator.update(
        frame(2, 10.182, 91, 340.0f, 208.0f),
        ads_intent(10.182),
        10.182);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringNominal,
        "first eligible target must enter nominal acquisition");
    require_true(plan.target_acquisition_id != 0 &&
                     plan.acquisition_elapsed_ms < 0.1f,
                 "target admission must start a fresh nominal clock");
    require_true(std::fabs(plan.ads_epoch_elapsed_ms - 182.0f) < 0.1f,
                 "late admission must retain physical epoch elapsed time");

    plan = coordinator.update(
        frame(3, 10.230, 91, 340.0f, 208.0f),
        ads_intent(10.230),
        10.230);
    require_true(plan.acquisition_elapsed_ms > 47.0f &&
                     plan.acquisition_elapsed_ms < 49.0f,
                 "nominal elapsed time must be measured from target admission");
    require_true(plan.ads_activation_radius_px > 0.0f &&
                     plan.ads_activation_radius_px !=
                         config.ads_nominal_acquisition_ms,
                 "spatial activation radius and time budget must stay separate");
}

void test_ads_nominal_can_extend_then_settle_hands_off_early() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_max_acquisition_ms = 220.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(10, 11.000);

    (void)coordinator.update(
        frame(1, 11.000, 1, 350.0f, 208.0f),
        ads_intent(11.000),
        11.000);
    auto plan = coordinator.update(
        frame(2, 11.135, 1, 230.0f, 208.0f),
        ads_intent(11.135),
        11.135);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::CenterCross,
        "a center-cross at nominal must hand off instead of extending");

    auto extending_config = config;
    extending_config.settle_frames = 1;
    extending_config.handoff_prediction_seconds = 0.0f;
    extending_config.handoff_max_closing_velocity_px_per_sec = 2000.0f;
    controller_native::TargetCoordinator extending(extending_config);
    extending.begin_ads_epoch(11, 12.000);
    (void)extending.update(
        frame(1, 12.000, 1, 350.0f, 208.0f),
        ads_intent(12.000),
        12.000);
    plan = extending.update(
        frame(2, 12.135, 1, 330.0f, 208.0f),
        ads_intent(12.135),
        12.135);
    require_true(
        plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "continued radial closing at nominal must enter Extended");

    plan = extending.update(
        frame(3, 12.170, 1, 242.0f, 208.0f),
        ads_intent(12.170),
        12.170);
    require_true(
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
        plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::Settled,
        "settle after nominal extension must complete before the hard ceiling");
}

void test_ads_moving_away_and_unavailable_replacement_identity_cannot_reset_clock() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_max_acquisition_ms = 220.0f;

    controller_native::TargetCoordinator moving_away(config);
    moving_away.begin_ads_epoch(12, 13.000);
    (void)moving_away.update(
        frame(1, 13.000, 1, 300.0f, 208.0f),
        ads_intent(13.000),
        13.000);
    auto plan = moving_away.update(
        frame(2, 13.135, 1, 320.0f, 208.0f),
        ads_intent(13.135),
        13.135);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "one moving-away sample must not hard-stop an otherwise visible acquisition");

    controller_native::TargetCoordinator zero_velocity(config);
    zero_velocity.begin_ads_epoch(121, 13.000);
    (void)zero_velocity.update(
        frame(1, 13.000, 1, 300.0f, 208.0f),
        ads_intent(13.000),
        13.000);
    plan = zero_velocity.update(
        frame(2, 13.135, 1, 300.0f, 208.0f),
        ads_intent(13.135),
        13.135);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "one zero-closing sample must not hard-stop an otherwise visible acquisition");

    controller_native::TargetCoordinator switched(config);
    switched.begin_ads_epoch(13, 14.000);
    (void)switched.update(
        frame(1, 14.000, 1, 300.0f, 208.0f),
        ads_intent(14.000),
        14.000);
    plan = switched.update(
        frame(2, 14.050, 2, 295.0f, 208.0f),
        ads_intent(14.050),
        14.050);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringNominal,
        "a target switch before nominal must retain, not reset, the timer");
    const auto acquisition_id = plan.target_acquisition_id;
    plan = switched.update(
        frame(3, 14.135, 2, 290.0f, 208.0f),
        ads_intent(14.135),
        14.135);
    require_true(
        plan.target_acquisition_id == acquisition_id &&
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "frame-local observation ids must not fabricate a target switch or reset the clock");
}

void test_selector_replacement_is_a_new_identity_without_rearming_ads() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.handoff_prediction_seconds = 0.0f;
    config.handoff_max_closing_velocity_px_per_sec = 2000.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(120, 20.000);

    auto first = frame(1, 20.000, 11, 350.0f, 208.0f);
    first.selector_identity_protocol = true;
    first.selector_target_generation = 1;
    auto plan = coordinator.update(first, ads_intent(20.000), 20.000);
    const auto original_target_id = plan.target_id;
    const auto acquisition_id = plan.target_acquisition_id;
    require_true(original_target_id != 0 && acquisition_id != 0,
                 "replacement fixture must establish target and acquisition identity");

    auto moving_old = frame(2, 20.020, 12, 340.0f, 208.0f);
    moving_old.selector_identity_protocol = true;
    moving_old.selector_target_generation = 1;
    plan = coordinator.update(moving_old, ads_intent(20.020), 20.020);
    require_true(std::fabs(plan.velocity_px_per_sec.x) > 0.1f,
                 "replacement fixture must establish old target velocity state");

    auto replacement = frame(3, 20.040, 99, 230.0f, 208.0f);
    replacement.selector_identity_protocol = true;
    replacement.selector_target_generation = 2;
    replacement.selector_target_changed = true;
    controller_native::TargetControlFeedback old_capture_work{};
    old_capture_work.has_delivered_camera_work_since_capture = true;
    old_capture_work.delivered_camera_work_since_capture_px = {30.0f, 0.0f};
    old_capture_work.remaining_work_confidence = 0.8f;
    plan = coordinator.update(
        replacement, ads_intent(20.040), 20.040, old_capture_work);

    require_true(plan.selector_target_changed,
                 "coordinator must publish the selector replacement boundary");
    require_true(plan.target_id != original_target_id,
                 "confirmed selector replacement must publish a new persistent target id");
    require_true(plan.target_acquisition_id == acquisition_id &&
                     plan.physical_ads_epoch == 120 &&
                     plan.acquisition_elapsed_ms > 39.0f &&
                     plan.acquisition_elapsed_ms < 41.0f,
                 "replacement must preserve the physical ADS epoch and acquisition clock");
    require_true(std::fabs(plan.aim_px.x - 230.0f) < 0.01f,
                 "replacement position must ignore delivered work from the previous person");
    require_true(std::fabs(plan.velocity_px_per_sec.x) < 0.01f &&
                     std::fabs(plan.velocity_px_per_sec.y) < 0.01f,
                 "replacement must reset target-owned velocity state");
    require_true(!plan.remaining_work_valid &&
                     std::fabs(plan.delivered_camera_motion_since_capture_px.x) < 0.01f,
                 "replacement must not carry remaining or delivered work across identities");
}

void test_selector_replacement_radius_requires_generation_and_preferred_signal() {
    controller_native::TargetCoordinatorConfig config{};
    config.association_radius_px = 80.0f;

    const auto establish_ads_target = [&](controller_native::TargetCoordinator& coordinator,
                                          std::uint64_t epoch,
                                          double start_time) {
        coordinator.begin_ads_epoch(epoch, start_time);
        auto first = frame(1, start_time, 11, 350.0f, 208.0f);
        first.selector_identity_protocol = true;
        first.selector_target_generation = 1;
        auto plan = coordinator.update(
            first, ads_intent(start_time), start_time);
        auto old_motion = frame(2, start_time + 0.020, 12, 340.0f, 208.0f);
        old_motion.selector_identity_protocol = true;
        old_motion.selector_target_generation = 1;
        return std::pair<std::uint64_t, std::uint64_t>{
            plan.target_id,
            coordinator.update(
                old_motion,
                ads_intent(start_time + 0.020),
                start_time + 0.020).target_acquisition_id};
    };

    const auto far_candidate = [&](std::uint64_t frame_id,
                                   double time,
                                   std::uint64_t source_id,
                                   std::uint64_t preferred_source_id,
                                   std::uint64_t generation,
                                   bool changed) {
        auto value = frame(frame_id, time, source_id, 230.0f, 208.0f, 0.8f);
        value.selector_identity_protocol = true;
        value.preferred_source_id = preferred_source_id;
        value.selector_target_generation = generation;
        value.selector_target_changed = changed;
        return value;
    };

    controller_native::TargetCoordinator same_generation(config);
    const auto same_target = establish_ads_target(same_generation, 201, 30.000);
    auto plan = same_generation.update(
        far_candidate(3, 30.040, 99, 99, 1, false),
        ads_intent(30.040),
        30.040);
    require_true(
        plan.target_id == same_target.first &&
            !plan.selector_target_changed &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAssociationRadius,
        "same-generation far candidate must remain an association reject");

    controller_native::TargetCoordinator spurious_changed(config);
    const auto spurious_target = establish_ads_target(spurious_changed, 202, 31.000);
    plan = spurious_changed.update(
        far_candidate(3, 31.040, 99, 99, 1, true),
        ads_intent(31.040),
        31.040);
    require_true(
        plan.target_id == spurious_target.first &&
            !plan.selector_target_changed &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAssociationRadius,
        "spurious changed pulse must not bypass same-generation association");

    controller_native::TargetCoordinator missed_pulse(config);
    const auto missed_target = establish_ads_target(missed_pulse, 203, 32.000);
    plan = missed_pulse.update(
        far_candidate(3, 32.040, 99, 99, 2, false),
        ads_intent(32.040),
        32.040);
    require_true(
        plan.target_id != missed_target.first &&
            plan.selector_target_changed &&
            std::fabs(plan.aim_px.x - 230.0f) < 0.01f &&
            plan.target_acquisition_id == missed_target.second,
        "generation change must accept a preferred far replacement even when its pulse was missed");

    controller_native::TargetCoordinator nonpreferred(config);
    const auto nonpreferred_target = establish_ads_target(nonpreferred, 204, 33.000);
    plan = nonpreferred.update(
        far_candidate(3, 33.040, 98, 99, 2, true),
        ads_intent(33.040),
        33.040);
    require_true(
        plan.target_id == nonpreferred_target.first &&
            !plan.selector_target_changed &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAssociationRadius,
        "a far non-preferred candidate must not use the replacement bypass");

    controller_native::TargetCoordinator activation_guard(config);
    const auto activation_target = establish_ads_target(activation_guard, 208, 33.500);
    activation_guard.begin_ads_epoch(209, 33.560);
    auto outside_activation = far_candidate(3, 33.560, 99, 99, 2, false);
    outside_activation.candidates[0].aim_px.x = 430.0f;
    plan = activation_guard.update(
        outside_activation, ads_intent(33.560), 33.560);
    require_true(
        plan.target_id == activation_target.first &&
            !plan.ads_plan_admitted &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius,
        "generation replacement bypass must not widen a new ADS screen-radius admission");

    controller_native::TargetCoordinator manual(config);
    auto manual_first = frame(1, 34.000, 11, 350.0f, 208.0f);
    manual_first.selector_identity_protocol = true;
    manual_first.selector_target_generation = 1;
    auto manual_plan = manual.update(manual_first, {}, 34.000);
    const auto manual_target_a = manual_plan.target_id;
    auto manual_old = frame(2, 34.020, 12, 340.0f, 208.0f);
    manual_old.selector_identity_protocol = true;
    manual_old.selector_target_generation = 1;
    (void)manual.update(manual_old, {}, 34.020);
    manual_plan = manual.update(
        far_candidate(3, 34.040, 99, 99, 2, false),
        {},
        34.040);
    const auto manual_target_b = manual_plan.target_id;
    require_true(
        manual_target_b != manual_target_a && manual_plan.selector_target_changed &&
            std::fabs(manual_plan.velocity_px_per_sec.x) < 0.01f,
        "confirmed manual-mode replacement must cross the identity boundary");

    manual.begin_ads_epoch(205, 34.060);
    manual_plan = manual.update(
        far_candidate(4, 34.060, 99, 99, 2, false),
        ads_intent(34.060),
        34.060);
    require_true(
        manual_plan.target_id == manual_target_b &&
            manual_plan.ads_plan_admitted &&
            !manual_plan.selector_target_changed &&
            manual_plan.ads_decision_reason ==
                pipeline_contract::AdsDecisionReason::Admitted,
        "starting ADS on the current generation must admit once without a target switch");

    controller_native::TargetCoordinator rising(config);
    auto rising_first = frame(1, 35.000, 11, 350.0f, 208.0f);
    rising_first.selector_identity_protocol = true;
    rising_first.selector_target_generation = 1;
    const auto rising_a = rising.update(rising_first, {}, 35.000).target_id;
    rising.begin_ads_epoch(206, 35.020);
    plan = rising.update(
        far_candidate(2, 35.020, 99, 99, 2, false),
        ads_intent(35.020),
        35.020);
    require_true(
        plan.target_id != rising_a && plan.selector_target_changed &&
            plan.ads_plan_admitted &&
            plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::Admitted,
        "a replacement at LT rising must reset identity while admitting exactly once");
}

void test_generation_change_gives_preferred_replacement_frame_ownership() {
    controller_native::TargetCoordinatorConfig config{};
    config.association_radius_px = 80.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(207, 36.000);

    auto first = frame(1, 36.000, 11, 350.0f, 208.0f);
    first.selector_identity_protocol = true;
    first.selector_target_generation = 1;
    const auto first_plan = coordinator.update(
        first, ads_intent(36.000), 36.000);
    const auto old_target_id = first_plan.target_id;

    auto old_motion = frame(2, 36.020, 12, 340.0f, 208.0f);
    old_motion.selector_identity_protocol = true;
    old_motion.selector_target_generation = 1;
    (void)coordinator.update(old_motion, ads_intent(36.020), 36.020);

    auto mixed = frame(3, 36.040, 12, 330.0f, 208.0f);
    mixed.selector_identity_protocol = true;
    mixed.preferred_source_id = 99;
    mixed.selector_target_generation = 2;
    mixed.selector_target_changed = false;
    mixed.count = 2;
    mixed.candidates[1] = mixed.candidates[0];
    mixed.candidates[1].source_id = 99;
    mixed.candidates[1].aim_px = {230.0f, 208.0f};
    mixed.candidates[1].reliability = 0.8f;
    const auto plan = coordinator.update(
        mixed, ads_intent(36.040), 36.040);

    require_true(
        plan.target_id != old_target_id &&
            plan.selector_target_changed &&
            plan.source_observation_id == 99 &&
            std::fabs(plan.aim_px.x - 230.0f) < 0.01f,
        "durable generation change must give frame ownership to the preferred far replacement");
}

void test_selector_protocol_owns_preferred_without_nonpreferred_fallback() {
    controller_native::TargetCoordinatorConfig config{};
    config.association_radius_px = 80.0f;

    const auto establish = [&](controller_native::TargetCoordinator& coordinator,
                               std::uint64_t epoch,
                               double start_time) {
        coordinator.begin_ads_epoch(epoch, start_time);
        auto first = frame(1, start_time, 11, 350.0f, 208.0f);
        first.selector_identity_protocol = true;
        first.selector_target_generation = 1;
        const auto first_plan = coordinator.update(
            first, ads_intent(start_time), start_time);
        auto old_motion = frame(2, start_time + 0.020, 12, 340.0f, 208.0f);
        old_motion.selector_identity_protocol = true;
        old_motion.selector_target_generation = 1;
        (void)coordinator.update(
            old_motion,
            ads_intent(start_time + 0.020),
            start_time + 0.020);
        return first_plan.target_id;
    };

    controller_native::TargetCoordinator preferred_owner(config);
    const auto original_target = establish(preferred_owner, 210, 37.000);
    auto same_generation = frame(3, 37.040, 12, 331.0f, 208.0f);
    same_generation.selector_identity_protocol = true;
    same_generation.preferred_source_id = 99;
    same_generation.selector_target_generation = 1;
    same_generation.count = 2;
    same_generation.candidates[1] = same_generation.candidates[0];
    same_generation.candidates[1].source_id = 99;
    same_generation.candidates[1].aim_px = {360.0f, 208.0f};
    same_generation.candidates[1].reliability = 0.8f;
    auto plan = preferred_owner.update(
        same_generation, ads_intent(37.040), 37.040);
    require_true(
        plan.target_id == original_target &&
            plan.source_observation_id == 99 &&
            std::fabs(plan.aim_px.x - 360.0f) < 0.01f,
        "same-generation protocol frame must consume preferred A instead of nearby nonpreferred B");

    controller_native::TargetCoordinator missing_preferred(config);
    const auto missing_target = establish(missing_preferred, 211, 38.000);
    auto missing = frame(3, 38.040, 12, 331.0f, 208.0f);
    missing.selector_identity_protocol = true;
    missing.preferred_source_id = 99;
    missing.selector_target_generation = 1;
    plan = missing_preferred.update(missing, ads_intent(38.040), 38.040);
    require_true(
        plan.target_id == missing_target &&
            plan.source_observation_id == 0 &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::Rejected,
        "missing preferred detection must reject/coast rather than select a nonpreferred candidate");

    controller_native::TargetCoordinator low_preferred(config);
    const auto low_target = establish(low_preferred, 212, 39.000);
    auto low = frame(3, 39.040, 12, 331.0f, 208.0f);
    low.selector_identity_protocol = true;
    low.preferred_source_id = 99;
    low.selector_target_generation = 1;
    low.count = 2;
    low.candidates[1] = low.candidates[0];
    low.candidates[1].source_id = 99;
    low.candidates[1].reliability = 0.0f;
    plan = low_preferred.update(low, ads_intent(39.040), 39.040);
    require_true(
        plan.target_id == low_target && plan.source_observation_id == 0 &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::Rejected,
        "unreliable preferred detection must not fall back to a nonpreferred candidate");

    controller_native::TargetCoordinator activation_guard(config);
    const auto activation_target = establish(activation_guard, 213, 40.000);
    activation_guard.begin_ads_epoch(214, 40.060);
    auto outside_preferred = frame(3, 40.060, 12, 245.0f, 208.0f);
    outside_preferred.selector_identity_protocol = true;
    outside_preferred.preferred_source_id = 99;
    outside_preferred.selector_target_generation = 2;
    outside_preferred.count = 2;
    outside_preferred.candidates[1] = outside_preferred.candidates[0];
    outside_preferred.candidates[1].source_id = 99;
    outside_preferred.candidates[1].aim_px = {430.0f, 208.0f};
    plan = activation_guard.update(
        outside_preferred, ads_intent(40.060), 40.060);
    require_true(
        plan.target_id == activation_target && !plan.ads_plan_admitted &&
            plan.source_observation_id == 0 &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius,
        "new ADS epoch must not substitute a nearby nonpreferred candidate for an out-of-radius preferred target");

    controller_native::TargetCoordinator legacy(config);
    auto legacy_frame = frame(1, 41.000, 17, 300.0f, 208.0f);
    legacy_frame.selector_identity_protocol = false;
    legacy_frame.preferred_source_id = 0;
    plan = legacy.update(legacy_frame, {}, 41.000);
    require_true(
        plan.target_id != 0,
        "protocol-unavailable legacy path must retain its association chooser");
}

void test_ads_manual_escape_and_reject_reasons_are_stable() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    controller_native::TargetCoordinator manual(config);
    manual.begin_ads_epoch(14, 15.000);
    (void)manual.update(
        frame(1, 15.000, 1, 350.0f, 208.0f),
        ads_intent(15.000),
        15.000);
    (void)manual.update(
        frame(2, 15.135, 1, 330.0f, 208.0f),
        ads_intent(15.135),
        15.135);
    auto intent = ads_intent(15.150);
    intent.filtered_right.x = 0.5f;
    intent.right_x.confidence = 1.0f;
    controller_native::TargetControlFeedback feedback{};
    feedback.fusion_manual_escape = true;
    auto plan = manual.update(
        frame(3, 15.150, 1, 325.0f, 208.0f),
        intent,
        15.150,
        feedback);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::ManualEscape,
        "deliberate manual escape must complete acquisition immediately");

    controller_native::TargetCoordinator selector(config);
    auto selected = frame(1, 16.000, 7, 300.0f, 208.0f);
    selected.selector_identity_protocol = true;
    selected.preferred_source_id = 0;
    plan = selector.update(selected, ads_intent(16.000), 16.000);
    require_true(
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::SelectorNoSelection,
        "selector rejection must use the stable enum reason");

    controller_native::TargetCoordinator low_reliability(config);
    auto low = empty_fresh_frame(1, 17.000);
    low.rejected_low_reliability_count = 1;
    plan = low_reliability.update(
        low, ads_intent(17.000), 17.000);
    require_true(
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::LowReliability,
        "low-reliability rejection must use the stable enum reason");

    controller_native::TargetCoordinator stale(config);
    (void)stale.update(
        frame(1, 18.000, 1, 300.0f, 208.0f),
        ads_intent(18.000),
        18.000);
    auto stale_frame = frame(2, 17.800, 1, 300.0f, 208.0f);
    plan = stale.update(stale_frame, ads_intent(18.000), 18.000);
    require_true(
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::StaleCapture,
        "stale capture rejection must use the stable enum reason");

    stale_frame.frame_id = 1;
    stale_frame.source_time_seconds = 18.000;
    stale_frame.publish_time_seconds = 18.000;
    plan = stale.update(stale_frame, ads_intent(18.010), 18.010);
    require_true(
        plan.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::DuplicateFrame,
        "duplicate frame rejection must use the stable enum reason");
}

void test_new_ads_epoch_requires_center_admission_even_with_existing_track() {
    controller_native::TargetCoordinator coordinator;
    auto hipfire = pipeline_contract::IntentState{};
    hipfire.sample_time_seconds = 30.000;
    (void)coordinator.update(
        frame(1, 30.000, 21, 300.0f, 208.0f), hipfire, 30.000);

    coordinator.begin_ads_epoch(30, 30.010);
    auto plan = coordinator.update(
        frame(2, 30.020, 22, 430.0f, 208.0f),
        ads_intent(30.020),
        30.020);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget &&
            plan.target_acquisition_id == 0 &&
            plan.ads_decision_reason ==
                pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius &&
            !plan.ads_plan_admitted &&
            !plan.ads_acquisition_active,
        "an existing far track must not bypass the new physical ADS radius");
    require_true(
        plan.mode == pipeline_contract::ControlMode::Manual &&
            plan.aim_authority == 0.0f,
        "outside-radius ADS candidates must not produce AI authority");

    plan = coordinator.update(
        frame(3, 30.030, 23, 340.0f, 208.0f),
        ads_intent(30.030),
        30.030);
    require_true(
        plan.ads_plan_admitted && plan.ads_acquisition_active &&
            plan.target_acquisition_id != 0 &&
            plan.ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringNominal,
        "the same epoch must start its nominal clock only after center admission");
}

void test_radial_crossing_ignores_orthogonal_noise_but_stops_true_crossing() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.handoff_prediction_seconds = 0.0f;
    config.handoff_max_closing_velocity_px_per_sec = 2000.0f;

    controller_native::TargetCoordinator noise(config);
    noise.begin_ads_epoch(31, 31.000);
    (void)noise.update(
        frame(1, 31.000, 31, 270.0f, 206.0f),
        ads_intent(31.000),
        31.000);
    auto plan = noise.update(
        frame(2, 31.135, 32, 260.0f, 210.0f),
        ads_intent(31.135),
        31.135);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "small Y sign noise while X remains far must not be center crossing");

    controller_native::TargetCoordinator radial(config);
    radial.begin_ads_epoch(32, 32.000);
    (void)radial.update(
        frame(1, 32.000, 33, 300.0f, 208.0f),
        ads_intent(32.000),
        32.000);
    plan = radial.update(
        frame(2, 32.135, 34, 220.0f, 208.0f),
        ads_intent(32.135),
        32.135);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
            plan.ads_decision_reason ==
                pipeline_contract::AdsDecisionReason::CenterCross,
        "a true radial reversal across the center must stop acquisition");
}

void test_same_direction_manual_magnitude_does_not_end_extended() {
    for (const float magnitude : {0.10f, 0.50f}) {
        controller_native::TargetCoordinatorConfig config{};
        config.settle_frames = 2;
        config.handoff_prediction_seconds = 0.0f;
        config.handoff_max_closing_velocity_px_per_sec = 2000.0f;
        controller_native::TargetCoordinator coordinator(config);
        coordinator.begin_ads_epoch(
            static_cast<std::uint64_t>(40 + magnitude * 10.0f), 40.000);
        (void)coordinator.update(
            frame(1, 40.000, 41, 350.0f, 208.0f),
            ads_intent(40.000),
            40.000);
        (void)coordinator.update(
            frame(2, 40.135, 42, 330.0f, 208.0f),
            ads_intent(40.135),
            40.135);
        auto intent = ads_intent(40.150);
        intent.filtered_right.x = magnitude;
        intent.right_x.confidence = 1.0f;
        const auto plan = coordinator.update(
            frame(3, 40.150, 43, 325.0f, 208.0f),
            intent,
            40.150);
        require_true(
            plan.ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "same-direction manual cooperation must not be a coordinator escape");
    }
}

void test_nominal_boundary_waits_for_fresh_authoritative_decision() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_max_acquisition_ms = 220.0f;
    config.handoff_prediction_seconds = 0.0f;
    config.handoff_max_closing_velocity_px_per_sec = 2000.0f;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(50, 50.000);

    (void)coordinator.update(
        frame(1, 50.000, 50, 350.0f, 208.0f),
        ads_intent(50.000),
        50.000);
    auto plan = coordinator.update(
        frame(2, 50.134, 50, 330.0f, 208.0f),
        ads_intent(50.134),
        50.134);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringNominal,
        "the pre-boundary fresh frame must remain nominal");

    for (const double time : {50.135, 50.136}) {
        plan = coordinator.update(
            replay_tick(time),
            ads_intent(time),
            time);
        require_true(
            plan.ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringNominal &&
                plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                plan.target_acquisition_id != 0 &&
                plan.ads_acquisition_state !=
                    pipeline_contract::AdsAcquisitionState::Consumed,
            "a replay tick at nominal must not fabricate loss or consume ADS");
    }

    plan = coordinator.update(
        frame(3, 50.140, 50, 325.0f, 208.0f),
        ads_intent(50.140),
        50.140);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended,
        "the next fresh helpful frame must enter Extended");

    plan = coordinator.update(
        replay_tick(50.145),
        ads_intent(50.145),
        50.145);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::AcquiringExtended &&
            plan.mode == pipeline_contract::ControlMode::AdsAcquire,
        "replay ticks must preserve Extended after a fresh extension decision");

    plan = coordinator.update(
        replay_tick(50.220),
        ads_intent(50.220),
        50.220);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
            plan.ads_decision_reason ==
                pipeline_contract::AdsDecisionReason::AcquisitionCeiling &&
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "the controller-clock hard ceiling must still complete on replay");
}

void test_fuser_manual_escape_hands_off_during_nominal() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(51, 51.000);
    (void)coordinator.update(
        frame(1, 51.000, 51, 350.0f, 208.0f),
        ads_intent(51.000),
        51.000);

    controller_native::TargetControlFeedback feedback{};
    feedback.fusion_manual_escape = true;
    auto intent = ads_intent(51.010);
    intent.filtered_right.x = 0.10f;
    intent.right_x.confidence = 1.0f;
    const auto plan = coordinator.update(
        replay_tick(51.010), intent, 51.010, feedback);
    require_true(
        plan.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
            plan.ads_decision_reason ==
                pipeline_contract::AdsDecisionReason::ManualEscape &&
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
        "fuser-confirmed deliberate escape must hand off during nominal");
}

void test_replay_does_not_overwrite_source_decision_reason() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    coordinator.begin_ads_epoch(52, 52.000);

    auto plan = coordinator.update(
        frame(1, 52.000, 52, 350.0f, 208.0f),
        ads_intent(52.000),
        52.000);
    require_true(
        plan.ads_plan_admitted,
        "fresh admission must publish plan_admitted=true");
    require_true(
        plan.source_decision_available &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::Admitted &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::Admitted,
        "fresh admission must publish a separate source decision contract");
    require_true(
        plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::Admitted,
        "fresh admission must publish decision reason Admitted");

    plan = coordinator.update(
        replay_tick(52.001),
        ads_intent(52.001),
        52.001);
    require_true(
        plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::Admitted,
        "a replay tick must preserve the last source decision reason");
    require_true(
        plan.ads_decision_reason !=
                pipeline_contract::AdsDecisionReason::OutsideAssociationRadius &&
            plan.ads_decision_reason !=
                pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius,
        "a replay tick must not emit a false association or activation reject");
    require_true(
        !plan.source_decision_available &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::NoDecision &&
            plan.source_decision_reason == pipeline_contract::AdsDecisionReason::None,
        "replay must carry no fresh source decision");

    plan = coordinator.update(
        frame(2, 52.010, 52, 345.0f, 208.0f),
        ads_intent(52.010),
        52.010);
    require_true(
        plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::Admitted &&
            plan.ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringNominal,
        "fresh accepted continuation must not carry a replay reject reason");
    require_true(
        plan.source_decision_available &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::AcceptedContinuation &&
            plan.source_decision_reason == pipeline_contract::AdsDecisionReason::None,
        "fresh accepted continuation must have its own non-reject outcome");
}

void test_source_decision_reports_fresh_reject_and_consumed_separately() {
    controller_native::TargetCoordinator coordinator;
    auto plan = coordinator.update(
        frame(1, 53.000, 53, 350.0f, 208.0f), ads_intent(53.000), 53.000);
    require_true(plan.source_decision_outcome ==
                     pipeline_contract::SourceDecisionOutcome::Admitted,
                 "setup must admit a source frame");

    auto duplicate = frame(1, 53.000, 53, 350.0f, 208.0f);
    plan = coordinator.update(duplicate, ads_intent(53.010), 53.010);
    require_true(
        plan.source_decision_available &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::Rejected &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::DuplicateFrame,
        "duplicate fresh input must be a stable rejected source decision");

    auto stale = frame(2, 52.900, 53, 350.0f, 208.0f);
    plan = coordinator.update(stale, ads_intent(53.020), 53.020);
    require_true(
        plan.source_decision_available &&
            plan.source_decision_outcome ==
                pipeline_contract::SourceDecisionOutcome::Rejected &&
            plan.source_decision_reason ==
                pipeline_contract::AdsDecisionReason::StaleCapture,
        "older capture time must be a stable stale source decision");
}

}  // namespace

int main() {
    try {
        test_single_owner_coasts_and_reacquires_same_identity();
        test_coasting_actuation_lease_retires_before_identity_hold();
        test_36ms_moving_occlusion_reacquires_without_false_stop();
        test_different_target_after_expired_hold_uses_new_admission();
        test_hold_expires_to_safe_manual_plan();
        test_selector_owned_batch_does_not_acquire_unselected_candidate();
        test_motion_labels_jump_then_fall();
        test_jump_cue_adds_causal_vertical_acceleration_projection();
        test_player_motion_oracle_separates_realized_camera_error();
        test_causal_slide_model_separates_state_and_forecast();
        test_player_motion_forecast_only_bridges_between_vision_frames();
        test_selector_replacement_preserves_held_player_motion_state();
        test_selector_replacement_preserves_held_slide_motion_state();
        test_ads_handoff_waits_for_settle();
        test_bodylock_cannot_rearm_ads_within_one_held_epoch();
        test_ads_ownership_ceiling_is_independent_from_arrival_horizon();
        test_late_target_gets_nominal_window_from_admission();
        test_ads_timed_fallback_consumes_snap_without_far_bodylock_pull();
        test_ads_base_range_rejects_small_far_target();
        test_close_target_geometry_expands_ads_initial_range();
        test_close_target_geometry_expands_bodylock_continuation_range();
        test_close_target_geometry_does_not_expand_blind_coast_range();
        test_ads_handoff_rejects_a_predicted_high_speed_crossing();
        test_ads_handoff_accepts_stable_in_radius_capture();
        test_ads_handoff_allows_tangential_target_motion();
        test_bodylock_exit_band_is_independent_from_ads_capture_radius();
        test_moving_target_gets_a_bounded_expanded_capture_set();
        test_control_rate_gaps_do_not_compound_reliability_decay();
        test_control_rate_gaps_preserve_ads_settle_progress();
        test_100hz_motion_stays_finite_at_1000hz_control_rate();
        test_replayed_vision_frame_is_consumed_once();
        test_duplicate_and_backward_frames_cannot_reenter_as_observations();
        test_overage_capture_is_ignored_and_capture_age_is_preserved();
        test_velocity_alpha_is_normalized_to_vision_interval();
        test_bodylock_velocity_obeys_target_acceleration_limit();
        test_bodylock_bounds_impulse_without_delayed_release();
        test_bodylock_reacquire_preserves_fresh_position_without_velocity_impulse();
        test_ads_fire_impulse_uses_same_robust_observation_update();
        test_constant_motion_response_is_cadence_invariant();
        test_left_intent_enters_plan_through_learned_response();
        test_aim_response_feedback_is_separate_from_left_motion_response();
        test_nonfresh_empty_ticks_preserve_observed_fire_plan();
        test_fresh_processed_miss_revokes_fire_immediately();
        test_anonymous_in_radius_hold_preserves_identity_and_fire_request();
        test_vision_fire_authority_is_not_rejected_by_reliability_weight();
        test_ads_waiting_and_admission_use_separate_clocks();
        test_ads_nominal_can_extend_then_settle_hands_off_early();
        test_selector_replacement_is_a_new_identity_without_rearming_ads();
        test_selector_replacement_radius_requires_generation_and_preferred_signal();
        test_generation_change_gives_preferred_replacement_frame_ownership();
        test_selector_protocol_owns_preferred_without_nonpreferred_fallback();
        test_ads_moving_away_and_unavailable_replacement_identity_cannot_reset_clock();
        test_ads_manual_escape_and_reject_reasons_are_stable();
        test_new_ads_epoch_requires_center_admission_even_with_existing_track();
        test_radial_crossing_ignores_orthogonal_noise_but_stops_true_crossing();
        test_same_direction_manual_magnitude_does_not_end_extended();
        test_nominal_boundary_waits_for_fresh_authoritative_decision();
        test_fuser_manual_escape_hands_off_during_nominal();
        test_replay_does_not_overwrite_source_decision_reason();
        test_source_decision_reports_fresh_reject_and_consumed_separately();
        test_delivered_camera_work_is_consumed_during_control_rate_prediction();
        test_fresh_vision_reanchors_to_work_delivered_since_capture();
        test_firing_fresh_position_remains_authoritative_across_center();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetCoordinatorTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
