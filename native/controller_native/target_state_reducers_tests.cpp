#include "target_state_reducers.h"
#include "intent_filter.h"
#include "incident_fixture_support.h"
#include <random>
#include "test_support/native_test_registry.h"

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::VisionCandidate candidate() {
    pipeline_contract::VisionCandidate value{};
    value.has_aim_point = true;
    value.aim_px = {50.0f, 30.0f};
    value.has_aim_region = true;
    value.aim_region_px = {0.0f, 0.0f, 100.0f, 100.0f};
    value.aim_region_source =
        pipeline_contract::AimRegionSource::VisionGeometry;
    return value;
}

void test_geometry_and_desired_point_have_separate_state() {
    controller_native::TargetGeometryReducer geometry;
    controller_native::DesiredPointReducer desired({100.0f, 50.0f});
    geometry.adopt(candidate(), false);
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    require(geometry.snapshot().available, "R was not admitted");
    require(std::fabs(desired.snapshot().normalized.x - 0.5f) < 0.001f,
            "D did not initialize from the source point");

    pipeline_contract::IntentState intent{};
    intent.activation = pipeline_contract::AssistActivation::Engaged;
    intent.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.filtered_right = {1.0f, 0.0f};
    desired.reduce_manual(intent, geometry.snapshot(), true, false, 0.01f);
    require(desired.snapshot().normalized.x > 0.5f,
            "manual correction did not move D");
    require(std::fabs(geometry.snapshot().source_position.x - 50.0f) < 0.001f,
            "D correction rewrote source-owned R geometry");
}

void test_geometry_refresh_preserves_user_corrected_d() {
    controller_native::TargetGeometryReducer geometry;
    controller_native::DesiredPointReducer desired({100.0f, 50.0f});
    auto observed = candidate();
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    pipeline_contract::IntentState intent{};
    intent.activation = pipeline_contract::AssistActivation::Engaged;
    intent.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.filtered_right = {1.0f, 0.0f};
    desired.reduce_manual(intent, geometry.snapshot(), true, false, 0.02f);
    const float corrected = desired.snapshot().normalized.x;

    const bool had_geometry = geometry.snapshot().available;
    observed.aim_px = {20.0f, 30.0f};
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, false, had_geometry);
    require(std::fabs(desired.snapshot().normalized.x - corrected) < 0.001f,
            "fresh R geometry erased user-owned D");
}

void test_acquisition_point_preserves_arrival_neighborhood() {
    for (const float radius : {1.0f, 8.0f, 16.0f}) {
        for (const float gap : {0.0f, 1.0f, 8.0f, 16.0f, 20.0f, 60.0f}) {
            controller_native::TargetGeometryReducer geometry;
            controller_native::DesiredPointReducer desired;
            geometry.adopt(candidate(), false);
            desired.adopt_geometry(geometry.snapshot(), false, true, false);
            desired.select_acquisition_point(geometry.snapshot(), {50, 30 - gap}, radius);
            const float saved = 30 - desired.snapshot().position.y;
            require(saved >= -.001f && saved <= 10.001f,
                "automatic D left its bounded upper segment");
            if (gap <= 2 * radius) {
                require(std::fabs(saved) < .001f,
                    "near-reticle D selection manufactured arrival or erased a crossing");
            } else {
                require(gap - saved >= 2 * radius - .001f,
                    "automatic D consumed the reserved approach/braking distance");
            }
        }
    }
}

void test_acquisition_point_carries_geometry_and_yields_to_manual() {
    controller_native::TargetGeometryReducer geometry;
    controller_native::DesiredPointReducer desired;
    auto observed = candidate();
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    desired.select_acquisition_point(geometry.snapshot(), {50, -30}, 8);
    require(std::fabs(desired.snapshot().position.y - 20) < .001f,
        "upper central acquisition did not choose the bounded point");
    const auto normalized = desired.snapshot().normalized;
    // Source point jitter does not erase the selected normalized D.
    observed.aim_px.y = 34;
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, false, true);
    require(std::fabs(desired.snapshot().normalized.y - normalized.y) < .001f,
        "fresh default point jitter reselected automatic D");
    observed.aim_region_px = {100, 200, 200, 200};
    observed.aim_px = {200, 260};
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, false, true);
    require(std::fabs(desired.snapshot().position.y - 240) < .001f,
        "translated/scaled geometry lost relative D");
    pipeline_contract::IntentState intent{};
    intent.activation = pipeline_contract::AssistActivation::Engaged;
    intent.right_purpose = pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.filtered_right = {0, -.5f};
    desired.reduce_manual(intent, geometry.snapshot(), true, false, .01f);
    const float manual_y = desired.snapshot().position.y;
    desired.select_acquisition_point(geometry.snapshot(), {200, 100}, 8);
    require(manual_y > 240 && std::fabs(desired.snapshot().position.y - manual_y) < .001f,
        "new acquisition selection overwrote deliberate correction");
    desired.reset();
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    require(std::fabs(desired.snapshot().position.y - 260) < .001f,
        "reset carried an old acquisition offset");
}

