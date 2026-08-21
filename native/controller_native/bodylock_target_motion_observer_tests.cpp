#include "bodylock_target_motion_observer.h"
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

controller_native::BodylockTargetMotionObservation observation(
    std::uint64_t target_id,
    double capture_seconds,
    pipeline_contract::Vec2f screen_rate,
    pipeline_contract::Vec2f delivered,
    float response = 500.0f) {
    return {
        target_id,
        capture_seconds,
        1.0f / 180.0f,
        screen_rate,
        delivered,
        response,
        1.0f,
        true,
    };
}

void prime(
    controller_native::BodylockTargetMotionObserver& observer,
    std::uint64_t target_id,
    pipeline_contract::Vec2f screen_rate,
    pipeline_contract::Vec2f delivered,
    double* capture_seconds) {
    for (int index = 0; index < 3; ++index) {
        *capture_seconds += 1.0 / 180.0;
        require_true(observer.update(observation(
            target_id, *capture_seconds, screen_rate, delivered)),
            "valid direct observations must be accepted");
    }
}

void test_camera_work_is_removed_from_stationary_target() {
    controller_native::BodylockTargetMotionObserver observer;
    double capture = 1.0;
    prime(observer, 11, {-200.0f, 100.0f}, {0.4f, 0.2f}, &capture);
    const auto estimate = observer.estimate(11, capture + 0.001);
    require_true(estimate.valid, "two aligned samples must establish an estimate");
    require_true(near(estimate.target_motion_stick.x, 0.0f) &&
                     near(estimate.target_motion_stick.y, 0.0f),
                 "camera-induced screen motion must not become target motion");
}

void test_perfect_tracking_recovers_total_target_command_and_y_sign() {
    controller_native::BodylockTargetMotionObserver observer;
    double capture = 2.0;
    prime(observer, 12, {0.0f, 0.0f}, {0.4f, -0.3f}, &capture);
    const auto estimate = observer.estimate(12, capture + 0.001);
    require_true(estimate.valid, "tracked target estimate must be valid");
    require_true(near(estimate.target_motion_stick.x, 0.4f) &&
                     near(estimate.target_motion_stick.y, -0.3f),
                 "stationary screen geometry must retain the total camera command");
}

void test_speed_step_reaches_total_demand_without_error_accumulation() {
    controller_native::BodylockTargetMotionObserver observer;
    double capture = 3.0;
    prime(observer, 13, {0.0f, 0.0f}, {0.4f, 0.0f}, &capture);

    int latency_frames = -1;
    for (int frame = 1; frame <= 12; ++frame) {
        capture += 1.0 / 180.0;
        observer.update(observation(
            13, capture, {100.0f, 0.0f}, {0.4f, 0.0f}));
        const auto estimate = observer.estimate(13, capture + 0.001);
        if (estimate.valid && estimate.target_motion_stick.x >= 0.57f) {
            latency_frames = frame;
            break;
        }
    }
    require_true(latency_frames > 0 && latency_frames <= 9,
                 "100 px/s speed step must become total demand within 50 ms");
}

void test_deceleration_discharges_faster_than_rise() {
    controller_native::BodylockTargetMotionObserver observer;
    double capture = 4.0;
    prime(observer, 14, {0.0f, 0.0f}, {0.6f, 0.0f}, &capture);
    for (int frame = 0; frame < 5; ++frame) {
        capture += 1.0 / 180.0;
        observer.update(observation(
            14, capture, {-100.0f, 0.0f}, {0.6f, 0.0f}));
    }
    const auto estimate = observer.estimate(14, capture + 0.001);
    require_true(estimate.valid && estimate.target_motion_stick.x <= 0.41f,
                 "target slowdown must not retain a long overshoot tail");
}

void test_target_replacement_requires_new_aligned_evidence() {
    controller_native::BodylockTargetMotionObserver observer;
    double capture = 5.0;
    prime(observer, 15, {0.0f, 0.0f}, {0.6f, 0.0f}, &capture);
    require_true(observer.estimate(15, capture + 0.001).valid,
                 "first target must be established");

    observer.begin_target(16);
    require_true(!observer.estimate(16, capture + 0.001).valid,
                 "replacement must not inherit old target motion");
    capture += 1.0 / 180.0;
    observer.update(observation(
        16, capture, {0.0f, 0.0f}, {0.4f, 0.0f}));
    require_true(!observer.estimate(16, capture + 0.001).valid,
                 "one replacement sample is only observer priming");
}

}  // namespace

void register_bodylock_target_motion_observer_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "camera_work_removed_from_stationary_target", test_camera_work_is_removed_from_stationary_target);
    registry.add_case("BaseBodyLock", "perfect_tracking_recovers_total_command", test_perfect_tracking_recovers_total_target_command_and_y_sign);
    registry.add_case("BaseBodyLock", "speed_step_reaches_total_without_error_debt", test_speed_step_reaches_total_demand_without_error_accumulation);
    registry.add_case("BaseBodyLock", "deceleration_discharges_faster_than_rise", test_deceleration_discharges_faster_than_rise);
    registry.add_case("BaseBodyLock", "replacement_requires_new_aligned_evidence", test_target_replacement_requires_new_aligned_evidence);
}
