#include "aim_dynamics_shaper.h"
#include "bodylock_follow_controller.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <random>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan active_plan() {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.confidence = 1.0f;
    plan.aim_authority = 1.0f;
    return plan;
}

void test_step_and_reversal_are_bounded() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    auto output = shaper.shape({1.0f, 0.0f}, plan, 0.01f);
    require_true(output.x > 0.0f && output.x <= 0.081f,
                 "initial AI step must obey slew limit");
    const float before = output.x;
    output = shaper.shape({-1.0f, 0.0f}, plan, 0.01f);
    require_true(std::fabs(output.x - before) <= 0.081f,
                 "reversal must not jump across the axis");
}

void test_reversal_discharges_before_opposite_rise() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    pipeline_contract::Vec2f output{};
    for (int index = 0; index < 8; ++index) {
        output = shaper.shape({1.0f, 0.0f}, plan, 0.001f);
    }
    const float before = output.x;
    output = shaper.shape({-1.0f, 0.0f}, plan, 0.001f);
    require_true(before > 0.0f && output.x >= 0.0f && output.x < before,
                 "a reversal must discharge stale AI work without crossing zero");
}

void test_plan_loss_decays_stale_force_without_reversal() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 20; ++i) shaper.shape({0.6f, 0.0f}, plan, 0.01f);
    const auto before = shaper.current();
    pipeline_contract::TargetPlan missing{};
    const auto after = shaper.shape({}, missing, 0.01f);
    require_true(before.x > 0.0f && after.x >= 0.0f && after.x < before.x,
                 "plan loss must monotonically discharge stale AI force");
}

void test_cue_continuation_never_ramps_without_observation() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto continued = shaper.shape({0.8f, 0.0f}, plan, 0.001f);
    require_true(continued.x <= observed.x + 0.0001f,
                 "cue continuation must not increase assist without a new observation");
}

void test_measured_cue_motion_uses_bounded_slew() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    shaper.shape({}, plan, .001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    plan.bodylock_target_motion_valid = true;
    plan.bodylock_target_motion_from_cue = true;
    const auto tracking = shaper.shape({.20f, -.10f}, plan, .001f);
    require_true(tracking.x > 0 && tracking.y < 0 &&
                 tracking.x <= .081f && tracking.y >= -.081f,
                 "measured current cue motion must resume through ordinary bounded slew");
    plan.bodylock_target_motion_from_cue = false;
    const auto unmeasured = shaper.shape({.40f, -.20f}, plan, .001f);
    require_true(unmeasured.x <= tracking.x && unmeasured.y >= tracking.y,
                 "a historical motion hint must not remove blind-rise protection");
}

void test_cue_continuation_cannot_start_unobserved_axis() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto observed = shaper.shape({0.8f, 0.0f}, plan, 0.001f);
    plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto continued = shaper.shape({0.8f, 0.8f}, plan, 0.001f);
    require_true(continued.x <= observed.x + 0.0001f,
                 "cue continuation must not amplify an observed axis");
    require_true(std::fabs(continued.y) <= 0.0001f,
                 "unconfirmed Y must retain blind-rise protection");
}

void test_shaper_never_amplifies_work_after_controller_reduces_request() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 12; ++i) {
        (void)shaper.shape({0.60f, 0.0f}, plan, 0.01f);
    }
    const auto reduced = shaper.shape({0.10f, 0.0f}, plan, 0.001f);
    require_true(reduced.x >= 0.10f && reduced.x < 0.60f,
                 "a reduced request must produce monotonic bounded discharge");
}

void test_shaper_reversal_must_discharge_old_direction_first() {
    controller_native::AimDynamicsShaper shaper;
    const auto plan = active_plan();
    for (int i = 0; i < 12; ++i) {
        (void)shaper.shape({0.60f, 0.0f}, plan, 0.01f);
    }
    const auto discharge = shaper.shape({-0.60f, 0.0f}, plan, 0.01f);
    require_true(discharge.x >= 0.0f && discharge.x < 0.60f,
                 "a requested reversal must discharge before driving the opposite direction");
}