void test_gamepad_manual_never_edits_source_point() {
    using namespace controller_native;
    for (unsigned seed : {714293u, 990137u}) {
        std::mt19937 rng(seed);
        for (int scenario=0; scenario<120; ++scenario) {
            IntentFilterConfig config; config.gamepad_right_stick_curve=true;
            IntentFilter filter(config);
            TargetGeometryReducer geometry; DesiredPointReducer desired;
            auto observed=candidate(); geometry.adopt(observed,false);
            desired.adopt_geometry(geometry.snapshot(),false,true,false);
            const float dt=scenario%3==0?.001f:scenario%3==1?.004f:.016f;
            const float magnitude=.05f+.9f*(rng()%1000)/1000.f;
            double time=1;
            for(int tick=0;tick<int((scenario%2?8.f:1.2f)/dt);++tick) {
                // Move/scale the source, including changes in its relative point.
                observed.aim_px={50.f+std::sin(float(time))*10.f,30.f+std::cos(float(time))*10.f};
                geometry.adopt(observed,false);
                desired.adopt_geometry(geometry.snapshot(),false,false,true);
                const bool release=tick%200>150;
                const auto manual=release?pipeline_contract::Vec2f{}:
                    pipeline_contract::Vec2f{magnitude*std::sin(tick*.07f),-magnitude};
                auto intent=filter.update({},manual,true,false,time,true);
                desired.reduce_manual(intent,geometry.snapshot(),true,scenario%3!=0,dt);
                const auto& d=desired.snapshot();
                require(std::hypot(d.position.x-observed.aim_px.x,d.position.y-observed.aim_px.y)<1e-5f,
                    "gamepad input changed the source-owned AI aim point");
                require(!d.user_active && d.source==pipeline_contract::DesiredPointSource::VisionDefault,
                    "gamepad gesture took ownership of anatomical point");
                time+=dt;
            }
        }
    }
}

void test_axis_gestures_do_not_reclassify_each_other() {
    for (bool reverse : {false, true}) for (bool held_vertical : {false, true})
    for (float held_sign : {-1.f, 1.f}) for (float new_sign : {-1.f, 1.f}) {
        controller_native::IntentFilterConfig config;
        config.gamepad_right_stick_curve = true;
        controller_native::IntentFilter filter(config);
        controller_native::TargetGeometryReducer geometry;
        controller_native::DesiredPointReducer desired;
        geometry.adopt(candidate(), false);
        desired.adopt_geometry(geometry.snapshot(), false, true, false);
        const auto origin = desired.snapshot().normalized;
        const auto vector = [held_vertical,held_sign](float changing) {
            return held_vertical ? pipeline_contract::Vec2f{changing, held_sign*.4f}
                                 : pipeline_contract::Vec2f{held_sign*.4f, changing};
        };
        const float prior = reverse ? -new_sign*.9f : 0.f;
        filter.update({}, vector(prior), true, false, 1, false);
        filter.update({}, vector(prior), true, false, 1.001, true);
        const auto intent = filter.update({}, vector(new_sign*(reverse?.9f:.23f)), true, false, 1.002, true);
        desired.reduce_manual(intent, geometry.snapshot(), true, false, .001f);
        const auto& d = desired.snapshot();
        require(held_vertical ? d.manual_correction_x : d.manual_correction_y,
            "new axis gesture hidden by orthogonal held input");
        require(!(held_vertical ? d.manual_correction_y : d.manual_correction_x),
            "new axis gesture reclassified orthogonal carried input");
        require(std::fabs(held_vertical ? d.normalized.y-origin.y : d.normalized.x-origin.x)<1e-6f,
            "carried axis rewrote D");
    }
}

