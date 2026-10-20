#include "response_model_aim_solver.h"
#include "camera_response_alignment.h"
#include "aim_dynamics_shaper.h"
#include "target_coordinator.h"
#include "ads_acquisition_controller.h"
#include "incident_fixture_support.h"
#include "bodylock_target_motion_observer.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace controller_native;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected=" +
            std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

ResponseModelAimRequest base_request() {
    ResponseModelAimRequest request;
    request.response_px_per_stick_second = 500.0f;
    request.arrival_horizon_seconds = 0.050f;
    request.motion_weight = 1.0f;
    request.max_force = {1.0f, 1.0f};
    request.authority = 1.0f;
    return request;
}

void test_radial_direction_and_y_sign() {
    auto request = base_request();
    request.error_px = {3.0f, 4.0f};
    const auto output = solve_response_model_aim(request);
    require(output.unclamped_stick.x > 0.0f &&
                output.unclamped_stick.y < 0.0f,
            "screen-down target requires negative controller Y");
    require_near(output.unclamped_stick.x / -output.unclamped_stick.y,
                 3.0f / 4.0f, 1e-4f,
                 "solver must preserve radial direction");
}

void test_horizon_and_response_have_physical_units() {
    auto request = base_request();
    request.error_px = {20.0f, 0.0f};
    const auto normal = solve_response_model_aim(request);
    request.arrival_horizon_seconds = 0.025f;
    const auto urgent = solve_response_model_aim(request);
    require_near(urgent.unclamped_stick.x,
                 normal.unclamped_stick.x * 2.0f, 1e-5f,
                 "half horizon requires double position demand");
    request.arrival_horizon_seconds = 0.050f;
    request.response_px_per_stick_second = 250.0f;
    const auto slow = solve_response_model_aim(request);
    require_near(slow.unclamped_stick.x,
                 normal.unclamped_stick.x * 2.0f, 1e-5f,
                 "half camera response requires double stick");
}

void test_motion_feedforward_is_not_multiplied_by_force_cap() {
    auto request = base_request();
    request.motion_is_sustaining_target_motion = true;
    request.relative_velocity_px_per_sec = {100.0f, 0.0f};
    request.max_force = {0.45f, 0.50f};
    const auto output = solve_response_model_aim(request);
    require_near(output.motion_stick.x, 0.20f, 1e-6f,
                 "velocity divided by response is already stick demand");
    require_near(output.stick.x, 0.20f, 1e-6f,
                 "force envelope must not attenuate an in-range motion term");
}

void test_motion_does_not_reverse_current_residual() {
    auto request = base_request();
    request.error_px = {-1.0f, 0.0f};
    request.relative_velocity_px_per_sec = {100.0f, 0.0f};
    request.arrival_horizon_seconds = 0.10f;
    const auto output = solve_response_model_aim(request);
    require(output.stick.x <= 0.0f,
            "motion metadata must not reverse the current source-owned residual");
}

void test_current_position_bounds_only_opposing_axis_motion() {
    auto request = base_request();
    request.motion_is_sustaining_target_motion = true;
    request.error_px = {-10.0f, 0.0f};
    request.relative_velocity_px_per_sec = {260.0f, 120.0f};
    const auto output = solve_response_model_aim(request);
    require(output.radial_motion_bound_applied,
            "current position fixture must exercise the position-motion bound");
    require(output.bounded_motion_stick.x <= 0.0f,
            "fresh position must not keep an opposing radial motion sign");
    require_near(output.bounded_motion_stick.y, output.motion_stick.y,
                 1e-5f,
                 "axis-local bound must preserve orthogonal motion");
    require(output.radial_motion_bound_reason ==
                ResponseModelConstraintReason::PositionRadialMotionBound,
            "position-motion bound must expose its single-path reason");
}

void test_orthogonal_motion_cannot_mask_an_axis_reversal() {
    auto request = base_request();
    request.error_px = {-7.5f, -18.5f};
    request.relative_velocity_px_per_sec = {190.0f, -250.0f};
    const auto output = solve_response_model_aim(request);
    const float vector_dot =
        output.position_stick.x * output.motion_stick.x +
        output.position_stick.y * output.motion_stick.y;

    require(output.position_stick.x * output.motion_stick.x < 0.0f,
            "fixture must oppose the current X position");
    require(vector_dot > 0.0f,
            "orthogonal same-direction motion must mask the old vector test");
    require(output.radial_motion_bound_applied,
            "axis-local conflict must exercise the position-motion bound");
    require_near(output.bounded_motion_stick.y, output.motion_stick.y,
                 1e-6f,
                 "compatible orthogonal motion must remain unchanged");
    require(output.pre_curve_stick.x * output.position_stick.x >= 0.0f,
            "motion must not reverse the current source-owned X position");
}

