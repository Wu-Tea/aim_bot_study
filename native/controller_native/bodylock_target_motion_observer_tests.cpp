#include "bodylock_target_motion_observer.h"
#include "bodylock_follow_controller.h"
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
    registry.add_case("BaseBodyLock", "motion_acceleration_scales_with_projected_height", [] {
        for (float direction : {-1.f, 1.f}) for (float response : {3000.f, 5000.f}) {
            float speed[2]{};
            for (int size=0;size<2;++size) {
                controller_native::BodylockTargetMotionObserver observer;
                for (int i=1;i<=4;++i) {
                    // A growing Y estimate cannot revoke independently held X.
                    const float height=size ? 16.f : 8.f;
                    observer.update({1,1.+i*.005,.005f,{0,1000.f*direction},{},response,1,true,false,height});
                }
                auto estimate=observer.estimate(1,1.02);
                require_true(estimate.valid && estimate.target_motion_stick.x==0,
                    "vertical prediction must not introduce horizontal work");
                speed[size]=std::fabs(estimate.target_motion_stick.y)*response;
            }
            require_true(speed[0]>0 && near(speed[1],2*speed[0],.001f),
                "same sudden motion on half-height target must grow at half physical acceleration, independent of calibration");
        }
    });
    registry.add_case("BaseBodyLock", "small_target_motion_initialization_is_bounded", [] {
        for (float direction : {-1.f, 1.f}) for (auto curve : {
                controller_native::AimResponseCurveAlgorithm::Linear,
                controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
            controller_native::BodylockTargetMotionObserver observer;
            for (int i=1;i<=2;++i)
                observer.update({1,1.+i*.005,.005f,{0,800.f*direction},{},500,1,true,false,16});
            const auto motion=observer.estimate(1,1.01);
            controller_native::BodylockFollowControllerConfig config;
            config.range_position_response=true;
            config.response_curve.algorithm=curve;
            config.max_force_x=config.max_force_y=1;
            controller_native::BodylockFollowController solver(config);
            pipeline_contract::TargetPlan plan{};
            plan.mode=pipeline_contract::ControlMode::BodyLockFollow;
            plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
            plan.aim_authority=plan.reliability=1;
            plan.response_scale=500;
            plan.position_response_radius_px=16;
            plan.bodylock_target_motion_valid=motion.valid;
            plan.bodylock_target_motion_px_per_sec={0,-motion.target_motion_stick.y*500};
            const auto output=controller_native::forward_aim_response_curve(
                solver.compute(plan), config.response_curve);
            require_true(motion.valid && std::fabs(output.y)<=.08001f,
                "two 4px increments on a 16px target must obey the motion rise budget even at initialization");
        }
    });
    registry.add_case("BaseBodyLock", "high_response_motion", [] {
        for (float gain : {8000.f, 25000.f}) for (int seed : {7, 81, 923, 6503}) {
            controller_native::BodylockTargetMotionObserver observer;
            for (int i=0; i<120; ++i) {
                const double capture=1.+i*.005;
                require_true(observer.update(observation(1,capture,{-gain*.4f,gain*.2f},{.4f,.2f},gain)),
                    "high response remains valid motion evidence");
            }
            const auto motion=observer.estimate(1,1.+119*.005);
            require_true(motion.valid && std::hypot(motion.target_motion_stick.x,motion.target_motion_stick.y)<1e-4f,
                "high gain camera motion must not become false target motion");
        }
    });
    registry.add_case("BaseBodyLock", "unequal_capture_intervals_cancel_before_saturation", [] {
        for(float sign:{-1.f,1.f}) {
            controller_native::BodylockTargetMotionObserver observer;
            double now=1;
            for(int i=0;i<800;++i) {
                const float dt=i%2 ? .017f : .008f;
                const float dx=(i%2 ? -20.f : 20.f)*sign;
                now+=dt;
                observer.update({1,now,dt,{dx/dt,0},{},1400,1,true});
                if(i>2) require_true(std::abs(observer.estimate(1,now).target_motion_stick.x)<.00001f,
                    "zero-net displacement must not acquire a directional bias from clipping");
            }
            observer.reset();
            for(int i=1;i<=100;++i) observer.update({1,2.+i*.01,.01f,{28000*sign,0},{},1400,1,true});
            require_true(std::abs(observer.estimate(1,3.).target_motion_stick.x)<=1.25001f,
                "integrated motion still obeys the existing demand cap");
        }
    });
    registry.add_case("BaseBodyLock", "motion_noise_window_keeps_wall_time_at_220hz", [] {
        for(int hz:{180,220,250}){
            controller_native::BodylockTargetMotionObserver observer;
            const float dt=1.f/hz;
            for(int i=1;i<=100;++i){
                const float interval_mid=(i-.5f)*dt;
                const float motion=.1f+.3f*interval_mid;
                observer.update({1,1.+double(i)/hz,dt,{motion*1600,0},{},1600,1,true});
            }
            const auto estimate=observer.estimate(1,1.+100./hz);
            const float expected=.1f+.3f*(100.f/hz-.025f/2);
            require_true(estimate.valid && std::fabs(estimate.target_motion_stick.x-expected)<.00012f,
                "increased observation rate must not shorten the25ms noise integration horizon");
        }
    });
    registry.add_case("BaseBodyLock", "capture_displacement_noise_cancels_before_velocity", [] {
        controller_native::BodylockTargetMotionObserver observer;
        float previous=0;
        for (int frame=1;frame<=200;++frame) {
            const float position=(frame%3==0 ? .6f : -.6f);
            const float rate=(position-previous)/.004f;
            observer.update({1,1.+frame*.004,.004f,{rate,0},{},1600,1,true});
            const auto estimate=observer.estimate(1,1.+frame*.004);
            if(frame>20) require_true(estimate.valid && std::abs(estimate.target_motion_stick.x)<.04f,
                "bounded position noise cannot become a large sustaining motion command");
            previous=position;
        }
    });
    registry.add_case("BaseBodyLock", "camera_work_removed_from_stationary_target", test_camera_work_is_removed_from_stationary_target);
    registry.add_case("BaseBodyLock", "perfect_tracking_recovers_total_command", test_perfect_tracking_recovers_total_target_command_and_y_sign);
    registry.add_case("BaseBodyLock", "speed_step_reaches_total_without_error_debt", test_speed_step_reaches_total_demand_without_error_accumulation);
    registry.add_case("BaseBodyLock", "deceleration_discharges_faster_than_rise", test_deceleration_discharges_faster_than_rise);
    registry.add_case("BaseBodyLock", "replacement_requires_new_aligned_evidence", test_target_replacement_requires_new_aligned_evidence);
}