void test_ads_to_bodylock_same_direction_does_not_carry_ads_force() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 42;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({1.34f, 0.0f}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.0f}, bodylock, 0.01f);
    require_true(output.x <= 0.581f,
                 "BodyLock handoff carried stale same-direction ADS force");
}

void test_ads_to_bodylock_opposite_direction_does_not_carry_ads_force() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 43;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({-1.34f, 0.0f}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.0f}, bodylock, 0.01f);
    require_true(output.x >= -0.081f,
                 "BodyLock handoff carried stale opposite-direction ADS force");
}

void test_ads_to_bodylock_vector_handoff_respects_both_components() {
    controller_native::AimDynamicsShaper shaper;
    auto ads = active_plan();
    ads.target_id = 44;
    ads.mode = pipeline_contract::ControlMode::AdsAcquire;
    for (int index = 0; index < 24; ++index) {
        (void)shaper.shape({1.34f, -1.0f}, ads, 0.01f);
    }
    auto bodylock = ads;
    bodylock.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto output = shaper.shape({0.50f, 0.20f}, bodylock, 0.01f);
    require_true(output.x <= 0.581f && output.y <= 0.281f,
                 "BodyLock handoff retained stale force on a vector component");
    const float output_length = std::hypot(output.x, output.y);
    const float request_length = std::hypot(0.50f, 0.20f);
    require_true(output_length <= request_length * 1.15f + 0.001f,
                 "BodyLock handoff exceeded the new vector request envelope");
}

void test_target_change_does_not_smooth_old_target_force_into_new_target() {
    controller_native::AimDynamicsShaper shaper;
    auto old_target = active_plan();
    old_target.target_id = 100;
    for (int index = 0; index < 20; ++index) {
        (void)shaper.shape({0.90f, 0.0f}, old_target, 0.01f);
    }
    auto new_target = old_target;
    new_target.target_id = 200;
    const auto output = shaper.shape({-0.20f, 0.0f}, new_target, 0.01f);
    require_true(output.x >= -0.201f && output.x <= 0.201f,
                 "new target inherited stale shaper force from the old target");
}

void test_initial_target_acquisition_uses_normal_slew() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    const auto idle = shaper.shape({}, plan, 0.01f);
    require_true(std::fabs(idle.x) <= 0.0001f,
                 "idle shaper must start at a neutral AI baseline");
    plan.target_id = 300;
    const auto acquired = shaper.shape({0.90f, 0.0f}, plan, 0.01f);
    require_true(acquired.x > 0.0f && acquired.x <= 0.081f,
                 "initial target acquisition bypassed the normal slew envelope");
}

void test_non_handoff_mode_change_keeps_normal_slew() {
    controller_native::AimDynamicsShaper shaper;
    auto plan = active_plan();
    plan.target_id = 301;
    for (int index = 0; index < 20; ++index) {
        (void)shaper.shape({0.60f, 0.0f}, plan, 0.01f);
    }
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto output = shaper.shape({0.10f, 0.0f}, plan, 0.01f);
    require_true(output.x < 0.60f && output.x > 0.50f,
                 "a non-handoff mode change bypassed normal decay slew");
}