void test_elliptical_force_envelope_scales_one_vector() {
    auto request = base_request();
    request.error_px = {100.0f, 100.0f};
    request.max_force = {0.30f, 0.20f};
    const auto output = solve_response_model_aim(request);
    const float ellipse = std::sqrt(
        std::pow(output.stick.x / 0.30f, 2.0f) +
        std::pow(output.stick.y / 0.20f, 2.0f));
    require_near(ellipse, 1.0f, 1e-5f,
                 "limited request must lie on force ellipse");
    require(output.limited, "large request must report force limiting");
}

void test_authority_caps_delivered_force_after_curve_inversion() {
    auto request = base_request();
    request.error_px = {1000.0f, 0.0f};
    request.authority = 0.20f;
    request.response_curve.algorithm =
        AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    const auto output = solve_response_model_aim(request);
    require(output.limited,
            "large low-authority request must exercise its force budget");
    require_near(output.stick.x, 0.20f, 1e-5f,
                 "authority must cap the delivered stick after inverse curve");
}

void test_clear_authority_does_not_multiply_smaller_mode_cap() {
    auto request = base_request();
    request.error_px = {1000.0f, 0.0f};
    request.max_force = {0.30f, 0.42f};
    request.authority = 0.72f;
    const auto output = solve_response_model_aim(request);
    require_near(output.stick.x, 0.30f, 1e-5f,
                 "clear evidence must preserve the smaller BodyLock cap");
}

void test_cod_dynamic_plugin_round_trips_seed_curve() {
    const auto& plugin = resolve_aim_response_curve_plugin(
        AimResponseCurveAlgorithm::CodDynamicLegacyLut);
    require(std::string(plugin.name) == "cod_dynamic_legacy_lut",
            "configured algorithm must resolve to the COD Dynamic plugin");
    require_near(plugin.forward_magnitude(0.59924f), 0.30000f, 1e-6f,
                 "COD Dynamic forward LUT must preserve its seed sample");
    require_near(plugin.inverse_magnitude(0.30000f), 0.59924f, 1e-6f,
                 "COD Dynamic inverse LUT must recover the stick sample");
}

void test_cod_dynamic_curve_shapes_target_t_around_reference() {
    auto request = base_request();
    request.max_force = {2.0f, 2.0f};
    request.response_curve.algorithm =
        AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    request.response_curve.calibration_reference_stick = 0.50f;

    request.error_px = {5.0f, 0.0f};  // linear target magnitude 0.20
    const auto small = solve_response_model_aim(request);
    require_near(small.pre_curve_stick.x, 0.20f, 1e-6f,
                 "curve input must remain the response-model target");
    require(small.unclamped_stick.x > small.pre_curve_stick.x,
            "Dynamic inverse must compensate the low-stick slow region");

    request.error_px = {12.5f, 0.0f};  // configured reference 0.50
    const auto reference = solve_response_model_aim(request);
    require_near(reference.unclamped_stick.x, 0.50f, 1e-5f,
                 "Dynamic correction must be identity at its calibration reference");

    request.error_px = {22.5f, 0.0f};  // linear target magnitude 0.90
    const auto large = solve_response_model_aim(request);
    require(large.unclamped_stick.x < large.pre_curve_stick.x,
            "Dynamic inverse must avoid treating high-stick gain as linear");
}

void test_cod_dynamic_curve_preserves_radial_target_direction() {
    const pipeline_contract::Vec2f input{0.30f, -0.40f};
    AimResponseCurveConfig config{};
    config.algorithm = AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    const auto output = inverse_aim_response_curve(input, config);
    require_near(output.x / -output.y, 0.75f, 1e-5f,
                 "response plugin must reshape magnitude, not target direction");
}

