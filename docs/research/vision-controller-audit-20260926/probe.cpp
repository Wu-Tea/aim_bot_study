// Isolated source-behavior probes. These do not simulate a game or prove
// runtime reachability of every manually constructed arbitration state.
#include "controller_native/assist_control_state_machine.h"
#include "controller_native/aim_dynamics_shaper.h"
#include "controller_native/ads_acquisition_controller.h"
#include "controller_native/bodylock_follow_controller.h"
#include "controller_native/bodylock_target_motion_observer.h"
#include <cmath>
#include <iomanip>
#include <iostream>

int main() {
    using namespace controller_native;
    using namespace pipeline_contract;
    TargetPlan p{};
    p.target_id = 1;
    p.lifecycle = TargetLifecycle::Observed;
    p.mode = ControlMode::BodyLockFollow;
    p.aim_authority = p.reliability = 1;
    p.response_scale = 650;
    p.error_px = {1, 0};
    BodylockFollowControllerConfig bc{};
    bc.feedback_range_x_px = bc.feedback_range_y_px = 18;
    bc.max_force_x = .8f; bc.max_force_y = .6f;
    auto body = BodylockFollowController(bc).compute_detailed(p, {}, .001f);
    bc.max_force_x = .4f;
    auto weaker = BodylockFollowController(bc).compute_detailed(p, {}, .001f);

    AdsAcquisitionControllerConfig ac{};
    ac.arrival_horizon_seconds = .135f;
    p.mode = ControlMode::AdsAcquire;
    p.error_px = {0, -1}; p.normalized_size = .10f;
    auto above = AdsAcquisitionController(ac).compute(p, {}, .001f);
    p.error_px.y = 1;
    auto below = AdsAcquisitionController(ac).compute(p, {}, .001f);
    p.normalized_size = .40f;
    auto close = AdsAcquisitionController(ac).compute(p, {}, .001f);
    p.error_px = {800, -900};
    auto force_100 = AdsAcquisitionController(ac).compute(p, {}, .001f);
    ac.max_force_y = .9f;
    auto force_090 = AdsAcquisitionController(ac).compute(p, {}, .001f);

    AimDynamicsShaper shaper;
    p.mode = ControlMode::BodyLockFollow;
    shaper.shape({.8f,0}, {}, p, .001f);
    shaper.adopt({.8f,0});
    float first_reverse = shaper.shape({-.8f,0},{},p,.001f).x;
    int ticks = 1;
    while (shaper.current().x > 0 && ticks < 100) {
        shaper.shape({-.8f,0},{},p,.001f); ++ticks;
    }

    BodylockTargetMotionObserver observer;
    BodylockTargetMotionObservation o{};
    o.target_id = 1; o.direct_person_observation = true;
    o.interval_seconds = .010f; o.reliability = 1;
    o.response_px_per_stick_second = 650;
    o.average_delivered_stick = {.5f,0};
    o.observed_screen_rate_px_per_sec = {-459.305542f,0};
    o.capture_seconds = 1; observer.update(o);
    o.capture_seconds = 1.01; observer.update(o);
    auto motion = observer.estimate(1,1.011);

    AssistControlStateMachineConfig fc{};
    fc.use_gamepad_intent_for_arbitration = true;
    fc.capture_settle_radius_px = 8;
    AssistControlStateMachine fuser(fc);
    AssistControlStateMachineInput i{};
    i.aiming = i.target_authoritative = true;
    i.target_id = 1; i.mode = ControlMode::BodyLockFollow;
    i.visual_authority = 1; i.manual_stick = {-.225f,0};
    i.filtered_manual_stick = {-.1125f,0};
    i.manual_axis_activity = {.5f,0}; i.ai_stick = {.6f,0};
    i.target_error_px = {8,0};
    i.fresh_observation = true;
    float fresh = fuser.update(i).stick.x;
    i.fresh_observation = false;
    float held = fuser.update(i).stick.x;
    i.manual_correction_x = true;
    i.fresh_observation = true;
    float owned_fresh = fuser.update(i).stick.x;
    i.fresh_observation = false;
    float owned_held = fuser.update(i).stick.x;

    std::cout << std::setprecision(9)
        << "{\n  \"body_horizon_x_ms\": " << body.response_horizon_seconds*1000
        << ",\n  \"body_horizon_y_ms\": " << body.response_horizon_y_seconds*1000
        << ",\n  \"body_position_gain_ratio_strength_08_vs_04\": " << body.position_stick.x/weaker.position_stick.x
        << ",\n  \"ads_above_vs_below_ratio\": " << std::fabs(above.y/below.y)
        << ",\n  \"ads_close_vs_far_ratio\": " << std::fabs(close.y/below.y)
        << ",\n  \"ads_vertical_cap_09_vs_10_difference\": " << std::hypot(force_090.x-force_100.x, force_090.y-force_100.y)
        << ",\n  \"shaper_first_reverse_output\": " << first_reverse
        << ",\n  \"shaper_ticks_until_old_sign_zero\": " << ticks
        << ",\n  \"stationary_model_mismatch_motion_stick\": " << motion.target_motion_stick.x
        << ",\n  \"same_observer_state_at_response_650_px_s\": " << motion.target_motion_stick.x*650
        << ",\n  \"same_observer_state_at_response_918_px_s\": " << motion.target_motion_stick.x*918.611084f
        << ",\n  \"isolated_arbiter_fresh\": " << fresh
        << ",\n  \"isolated_arbiter_held\": " << held
        << ",\n  \"owned_d_fresh\": " << owned_fresh
        << ",\n  \"owned_d_held\": " << owned_held << "\n}\n";
    return !(std::fabs(body.position_stick.x/weaker.position_stick.x-2)<1e-5f &&
        first_reverse > 0 && ticks == 17 && motion.valid &&
        std::fabs(owned_fresh-owned_held)<1e-6f);
}