void test_mouse_bodylock_temporal_boundaries() {
    controller_native::AimDynamicsShaperConfig config;
    config.bodylock_accel_ms=40;config.bodylock_decel_ms=25;
    config.bodylock_max_force={.30f,.33f};config.bodylock_authority_budget_scale=.5f;
    for(int axis=0;axis<2;++axis) {
        controller_native::AimDynamicsShaper shaper(config), legacy;
        auto plan=active_plan();plan.target_id=77;plan.reliability=1;
        const float peak=axis ? .33f:.30f;
        const auto vector=[&](float v) { return axis ? pipeline_contract::Vec2f{0,v}:pipeline_contract::Vec2f{v,0}; };
        const auto component=[&](pipeline_contract::Vec2f v) { return axis ? v.y:v.x; };
        for(int i=0;i<40;++i) shaper.shape(vector(peak), plan, .001f);
        float previous=peak;
        for(int i=0;i<25;++i) {
            const float value=component(shaper.shape(vector(-peak), plan, .001f));
            require_true(value>=-1e-6f && value<=previous+1e-6f && previous-value<=peak/25+1e-5f,
                "timed reversal must brake old direction before opposite acceleration");
            previous=value;
        }
        const float opposite=component(shaper.shape(vector(-peak), plan, .001f));
        require_true(opposite<0 && opposite>=-peak/40-1e-5f,"opposite motion starts with normal rise");
        plan.aim_authority=0;
        require_true(std::fabs(component(shaper.shape(vector(-peak), plan, .001f)))<1e-6f,
            "slower braking may not prolong revoked target authority");
        plan.aim_authority=1;
        for(int i=0;i<40;++i) shaper.shape(vector(peak), plan, .001f);
        plan.reliability=.1f;
        require_true(std::fabs(component(shaper.shape(vector(peak), plan, .001f)))<=.050001f,
            "shrinking evidence must reduce delivered force budget immediately");
        plan.reliability=1;plan.target_id=99;
        require_true(component(shaper.shape(vector(-peak), plan, .001f))>=-peak/40-1e-5f,
            "replacement target cannot inherit the old ramp");
        plan.lifecycle=pipeline_contract::TargetLifecycle::CueContinuation;
        const float before=std::abs(component(shaper.current()));
        require_true(std::abs(component(shaper.shape(vector(-peak), plan, .001f)))<=before+1e-6f,
            "cue-only continuation cannot accelerate blindly");
        shaper.reset();plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
        plan.mode=pipeline_contract::ControlMode::AdsAcquire;
        for(int i=0;i<15;++i) {
            const auto a=shaper.shape(vector(1), plan, .001f);
            const auto b=legacy.shape(vector(1), plan, .001f);
            require_true(component(a)==component(b),"BodyLock timing must leave ADS response unchanged");
        }
    }
}

void test_mouse_moving_target_keeps_speed_at_zero_error() {
    controller_native::BodylockFollowControllerConfig follow_config;
    follow_config.max_force_x=.30f;follow_config.max_force_y=.33f;
    follow_config.authority_budget_scale=.5f;
    follow_config.fallback_response_px_per_stick_second=2000;
    controller_native::BodylockFollowController follow(follow_config);
    controller_native::AimDynamicsShaperConfig config;
    config.bodylock_accel_ms=40;config.bodylock_decel_ms=25;
    config.bodylock_max_force={.30f,.33f};config.bodylock_authority_budget_scale=.5f;
    for(int axis=0;axis<2;++axis) {
        controller_native::AimDynamicsShaper shaper(config);
        auto plan=active_plan();plan.target_id=77;plan.reliability=1;
        plan.response_confidence=1;plan.response_scale=2000;
        plan.bodylock_target_motion_valid=true;
        plan.bodylock_target_motion_px_per_sec=axis ? pipeline_contract::Vec2f{0,120}:pipeline_contract::Vec2f{120,0};
        const auto component=[&](pipeline_contract::Vec2f v) { return axis ? -v.y:v.x; };
        float value=0;
        for(int i=0;i<80;++i) {
            const auto requested=follow.compute(plan);
            value=component(shaper.shape(requested, plan, .001f));
            if(i>=40) require_true(std::abs(value-.06f)<1e-5f,
                "centered moving target must retain its sustaining velocity after the ramp");
        }
        plan.bodylock_target_motion_px_per_sec={};
        const float braking=component(shaper.shape(follow.compute(plan), plan, .001f));
        require_true(braking>0 && braking<value,"target stop begins braking rather than a velocity cliff");
        for(int i=0;i<25;++i) value=component(shaper.shape(follow.compute(plan), plan, .001f));
        require_true(std::abs(value)<1e-6f,"stationary centered target must finish braking without drift");
    }
}

}  // namespace

