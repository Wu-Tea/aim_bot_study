#include "target_coordinator.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

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

void test_motion_labels_jump_then_fall() {
    controller_native::TargetCoordinator coordinator;
    auto intent = ads_intent(0.0);
    coordinator.update(frame(1, 0.00, 1, 240.0f, 220.0f), intent, 0.00);
    coordinator.update(frame(2, 0.02, 1, 240.0f, 210.0f), intent, 0.02);
    auto plan = coordinator.update(frame(3, 0.04, 1, 240.0f, 198.0f), intent, 0.04);
    require_true(plan.motion == pipeline_contract::TargetMotion::Jump,
                 "persistent upward image motion must classify jump");
    coordinator.update(frame(4, 0.06, 1, 240.0f, 202.0f), intent, 0.06);
    plan = coordinator.update(frame(5, 0.08, 1, 240.0f, 214.0f), intent, 0.08);
    require_true(plan.motion == pipeline_contract::TargetMotion::Fall,
                 "persistent downward image motion must classify fall");
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

    plan = coordinator.update(
        frame(2, 1.050, 2, 300.0f, 208.0f), ads_intent(1.050), 1.050);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "held ADS epoch must not rearm snap for a new far target");
}

void test_ads_timeout_does_not_handoff_with_large_residual_error() {
    controller_native::TargetCoordinatorConfig config{};
    config.ads_max_acquisition_ms = 20.0f;
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);

    pipeline_contract::TargetPlan plan{};
    for (int i = 0; i < 8; ++i) {
        const double time = i * 0.010;
        auto observed = frame(i + 1, time, 1, 300.0f, 208.0f);
        observed.candidates[0].normalized_size = 0.6f;
        plan = coordinator.update(observed, ads_intent(time), time);
    }

    require_true(plan.mode == pipeline_contract::ControlMode::AdsAcquire,
                 "ADS timeout must not hand off a large residual error to BodyLock");
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

}  // namespace

int main() {
    try {
        test_single_owner_coasts_and_reacquires_same_identity();
        test_hold_expires_to_safe_manual_plan();
        test_motion_labels_jump_then_fall();
        test_ads_handoff_waits_for_settle();
        test_bodylock_cannot_rearm_ads_within_one_held_epoch();
        test_ads_timeout_does_not_handoff_with_large_residual_error();
        test_ads_handoff_rejects_a_predicted_high_speed_crossing();
        test_ads_handoff_accepts_stable_in_radius_capture();
        test_ads_handoff_allows_tangential_target_motion();
        test_bodylock_exit_band_is_independent_from_ads_capture_radius();
        test_moving_target_gets_a_bounded_expanded_capture_set();
        test_control_rate_gaps_do_not_compound_reliability_decay();
        test_control_rate_gaps_preserve_ads_settle_progress();
        test_100hz_motion_stays_finite_at_1000hz_control_rate();
        test_left_intent_enters_plan_through_learned_response();
        test_aim_response_feedback_is_separate_from_left_motion_response();
        test_nonfresh_empty_ticks_preserve_observed_fire_plan();
        test_fresh_processed_miss_revokes_fire_immediately();
        test_anonymous_in_radius_hold_preserves_identity_and_fire_request();
        test_vision_fire_authority_is_not_rejected_by_reliability_weight();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetCoordinatorTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