void test_axis_purpose_reaches_native_arbitration() {
    using namespace controller_native;
    for (bool held_vertical : {false, true})
    for (float held_sign : {-1.f,1.f}) for (float new_sign : {-1.f,1.f}) {
        auto config=incident_fixture::base_config(40,180);
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.visual_authority_enabled=false;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;spec.observation_id=1;spec.selector_generation=42;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        int corrected=0;
        for(int tick=0;tick<500;++tick){
            // Observation-contract replay: AI opposes the held axis while
            // an independent gesture starts on the other axis, in every quadrant.
            const float error=tick>=200 ? 12.f*held_sign : 0.f;
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec,tick+1,now,held_vertical?0.f:-error,held_vertical?error:0.f));
            const float fresh=tick>=300 ? new_sign*.23f : 0.f;
            auto physical=incident_fixture::ads_input(
                held_vertical?fresh:held_sign*.4f,held_vertical?held_sign*.4f:fresh);
            const auto output=controller.build_output(physical);
            const auto& plan=controller.last_target_plan();
            if(tick>=310){
                require(std::hypot(plan.aim_px.x-plan.source_aim_px.x,plan.aim_px.y-plan.source_aim_px.y)<1e-5f,
                    "native chain rewrote the source aim point during manual correction");
                require(plan.mode==pipeline_contract::ControlMode::BodyLockFollow,"native axis fixture did not reach BodyLock");
                require(held_vertical ? (plan.manual_correction_x&&!plan.manual_correction_y)
                                      : (plan.manual_correction_y&&!plan.manual_correction_x),
                    "axis ownership lost in native coordinator");
                require(std::fabs((held_vertical?output.right_y:output.right_x)-held_sign*.4f)<1e-6f,
                    "new orthogonal gesture revoked carried manual authority");
                ++corrected;
            }
            now+=.001;
        }
        require(corrected==190,"native axis fixture incomplete");
    }
}

void test_native_ads_correction_preserves_axis_authority() {
 using namespace controller_native;
 for(bool vertical:{false,true})for(float sign:{-1.f,1.f}) {
  auto config=incident_fixture::base_config(40,180);
  config.ai_aim.ads_completion_fresh_frames=1000;
  config.ai_aim.ads_extension_budget_ms=500;
  double now=10;NativeGamepadController controller(config,&now);
  incident_fixture::TargetSpec spec;spec.observation_id=1;spec.selector_generation=33;
  for(int tick=0;tick<40;++tick) {
   controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,tick+1,now,vertical?0:-sign*40,vertical?sign*40:0));
   auto physical=incident_fixture::ads_input(tick>=20&&!vertical?sign*.4f:0,tick>=20&&vertical?sign*.4f:0);
   auto output=controller.build_output(physical);
   if(tick==30) {
    const auto& plan=controller.last_target_plan();
    if(plan.mode!=pipeline_contract::ControlMode::AdsAcquire || !(vertical?plan.manual_correction_y:plan.manual_correction_x))
     throw std::runtime_error("fixture did not exercise fresh manual correction during ADS");
    if(std::fabs((vertical?output.right_y:output.right_x)-sign*.4f)>1e-5f)
     throw std::runtime_error("native ADS chain discarded explicit full manual axis authority");
   }
   now+=.001;
  }
 }
}

}  // namespace

#include "ads_carry_regression_tests.h"

void register_target_state_reducers_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "ads_carry_does_not_become_correction_on_timeout", ads_carry_does_not_become_correction_on_timeout);
    registry.add_case("BaseBodyLock", "native_ads_correction_preserves_axis_authority", test_native_ads_correction_preserves_axis_authority);
    registry.add_case("BaseBodyLock", "gamepad_manual_never_edits_source_point", test_gamepad_manual_never_edits_source_point);
    registry.add_case("BaseBodyLock", "axis_purpose_reaches_native_arbitration", test_axis_purpose_reaches_native_arbitration);
    registry.add_case("BaseBodyLock", "axis_gestures_do_not_reclassify_each_other", test_axis_gestures_do_not_reclassify_each_other);
    registry.add_case("BaseBodyLock", "acquisition_point_preserves_arrival_neighborhood", test_acquisition_point_preserves_arrival_neighborhood);
    registry.add_case("BaseBodyLock", "acquisition_point_carries_geometry_and_yields_to_manual", test_acquisition_point_carries_geometry_and_yields_to_manual);
    registry.add_case("BaseBodyLock", "geometry_and_desired_point_have_separate_state", test_geometry_and_desired_point_have_separate_state);
    registry.add_case("BaseBodyLock", "geometry_refresh_preserves_user_corrected_d", test_geometry_refresh_preserves_user_corrected_d);
}