void test_sustaining_motion_respects_mouse_point_policy() {
    auto request = base_request();
    request.motion_is_sustaining_target_motion = true;
    request.point_tolerance_px = 4.0f;
    request.relative_velocity_px_per_sec = {100.0f, -100.0f};
    request.error_px = {-4.0f, 4.0f};
    const auto centered = solve_response_model_aim(request);
    require_near(centered.stick.x, 0.0f, 1e-6f,
        "mouse point tolerance still owns the X axis");
    require_near(centered.stick.y, 0.0f, 1e-6f,
        "mouse point tolerance still owns the Y axis");
    request.error_px = {-5.0f, 5.0f};
    const auto outside = solve_response_model_aim(request);
    require(outside.stick.x < 0.0f && outside.stick.y < 0.0f,
        "outside mouse tolerance, position retains both axis directions");
}

void test_error_rate_lookahead_cannot_claim_sustaining_ownership() {
    auto request = base_request();
    request.motion_is_sustaining_target_motion = true;
    request.motion_is_error_rate_lookahead = true;
    request.relative_velocity_px_per_sec = {100.0f, -100.0f};
    const auto output = solve_response_model_aim(request);
    require_near(output.stick.x, 0.0f, 1e-6f,
        "screen-error lookahead cannot independently drive centered X");
    require_near(output.stick.y, 0.0f, 1e-6f,
        "screen-error lookahead cannot independently drive centered Y");
}


ResponseModelAimRequest ads_floor_request(AimResponseCurveAlgorithm curve) {
    auto r = base_request();
    r.error_px = {-10.f, -5.f};
    r.response_px_per_stick_second = 1500.f;
    r.arrival_horizon_seconds = r.arrival_horizon_y_seconds = .060f;
    r.motion_weight = .012f / .060f;
    r.motion_is_error_rate_lookahead = true;
    r.range_position_response = true;
    r.position_range_px = 210.f;
    r.minimum_position_stick = .5f;
    r.arrival_radius_px = 2.f;
    r.response_curve.algorithm = curve;
    return r;
}

