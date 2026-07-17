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
    for (int i = 1; i <= 8; ++i) {
        const double time = i * 0.02;
        plan = coordinator.update(frame(i + 1, time, 1, 243.0f, 208.0f), ads_intent(time), time);
    }
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "fresh settled frames must hand off to BodyLock");
    plan = coordinator.update(frame(20, 0.20, 1, 260.0f, 208.0f), ads_intent(0.20), 0.20);
    require_true(plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
                 "BodyLock must use a wider exit band than its ADS entry band");
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
    coordinator.begin_ads_epoch(1);
    for (int i = 0; i < 80; ++i) {
        coordinator.observe_control_response({0.5f, -100.0f, true, false});
    }
    auto intent = ads_intent(0.0);
    intent.filtered_left.x = 0.5f;
    intent.left_confidence = 1.0f;
    const auto plan = coordinator.update(frame(1, 0.0, 1, 240.0f, 208.0f), intent, 0.0);
    require_true(plan.response_scale < -190.0f,
                 "plan must carry signed learned response");
    require_true(plan.error_rate_px_per_sec.x < -90.0f,
                 "left intent must affect planned relative motion");
    require_true(plan.horizon[0].error_px.x < 0.0f,
                 "short plan must include left-stick feed-forward");
}

void test_left_input_change_projects_only_until_next_observation() {
    controller_native::TargetCoordinator coordinator;
    coordinator.begin_ads_epoch(1);
    for (int i = 0; i < 80; ++i) {
        coordinator.observe_control_response({0.5f, -100.0f, true, false});
    }
    auto neutral = ads_intent(1.000);
    auto plan = coordinator.update(
        frame(1, 1.000, 1, 280.0f, 208.0f), neutral, 1.000);
    require_true(std::fabs(plan.intent_projection_px.x) < 0.0001f,
                 "fresh observation must start with zero intent projection");

    pipeline_contract::VisionObservationBatch no_new_frame{};
    no_new_frame.frame_width_px = 480.0f;
    no_new_frame.frame_height_px = 416.0f;
    auto moved = ads_intent(1.005);
    moved.filtered_left.x = 0.5f;
    moved.left_confidence = 1.0f;
    plan = coordinator.update(no_new_frame, moved, 1.005);
    require_true(plan.intent_projection_px.x < -0.20f,
                 "new left input must project the learned signed screen response");
    require_true(std::fabs(plan.intent_projection_px.y) < 0.0001f,
                 "left strafe must never project Y");

    plan = coordinator.update(
        frame(2, 1.010, 1, 279.0f, 208.0f), moved, 1.010);
    require_true(std::fabs(plan.intent_projection_px.x) < 0.0001f,
                 "new observation must consume the inter-frame projection");
    plan = coordinator.update(no_new_frame, moved, 1.015);
    require_true(std::fabs(plan.intent_projection_px.x) < 0.0001f,
                 "already-observed sustained input must not be double-counted");

    auto reversed = moved;
    reversed.filtered_left.x = -0.5f;
    plan = coordinator.update(no_new_frame, reversed, 1.019);
    require_true(plan.intent_projection_px.x > 0.20f,
                 "inter-frame left reversal must flip projected response");
}

void test_left_projection_requires_confidence_and_expires_on_long_gap() {
    controller_native::TargetCoordinator low_confidence;
    low_confidence.begin_ads_epoch(1);
    low_confidence.observe_control_response({0.5f, -100.0f, true, false});
    auto neutral = ads_intent(2.000);
    low_confidence.update(frame(1, 2.000, 1, 280.0f, 208.0f), neutral, 2.000);
    pipeline_contract::VisionObservationBatch no_new_frame{};
    no_new_frame.frame_width_px = 480.0f;
    no_new_frame.frame_height_px = 416.0f;
    auto moved = ads_intent(2.005);
    moved.filtered_left.x = 0.5f;
    moved.left_confidence = 1.0f;
    auto plan = low_confidence.update(no_new_frame, moved, 2.005);
    require_true(std::fabs(plan.intent_projection_px.x) < 0.0001f,
                 "low-confidence response must not project left intent");

    controller_native::TargetCoordinator learned;
    learned.begin_ads_epoch(1);
    for (int i = 0; i < 80; ++i) {
        learned.observe_control_response({0.5f, -100.0f, true, false});
    }
    learned.update(frame(1, 3.000, 1, 280.0f, 208.0f), neutral, 3.000);
    plan = learned.update(no_new_frame, moved, 3.020);
    require_true(std::fabs(plan.intent_projection_px.x) < 0.0001f,
                 "projection must expire instead of surviving a real vision gap");
}

}  // namespace

int main() {
    try {
        test_single_owner_coasts_and_reacquires_same_identity();
        test_hold_expires_to_safe_manual_plan();
        test_motion_labels_jump_then_fall();
        test_ads_handoff_waits_for_settle();
        test_control_rate_gaps_do_not_compound_reliability_decay();
        test_control_rate_gaps_preserve_ads_settle_progress();
        test_100hz_motion_stays_finite_at_1000hz_control_rate();
        test_left_intent_enters_plan_through_learned_response();
        test_left_input_change_projects_only_until_next_observation();
        test_left_projection_requires_confidence_and_expires_on_long_gap();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetCoordinatorTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