void register_aim_dynamics_shaper_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "ownership_return_preserves_manual_and_tracking", [] {
        using V=pipeline_contract::Vec2f;
        controller_native::AimDynamicsShaper s;
        const V ai{-.55f,.2f};
        s.shape_ownership({.4f,.2f},{.4f,0},ai,.001f,true,true);
        const auto first=s.shape_ownership({-.55f,.2f},{.1f,0},ai,.001f,true,false);
        require_true(first.x > 0 && std::fabs(first.x-.1f)<=.06401f,
            "released acquisition must not instantly expose the full opposing AI");
        require_true(first.y==ai.y,"X handback must not reclassify held Y tracking");
        for(int i=0;i<20;++i) s.shape_ownership(ai,{0,0},ai,.001f,true,false);
        const auto tracking=s.shape_ownership({-.58f,.24f},{0,0},{-.58f,.24f},.001f,true,false);
        require_true(tracking.x==-.58f && tracking.y==.24f,"settled ownership must not filter tracking twice");
        s.shape_ownership({0,0},{0,0},{0,0},.001f,true,false);
        const auto crossing=s.shape_ownership({.06f,.06f},{0,0},{.06f,.06f},.001f,true,false);
        require_true(crossing.x==.06f && crossing.y==.06f,
            "equal manual/AI commands must not invent ownership loss at a diagonal zero crossing");
        const auto manual=s.shape_ownership({1,-1},{1,-1},ai,.001f,true,false);
        require_true(manual.x==1 && manual.y==-1,"strong manual reclaim must be immediate");
        const auto exit=s.shape_ownership({.012f,-.014f},{.012f,-.014f},ai,.001f,false,false);
        require_true(exit.x==.012f && exit.y==-.014f,"exit must preserve exact zero-deadzone passthrough");
        const auto fresh=s.shape_ownership(ai,{0,0},ai,.001f,true,true);
        require_true(fresh.x==ai.x && fresh.y==ai.y,"new target must not inherit a handback delay");
    });
    registry.add_case("BaseBodyLock", "ownership_return_randomized_vector_envelope", [] {
        for(unsigned seed:{23102311u,98102731u}) {
            std::mt19937 rng(seed);
            for(int i=0;i<10000;++i) {
                const auto value=[&](){return (int(rng()%20001)-10000)/10000.f;};
                const pipeline_contract::Vec2f manual{value(),value()},ai{value(),value()};
                controller_native::AimDynamicsShaper s;
                s.shape_ownership(manual,manual,ai,.001f,true,true);
                const auto out=s.shape_ownership(ai,manual,ai,.001f,true,false);
                require_true(std::hypot(out.x-manual.x,out.y-manual.y)<=.06401f,
                    "AI ownership work must obey a 2D envelope without spending raw manual work");
                require_true(out.x>=std::min(manual.x,ai.x)-1e-6f && out.x<=std::max(manual.x,ai.x)+1e-6f &&
                    out.y>=std::min(manual.y,ai.y)-1e-6f && out.y<=std::max(manual.y,ai.y)+1e-6f,
                    "ownership interpolation must stay inside current authorized segments");
                const auto regained=s.shape_ownership(manual,manual,ai,.001f,true,false);
                require_true(regained.x==manual.x && regained.y==manual.y,"AI revocation is immediate");
            }
        }
    });
    registry.add_case("BaseBodyLock", "gap_prediction_never_amplifies_or_reverses", [] {
        for (float dt : {.0005f, .001f, .004f}) {
            controller_native::AimDynamicsShaper shaper;
            auto plan=active_plan();plan.target_id=42;
            shaper.shape({.2f,-.2f}, plan, dt);
            shaper.adopt({.2f,-.2f});
            plan.observation_gap=true;plan.gap_prediction_valid=true;plan.observation_gap_remaining_ms=25;
            const auto bounded=shaper.shape({1,-1}, plan, dt);
            require_true(bounded.x>0 && bounded.x<.2f && bounded.y<0 && bounded.y>-.2f,"missing pixels must discharge already-supported work");
            const auto reversed=shaper.shape({-1,1}, plan, dt);
            require_true(reversed.x==0 && reversed.y==0,"prediction cannot start opposite work");
            const auto restart=shaper.shape({1,-1}, plan, dt);
            require_true(restart.x==0 && restart.y==0,"an unsupported axis cannot restart during the gap");
            shaper.adopt({.2f,-.2f});plan.gap_prediction_valid=false;plan.aim_authority=0;
            const auto identity_only=shaper.shape({1,-1}, plan, dt);
            require_true(identity_only.x==0 && identity_only.y==0,"identity alone cannot retain stale force");
        }
    });
    registry.add_case("BaseBodyLock", "gap_discharge_uses_capture_deadline_not_tick_count", [] {
        for (int hz : {500,1000,2000}) {
            controller_native::AimDynamicsShaper shaper;
            auto plan=active_plan();plan.target_id=7;
            shaper.shape({.2f,0}, plan, 1.f/hz);shaper.adopt({.2f,0});
            plan.observation_gap=true;plan.gap_prediction_valid=true;
            pipeline_contract::Vec2f output{};
            for (int tick=1;tick<=hz*24/1000;++tick) {
                plan.observation_gap_remaining_ms=32.f-tick*1000.f/hz;
                output=shaper.shape({1,0}, plan, 1.f/hz);
            }
            require_true(std::fabs(output.x-.05f)<1e-5f,"same capture age must spend the same prediction budget at every controller cadence");
        }
    });
    registry.add_case("BaseBodyLock", "measured_cue_motion_uses_bounded_slew", test_measured_cue_motion_uses_bounded_slew);
    registry.add_case("BaseBodyLock", "mouse_bodylock_temporal_boundaries", test_mouse_bodylock_temporal_boundaries);
    registry.add_case("BaseBodyLock", "mouse_moving_target_keeps_speed_at_zero_error", test_mouse_moving_target_keeps_speed_at_zero_error);
    registry.add_case("BaseBodyLock", "shaper_step_and_reversal_are_bounded", test_step_and_reversal_are_bounded);
    registry.add_case("BaseBodyLock", "shaper_reversal_discharges_before_rise", test_reversal_discharges_before_opposite_rise);
    registry.add_case("BaseBodyLock", "plan_loss_decays_without_reversal", test_plan_loss_decays_stale_force_without_reversal);
    registry.add_case("BaseBodyLock", "cue_never_ramps_without_observation", test_cue_continuation_never_ramps_without_observation);
    registry.add_case("BaseBodyLock", "cue_cannot_start_unobserved_axis", test_cue_continuation_cannot_start_unobserved_axis);
    registry.add_case("BaseBodyLock", "shaper_never_amplifies_reduced_request", test_shaper_never_amplifies_work_after_controller_reduces_request);
    registry.add_case("BaseBodyLock", "shaper_reversal_discharges_old_direction", test_shaper_reversal_must_discharge_old_direction_first);
    registry.add_case("BaseBodyLock", "ads_bodylock_same_direction_has_no_carry", test_ads_to_bodylock_same_direction_does_not_carry_ads_force);
    registry.add_case("BaseBodyLock", "ads_bodylock_opposite_has_no_carry", test_ads_to_bodylock_opposite_direction_does_not_carry_ads_force);
    registry.add_case("BaseBodyLock", "ads_bodylock_vector_handoff_respects_components", test_ads_to_bodylock_vector_handoff_respects_both_components);
    registry.add_case("BaseBodyLock", "target_change_does_not_smooth_old_force", test_target_change_does_not_smooth_old_target_force_into_new_target);
    registry.add_case("BaseBodyLock", "initial_acquisition_uses_normal_slew", test_initial_target_acquisition_uses_normal_slew);
    registry.add_case("BaseBodyLock", "non_handoff_mode_change_uses_normal_slew", test_non_handoff_mode_change_keeps_normal_slew);
}