void test_ads_braking_survives_pursuit_floor() {
    for (auto curve : {AimResponseCurveAlgorithm::Linear, AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        auto r = ads_floor_request(curve);
        r.error_px.y = 0.f;
        float previous = std::fabs(solve_response_model_aim(r).stick.x);
        for (int n = 1; n <= 10000; ++n) {
            r.relative_velocity_px_per_sec.x = (10.f / .012f) * n / 10000.f;
            const auto o = solve_response_model_aim(r);
            const float magnitude = std::fabs(o.stick.x);
            require(o.stick.x <= 0.f, "ADS brake must not reverse the source error");
            require(magnitude <= previous + 2e-6f, "stronger braking increased delivered demand");
            require(previous - magnitude < .001f, "pursuit floor jumps at brake cancellation");
            previous = magnitude;
        }
        r.relative_velocity_px_per_sec.x = 833.32f;
        require(std::fabs(solve_response_model_aim(r).stick.x) < .001f,
                "near-cancelled ADS work was re-amplified by pursuit floor");
    }
}

void test_ads_braking_does_not_redistribute_to_other_axis() {
    for (auto curve : {AimResponseCurveAlgorithm::Linear, AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        auto r = ads_floor_request(curve);
        const auto baseline = forward_aim_response_curve(solve_response_model_aim(r).stick, r.response_curve);
        for (float rate : {300.f, 500.f, 700.f, 833.32f, 834.f}) {
            r.relative_velocity_px_per_sec.x = rate;
            const auto response = forward_aim_response_curve(solve_response_model_aim(r).stick, r.response_curve);
            require_near(response.y, baseline.y, 2e-5f,
                "X braking redistributed pursuit work into unchanged Y camera demand");
        }
    }
}

void test_ads_distant_acquisition_is_not_braked_before_saturation() {
    for (auto curve : {AimResponseCurveAlgorithm::Linear, AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
        auto r = ads_floor_request(curve);
        r.error_px = {-100.f, 0.f};
        r.relative_velocity_px_per_sec = {1000.f, 0.f};
        r.response_px_per_stick_second = 500.f;
        // Even after the existing 12ms lookahead the uncapped demand is
        // saturated. Establishing a pursuit reference must not slow this
        // distant acquisition before the ordinary range cap is enforced.
        require_near(solve_response_model_aim(r).stick.x, -std::sqrt(100.f/210.f), 1e-5f,
            "distant saturated acquisition was attenuated before lookahead");
    }
}

void test_ads_pursuit_reference_survives_inside_person_box() {
    auto r = ads_floor_request(AimResponseCurveAlgorithm::Linear);
    r.error_px = {40.f, 0.f};
    r.response_px_per_stick_second = 3000.f;
    // No approach velocity: being inside an admission/person range is not
    // evidence that the aim point has been reached or needs early braking.
    for (float radius : {120.f, 210.f, 400.f}) {
        r.position_range_px = radius;
        require_near(solve_response_model_aim(r).stick.x, .5f, 1e-6f,
            "floor removed from unsatisfied stationary point pursuit");
    }
    r.error_px = {};
    require_near(solve_response_model_aim(r).stick.x, 0.f, 1e-6f,
        "minimum pursuit must not drive an already centered stationary point");
    r.error_px = {40.f, 0.f};r.authority = 0.f;
    require_near(solve_response_model_aim(r).stick.x, 0.f, 1e-6f, "floor bypassed revoked authority");
}

}  // namespace


namespace {
void test_camera_alignment_identifies_delay_without_target_motion_leak() {
    for(int lag:{1,9,19,29}) {
        CameraResponseAlignment model;model.begin_target(1);
        const auto command=[](double t) {return pipeline_contract::Vec2f{
            float(.35*std::sin(83*t)+.12*std::cos(191*t)),float(.23*std::cos(117*t))};};
        for(int i=0;i<32;++i) {
            double t=10+i*.005;CameraResponseAlignment::Commands commands{};
            for(int d=0;d<CameraResponseAlignment::delay_count;++d)commands[d]=command(t-d*.001);
            const auto u=commands[lag];
            model.update(t,{float(75+20*(t-10))-1400*u.x,float(-30+10*(t-10))+1400*u.y},commands);
        }
        const auto e=model.estimate();require(e.valid,"excited camera must be identifiable");
        require_near(e.response,1400,1,"target drift contaminated camera gain");
        require_near(e.delay_seconds,lag*.001f,.0001f,"camera response was paired to the wrong command");
    }
    CameraResponseAlignment flat;flat.begin_target(1);CameraResponseAlignment::Commands commands{};
    for(int i=0;i<32;++i)flat.update(10+i*.005,{300.f,200.f},commands);
    require(!flat.estimate().valid,"target motion without camera excitation must not identify a camera");
}
void test_stopping_reserve_accounts_for_existing_output() {
    AimDynamicsShaper shaper;shaper.adopt({.5f,0});AimResponseCurveConfig curve;
    const auto work=shaper.stopping_work({},curve);
    require_near(work.x,.5f*.5f/(2*48),1e-6f,"shaper stopping work has wrong units");
    auto r=ads_floor_request(AimResponseCurveAlgorithm::Linear);r.error_px={3,0};r.relative_velocity_px_per_sec={};
    const auto original=solve_response_model_aim(r).stick;
    r.position_stopping_reserve_px={work.x*1500,0};
    require(original.x>0 && solve_response_model_aim(r).stick.x==0,"already committed work must consume the remaining point budget");
    shaper.adopt({.2f,-.1f});
    const auto tracking=shaper.stopping_work({.2f,-.1f},curve);
    require(tracking.x==0 && tracking.y==0,"necessary sustaining output is not excess stopping work");
}
void test_firing_does_not_erase_ads_closing_rate() {
    TargetCoordinatorConfig cfg;cfg.settle_radius_px=2;TargetCoordinator coordinator(cfg);coordinator.begin_ads_epoch(1,10);
    pipeline_contract::IntentState intent;intent.activation=pipeline_contract::AssistActivation::Engaged;
    TargetControlFeedback feedback;feedback.firing_recently=true;feedback.aim_response_px_per_stick_second=1500;
    pipeline_contract::TargetPlan plan;float errors[]={-30,-30,-22,-14,-6};
    for(int i=0;i<5;++i){
        pipeline_contract::VisionObservationBatch b;b.frame_id=i+1;b.source_time_seconds=b.publish_time_seconds=10+i*.01;b.frame_width_px=640;b.frame_height_px=512;b.capture_fresh=true;b.selector_identity_protocol=true;b.selector_target_generation=7;b.preferred_source_id=1;b.count=1;
        auto& p=b.candidates[0];p.source_id=1;p.aim_px={320+errors[i],256};p.has_aim_point=true;p.aim_region_px={p.aim_px.x-20,240,40,80};p.aim_region_source=pipeline_contract::AimRegionSource::VisionGeometry;p.has_aim_region=true;p.body_box_px=p.aim_region_px;p.has_body_box=true;p.box_size_px={40,80};p.confidence=.95f;p.reliability=.9f;p.normalized_size=.15625f;p.body_cue=true;
        plan=coordinator.update(b,intent,b.source_time_seconds,feedback);
    }
    require_near(plan.error_rate_px_per_sec.x,800,1,"firing target-motion policy erased camera closing rate");
    AdsAcquisitionControllerConfig config;config.range_position_response=true;config.minimum_position_stick=.5f;config.arrival_horizon_seconds=.06f;config.force_headroom=1;
    require(AdsAcquisitionController(config).compute(plan).x==0,"fresh closing motion must brake before crossing the point");
}
}

void register_response_model_aim_solver_tests(native_test::Registry& registry) {
    registry.add_case("BaseAds", "capture_noise_preserves_offset_motion_and_time", [] {
        CaptureEvidenceFilter filter;
        double raw_energy=0,filtered_energy=0;
        for(int i=0;i<80;++i) {
            const float noise=i%2?.3f:-.3f;
            const auto result=filter.update(1,false,false,10+i*.005,{noise,noise},{},1500,.03f);
            if(i>8) {raw_energy+=2*noise*noise;filtered_energy+=2*std::pow(noise+result.position_offset.x,2);}
        }
        require(filtered_energy<raw_energy*.85,"centered measurement noise must be reduced before solving");
        filter.reset();
        for(int i=0;i<40;++i) {
            // Camera moves +20px/s on screen, target moves -8px/s. The fit
            // preserves both the small absolute offset and current timestamp.
            const pipeline_contract::Vec2f point{1.5f+i*.005f*12,-2.f};
            const auto e=filter.update(7,false,false,20+i*.005,point,{.1f,0},1500,.03f);
            require_near(e.position_offset.x,0,.0001f,"constant target motion acquired phase lag or a dead band");
            require_near(e.position_offset.y,0,.0001f,"small static residual was classified as noise");
        }
        const auto replaced=filter.update(8,false,false,20.2,{30,-20},{},1500,.03f);
        require(replaced.position_offset.x==0 && replaced.position_offset.y==0,"new identity inherited old noise history");
        require(filter.at(7,20.2).position_offset.x==0,"old identity retained evidence");
        filter.align_interval(21);
        require(!filter.at(8,20.2).rate_available,"missing delivery interval retained old evidence");
    });
    registry.add_case("BaseAds", "credible_small_correction_is_not_output_noise", [] {
        auto cfg=incident_fixture::base_config(40,190);
        cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.ai_input_deadzone=.03f;
        cfg.ai_aim.minimum_position_stick=.5f;
        cfg.ai_aim.arrival_radius_px=2;
        cfg.ai_aim.aim_response_learning_enabled=false;
        cfg.ai_aim.ads_slow_initial_scale=cfg.ai_aim.ads_free_initial_scale=2500;
        double now=10;NativeGamepadController controller(cfg,&now);
        incident_fixture::TargetSpec spec;spec.observation_id=1;spec.selector_generation=7;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        GamepadOutputState output;
        for(int i=0;i<20;++i) {
            now=10+i*.005;
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,i+1,now,1.5f,-2.f));
            output=controller.build_output(incident_fixture::ads_input());
            // This fixture holds the observed point fixed rather than moving
            // a camera plant. Explicitly report its zero delivered work so a
            // sustaining observer cannot infer motion cancelling the proposal.
            controller.observe_delivered_output(GamepadOutputState{},true,now);
        }
        const auto request=controller.last_output_components().requested_assist_stick;
        require(request.x>0 && request.x<.03f && request.y>0 && request.y<.03f,
            "fixture must require an already-small correction on both axes");
        require(output.right_x>0 && output.right_y>0,
            "credible unsatisfied position was erased solely because output was small");
    });
    registry.add_case("BaseAds","handoff_keeps_scoped_camera_prior", [] {
        auto cfg=incident_fixture::base_config(40,190);cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.ads_slow_initial_scale=900;cfg.ai_aim.ads_free_initial_scale=1400;
        cfg.ai_aim.body_slow_initial_scale=1500;cfg.ai_aim.body_free_initial_scale=2000;
        cfg.ai_aim.aim_response_learning_enabled=false;cfg.ai_aim.ads_completion_fresh_frames=3;
        double now=10;NativeGamepadController controller(cfg,&now);incident_fixture::TargetSpec spec;
        spec.observation_id=1;spec.selector_generation=7;spec.has_enemy_cue=true;spec.enemy_identity_confirmed=true;
        float first=0;bool ads=false,body=false;
        for(int i=0;i<12;++i) {
            now=10+i*.01;controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,i+1,now,0,0));
            controller.build_output(incident_fixture::ads_input());const auto& p=controller.last_target_plan();
            ads=ads||p.mode==pipeline_contract::ControlMode::AdsAcquire;
            body=body||p.mode==pipeline_contract::ControlMode::BodyLockFollow;
            if(!i)first=p.response_scale;
            require_near(p.response_scale,first,.001f,"ADS completion changed the scoped physical camera prior");
        }
        require(ads&&body,"handoff fixture must exercise both phases");
    });
    registry.add_case("BaseBodyLock","camera_gain_change_preserves_physical_target_motion", [] {
        BodylockTargetMotionObserver observer;
        for(int i=0;i<16;++i) {
            const float response=i<8?500.f:2000.f;
            observer.update({1,10+i*.01,.01f,{100,0},{},response,1,true});
            const auto e=observer.estimate(1,10+i*.01);
            if(i>=2)require_near(e.target_motion_stick.x*e.response_px_per_stick_second,100,.01f,"mode/response calibration changed physical target speed");
        }
    });

    registry.add_case("BaseAds","camera_alignment_keeps_physical_units",test_camera_alignment_identifies_delay_without_target_motion_leak);
    registry.add_case("BaseAds","stopping_reserve_accounts_for_existing_output",test_stopping_reserve_accounts_for_existing_output);
    registry.add_case("BaseAds","firing_preserves_closing_rate",test_firing_does_not_erase_ads_closing_rate);
    registry.add_case("BaseAds", "braking_survives_pursuit_floor", test_ads_braking_survives_pursuit_floor);
    registry.add_case("BaseAds", "braking_does_not_redistribute_other_axis", test_ads_braking_does_not_redistribute_to_other_axis);
    registry.add_case("BaseAds", "distant_acquisition_keeps_saturated_pursuit", test_ads_distant_acquisition_is_not_braked_before_saturation);
    registry.add_case("BaseAds", "pursuit_reference_inside_person_box", test_ads_pursuit_reference_survives_inside_person_box);
    registry.add_case("BaseBodyLock", "sustaining_motion_respects_mouse_point_policy", test_sustaining_motion_respects_mouse_point_policy);
    registry.add_case("BaseBodyLock", "error_rate_cannot_claim_sustaining_ownership", test_error_rate_lookahead_cannot_claim_sustaining_ownership);
    registry.add_case("BaseBodyLock", "solver_radial_direction_and_y_sign", test_radial_direction_and_y_sign);
    registry.add_case("BaseBodyLock", "solver_horizon_and_response_have_units", test_horizon_and_response_have_physical_units);
    registry.add_case("BaseBodyLock", "motion_feedforward_not_multiplied_by_cap", test_motion_feedforward_is_not_multiplied_by_force_cap);
    registry.add_case("BaseBodyLock", "motion_does_not_reverse_residual", test_motion_does_not_reverse_current_residual);
    registry.add_case("BaseBodyLock", "position_bounds_only_opposing_axis_motion", test_current_position_bounds_only_opposing_axis_motion);
    registry.add_case("BaseBodyLock", "orthogonal_motion_cannot_mask_axis_reversal", test_orthogonal_motion_cannot_mask_an_axis_reversal);
    registry.add_case("BaseBodyLock", "elliptical_force_scales_one_vector", test_elliptical_force_envelope_scales_one_vector);
    registry.add_case("BaseBodyLock", "authority_caps_after_curve_inversion", test_authority_caps_delivered_force_after_curve_inversion);
    registry.add_case("BaseBodyLock", "clear_authority_does_not_multiply_mode_cap", test_clear_authority_does_not_multiply_smaller_mode_cap);
    registry.add_case("BaseBodyLock", "dynamic_plugin_round_trips_seed_curve", test_cod_dynamic_plugin_round_trips_seed_curve);
    registry.add_case("BaseBodyLock", "dynamic_curve_shapes_target_around_reference", test_cod_dynamic_curve_shapes_target_t_around_reference);
    registry.add_case("BaseBodyLock", "dynamic_curve_preserves_radial_direction", test_cod_dynamic_curve_preserves_radial_target_direction);
}
