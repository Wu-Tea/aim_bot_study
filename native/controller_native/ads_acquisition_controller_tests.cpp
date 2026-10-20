#include "ads_acquisition_controller.h"
#include "assist_control_state_machine.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <deque>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan plan_with(float error_x, float error_rate_x, float reliability = 1.0f) {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    plan.error_px.x = error_x;
    plan.error_rate_px_per_sec.x = error_rate_x;
    plan.reliability = reliability;
    plan.confidence = reliability;
    plan.aim_authority = reliability;
    plan.ads_demand = std::min(1.0f, std::fabs(error_x) / 130.0f);
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_learned_slow_camera_response_automatically_increases_ads_request() {
    controller_native::AdsAcquisitionController controller;
    auto normal = plan_with(40.0f, 0.0f);
    auto slowed = normal;
    slowed.response_scale = 250.0f;
    const auto normal_output = controller.compute(normal);
    const auto slowed_output = controller.compute(slowed);
    require_true(slowed_output.x > normal_output.x * 1.8f,
                 "slower learned camera response must increase ADS correction without range tuning");
}

void test_shorter_arrival_horizon_increases_ads_positioning_speed() {
    controller_native::AdsAcquisitionControllerConfig fast_config{};
    fast_config.arrival_horizon_seconds = 0.120f;
    controller_native::AdsAcquisitionControllerConfig slow_config{};
    slow_config.arrival_horizon_seconds = 0.240f;
    controller_native::AdsAcquisitionController fast(fast_config);
    controller_native::AdsAcquisitionController slow(slow_config);
    const auto plan = plan_with(40.0f, 0.0f);
    require_true(fast.compute(plan).x >
                     slow.compute(plan).x * 1.8f,
                 "ADS snap duration must directly control response-model arrival time");
}

void test_large_reliable_error_gets_strong_output() {
    controller_native::AdsAcquisitionController controller;
    const auto output = controller.compute(plan_with(120.0f, 0.0f));
    require_true(output.x > 0.75f, "large reliable ADS error must retain strength");
}

void test_predicted_crossing_applies_terminal_brake() {
    controller_native::AdsAcquisitionController controller;
    const auto approaching = controller.compute(plan_with(6.0f, -400.0f));
    const auto stationary = controller.compute(plan_with(6.0f, 0.0f));
    require_true(std::fabs(approaching.x) < std::fabs(stationary.x),
                 "stopping error must brake an imminent crossing");
}

void test_screen_y_error_is_converted_to_stick_y_direction() {
    controller_native::AdsAcquisitionController controller;
    auto plan = plan_with(0.0f, 0.0f);
    plan.error_px.y = 60.0f;
    const auto output = controller.compute(plan);
    require_true(output.y < 0.0f,
                 "a target below center requires negative stick Y in screen coordinates");
}

void test_close_visual_target_converges_faster_than_mid_far_target() {
    controller_native::AdsAcquisitionControllerConfig config{};
    config.arrival_horizon_seconds = 0.135f;
    controller_native::AdsAcquisitionController controller(config);
    auto mid_far = plan_with(20.0f, 0.0f);
    mid_far.normalized_size = 0.12f;
    auto close = mid_far;
    close.normalized_size = 0.42f;
    const auto mid_far_output = controller.compute(mid_far);
    const auto close_output = controller.compute(close);
    const float close_to_mid_far = close_output.x / mid_far_output.x;
    require_true(
        close_to_mid_far >= 1.45f,
        "close target must use a shorter ADS arrival horizon than mid/far target");
    require_true(
        close_to_mid_far <= 1.55f,
        "close target must not restore the retired near-instant transfer speed");

    auto just_below_transition = mid_far;
    just_below_transition.normalized_size = 0.179f;
    auto just_above_transition = just_below_transition;
    just_above_transition.normalized_size = 0.181f;
    const float below = controller.compute(just_below_transition).x;
    const float above = controller.compute(just_above_transition).x;
    require_true(
        std::fabs(above - below) < 0.01f,
        "close-target ADS timing must change continuously without a range step");
}

struct VerticalConvergenceMetrics {
    float target_above_output = 0.0f;
    float target_below_output = 0.0f;
};

VerticalConvergenceMetrics measure_vertical_convergence() {
    controller_native::AdsAcquisitionControllerConfig config{};
    config.arrival_horizon_seconds = 0.135f;
    controller_native::AdsAcquisitionController controller(config);
    auto target_above = plan_with(0.0f, 0.0f);
    target_above.error_px.y = -50.0f;
    auto target_below = target_above;
    target_below.error_px.y = 50.0f;
    return {
        std::fabs(controller.compute(target_above).y),
        std::fabs(controller.compute(target_below).y),
    };
}

void write_vertical_convergence_report(
    const std::string& path,
    const VerticalConvergenceMetrics& metrics) {
    if (path.empty()) return;
    std::ofstream output(path, std::ios::trunc);
    output
        << "{\n"
        << "  \"incident_id\": \"ads-upward-convergence-20260809\",\n"
        << "  \"target_above_output\": " << metrics.target_above_output << ",\n"
        << "  \"target_below_counterfactual_output\": "
        << metrics.target_below_output << ",\n"
        << "  \"above_to_below_ratio\": "
        << metrics.target_above_output / std::max(1.0e-6f, metrics.target_below_output)
        << "\n}\n";
}

void test_target_above_gets_bounded_directional_urgency() {
    const auto metrics = measure_vertical_convergence();
    require_true(
        metrics.target_above_output >= metrics.target_below_output * 1.12f,
        "telemetry-proven slow upward ADS path needs at least 12% directional urgency");
    require_true(
        metrics.target_above_output <= metrics.target_below_output * 1.25f,
        "upward urgency must stay bounded instead of becoming a global force increase");
}

void test_reliability_does_not_create_a_second_ads_gain_policy() {
    controller_native::AdsAcquisitionController controller;
    const auto full_evidence = plan_with(80.0f, 0.0f, 1.0f);
    auto admitted_low_evidence = plan_with(80.0f, 0.0f, 0.15f);
    // Admission and authority belong to TargetCoordinator. Once it admits an
    // ADS plan, this controller must honor that authority without multiplying
    // it by confidence/reliability again.
    admitted_low_evidence.aim_authority = 1.0f;
    const float expected = controller.compute(full_evidence).x;
    const float actual = controller.compute(admitted_low_evidence).x;
    require_true(std::fabs(actual - expected) < 0.0001f,
                 "admitted ADS must not be weakened by reliability");
}

void test_september_ads_precenter_brake(const native_test::TestContext& context) {
    // Frozen owner inputs: an approaching source crosses within 6 or 24 ms.
    // Test camera-response demand, independent of the inverse stick curve.
    // This is a solver invariant, not a calibrated replay of the game plant.
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("ads-precenter-brake.json"));
    report << std::setprecision(9) << "{\"cases\":[";
    float max_terminal = 0.0f, max_fraction = 0.0f;
    int cases = 0, counter_failures = 0;
    for (int axis : {0, 1}) for (float sign : {-1.0f, 1.0f})
    for (float response : {500.0f, 650.0f, 1100.0f})
    for (float error : {6.0f, 16.0f, 24.0f})
    for (float size : {0.1f, 0.42f})
    for (float crossing_seconds : {0.006f, 0.024f}) {
        controller_native::AdsAcquisitionControllerConfig config;
        config.arrival_horizon_seconds = 0.135f;
        config.response_curve.algorithm =
            controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
        controller_native::AdsAcquisitionController controller(config);
        auto plan = plan_with(0.0f, 0.0f);
        plan.response_scale = response;
        plan.normalized_size = size;
        const auto vector = [axis](float value) {
            return axis == 0 ? pipeline_contract::Vec2f{value, 0.0f}
                             : pipeline_contract::Vec2f{0.0f, value};
        };
        const auto component = [axis](pipeline_contract::Vec2f value) {
            return axis == 0 ? value.x : value.y;
        };
        plan.error_px = vector(sign * error);
        const auto stationary = controller.compute(plan);
        plan.error_rate_px_per_sec = vector(-sign * error / crossing_seconds);
        const auto approaching = controller.compute(plan);
        plan.error_rate_px_per_sec = vector(sign * error / crossing_seconds);
        const auto receding = controller.compute(plan);
        const float baseline = std::fabs(component(controller_native::forward_aim_response_curve(
            stationary, config.response_curve)));
        if (!(baseline > 1.0e-5f)) native_test::invalid_fixture("nonzero stationary demand required");
        const float fraction = std::fabs(component(controller_native::forward_aim_response_curve(
            approaching, config.response_curve))) / baseline;
        const float terminal = std::fabs(component(approaching));
        if (crossing_seconds < 0.01f) max_terminal = std::max(max_terminal, terminal);
        else max_fraction = std::max(max_fraction, fraction);
        const bool counters = component(approaching) * component(stationary) >= -1.0e-7f &&
            std::fabs(component(receding)) + 1.0e-6f >= std::fabs(component(stationary)) &&
            component(stationary) * sign * (axis == 0 ? 1.0f : -1.0f) > 0.0f &&
            terminal <= 1.0f;
        counter_failures += counters ? 0 : 1;
        report << (cases++ ? "," : "") << "{\"axis\":" << axis
               << ",\"sign\":" << sign << ",\"response\":" << response
               << ",\"error\":" << error << ",\"size\":" << size
               << ",\"crossing_seconds\":" << crossing_seconds
               << ",\"stationary\":" << component(stationary)
               << ",\"approaching\":" << component(approaching)
               << ",\"receding\":" << component(receding)
               << ",\"response_fraction\":" << fraction << "}";
    }
    report << "],\"case_count\":" << cases << ",\"max_terminal\":" << max_terminal
           << ",\"max_fraction\":" << max_fraction
           << ",\"counter_failures\":" << counter_failures << "}\n";
    report.close();
    if (cases != 144) native_test::invalid_fixture("all 144 frozen approach cases required");
    require_true(counter_failures == 0, "stationary/receding/sign/budget controls must hold");
    require_true(max_terminal <= 1.0e-5f,
                 "lookahead crossing within 6 ms must discharge ADS position demand");
    require_true(max_fraction <= 0.60f,
                 "24 ms approach must retain the braking contribution before center");
}

void test_centered_ads_retains_qualified_motion_under_original_budget() {
    using namespace controller_native;
    AdsAcquisitionControllerConfig cfg;
    cfg.range_position_response = true;
    cfg.arrival_horizon_seconds = .060f;
    cfg.minimum_position_stick = .5f;
    cfg.force_headroom = 1.f;
    cfg.response_curve.algorithm = AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    AdsAcquisitionController controller(cfg);
    for (float authority : {.15f, .4f, 1.f}) {
        auto p = plan_with(0, 0, authority);
        p.position_response_radius_px = 190;
        p.position_arrival_radius_px = 2;
        p.bodylock_target_motion_valid = true;
        const auto velocity = forward_aim_response_curve({authority*.5f, -authority*.25f}, cfg.response_curve);
        p.bodylock_target_motion_px_per_sec = {velocity.x*p.response_scale, -velocity.y*p.response_scale};
        const auto output = controller.compute(p);
        const auto delivered = forward_aim_response_curve(output, cfg.response_curve);
        require_true(std::hypot(delivered.x-velocity.x,delivered.y-velocity.y)<1e-5f,
            "ADS must sustain feasible centered motion without requiring a permanent position error");
        p.bodylock_target_motion_px_per_sec = {10000,10000};
        const auto capped = controller.compute(p);
        require_true(std::hypot(capped.x,capped.y)<=authority+1e-5f,
            "sustaining motion must spend the original ADS authority envelope");
        p.bodylock_target_motion_valid = false;
        require_true(std::hypot(controller.compute(p).x,controller.compute(p).y)==0,
            "unqualified motion cannot create centered work");
    }
}

void test_ads_carried_manual_spends_remaining_work_without_restoring_brake() {
    using namespace controller_native;
    AdsAcquisitionControllerConfig cfg;
    cfg.range_position_response=true;
    cfg.response_curve.algorithm=AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    AdsAcquisitionController solver(cfg);
    AssistControlStateMachineConfig arb_config;
    arb_config.use_gamepad_intent_for_arbitration=true;
    auto p=plan_with(20,0);
    p.position_response_radius_px=190;p.position_arrival_radius_px=2;
    const auto solve=[&](pipeline_contract::Vec2f manual,pipeline_contract::Vec2f ai,float weight) {
        AssistControlStateMachine arb(arb_config);
        AssistControlStateMachineInput in;
        in.activation=pipeline_contract::AssistActivation::Engaged;
        in.target_authoritative=in.fresh_observation=true;
        in.target_id=1;in.mode=p.mode;
        in.ads_acquisition_state=pipeline_contract::AdsAcquisitionState::AcquiringNominal;
        in.manual_stick=in.filtered_manual_stick=manual;
        in.ai_stick=ai;in.manual_axis_activity={weight,weight};
        in.ads_manual_work=solver.cooperative_manual_work(p,manual,ai);
        return arb.update(in).stick;
    };
    require_true(std::fabs(solve({.3f,0},{.1f,0},1).x-.3f)<1e-5f,
        "sustained aligned manual must participate before ADS timeout when work is feasible");
    require_true(std::fabs(solve({.3f,0},{.1f,0},0).x-.1f)<1e-6f,
        "work evidence must not grant authority to subthreshold manual noise");
    require_true(std::fabs(solve({.3f,0},{.1f,0},.5f).x-.2f)<1e-5f,
        "cooperation must consume the existing intent weight exactly once");
    p.position_stopping_reserve_px={20,0};
    require_true(solve({.3f,0},{.01f,0},1).x==.01f,
        "carried manual must not refill work already committed to arrival");
    p.position_stopping_reserve_px={};p.error_px={.2f,0};
    const auto bounded=solve({.8f,0},{.001f,0},1);
    require_true(bounded.x<.03f && bounded.x>=.001f,
        "small remaining distance must bound manual cooperation, not select the larger output");
    p.error_px={20,0};
    require_true(solve({-.3f,0},{.1f,0},1).x==.1f,
        "opposing carry must not be relabeled as same-direction work");
    // Real-session 981.291 s: aligned held X was replaced by a smaller ADS X
    // while Y also needed correction. Keep the joint response curve in play.
    p.error_px={21.5835f,-62.f};p.response_scale=900;
    const auto live=solve({.451f,0},{.174f,.4f},1);
    require_true(live.x>.174f && live.x<=.451f && live.y==.4f,
        "live-shaped horizontal cooperation must not steal the other axis's proposal");
    p.observation_gap=true;
    require_true(solver.cooperative_manual_work(p,{.451f,0},{.174f,.4f}).x==0,
        "a gap must not authorize new ADS manual suppression or assistance");
}

void test_physical_camera_fit_distinguishes_known_manual_from_weapon_disturbance() {
    using namespace controller_native;
    namespace f=incident_fixture;
    for(bool firing : {false,true}) {
        auto cfg=f::base_config(40,190);
        cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.minimum_position_stick=.5f;cfg.ai_aim.arrival_radius_px=2;
        cfg.ai_aim.ads_response_time_ms=60;
        cfg.ai_aim.ads_free_initial_scale=cfg.ai_aim.ads_slow_initial_scale=900;
        cfg.ai_aim.bodylock_output_limit_x=cfg.ai_aim.bodylock_output_limit_y=1;
        cfg.ai_aim.ads_output_limit_x=cfg.ai_aim.ads_output_limit_y=1;
        cfg.ai_aim.bodylock_response_time_x_ms=cfg.ai_aim.bodylock_response_time_y_ms=10;
        cfg.ai_aim.body_lock_activation_box_px=120;
        cfg.ai_aim.body_free_initial_scale=2000;cfg.ai_aim.body_slow_initial_scale=1500;
        cfg.aim_response_curve.algorithm=AimResponseCurveAlgorithm::CodDynamicLegacyLut;
        cfg.auto_fire.enabled=false;cfg.recoil.enabled=false;
        cfg.output_transfer.enabled=false;
        double now=10;NativeGamepadController c(cfg,&now);
        std::deque<pipeline_contract::Vec2f> plant(22);
        float x=20,y=-18;bool identified=false;float best_response_error=1e9f;
        for(int tick=0;tick<1500;++tick) {
            now=10+tick*.001;
            if(tick%5==0) {
                f::TargetSpec spec;spec.observation_id=tick+1;spec.selector_generation=1;
                spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
                c.submit_vision_snapshot(f::observed_snapshot(spec,tick+1,now,x,y));
            }
            auto input=f::ads_input(.392f,0,firing);
            auto out=c.build_output(input);
            identified=identified||c.last_output_components().camera_alignment_valid;
            if(c.last_output_components().camera_alignment_valid)
                best_response_error=std::min(best_response_error,std::fabs(c.last_target_plan().response_scale-1500));
            c.observe_delivered_output(out,true,now);
            auto delivered=plant.front();plant.pop_front();
            plant.push_back(forward_aim_response_curve({out.right_x,out.right_y},cfg.aim_response_curve));
            x+=(600-1500*delivered.x)*.001f;y+=1500*delivered.y*.001f;
        }
        require_true(firing ? !identified : identified,
            "physical fit must learn from known delivered manual work but reject weapon-disturbed intervals");
        if(!firing) require_true(best_response_error<80,
            "held manual must not freeze the physical camera on an incorrect startup prior");
        if(!firing) require_true(std::hypot(x,y)<2.f,
            "feasible fast motion with held cooperation must reach the point despite a wrong startup prior");
    }
}

}  // namespace

void register_ads_acquisition_controller_tests(native_test::Registry& registry) {
    registry.add_case("BaseAds", "centered_qualified_motion", test_centered_ads_retains_qualified_motion_under_original_budget);
    registry.add_case("BaseAds", "carried_manual_remaining_work", test_ads_carried_manual_spends_remaining_work_without_restoring_brake);
    registry.add_case("BaseAds", "physical_fit_known_manual", test_physical_camera_fit_distinguishes_known_manual_from_weapon_disturbance);
    registry.add_context_case("BaseAds", "september_ads_precenter_brake", test_september_ads_precenter_brake);
    registry.add_case("BaseAds", "large_reliable_error_gets_strong_output", test_large_reliable_error_gets_strong_output);
    registry.add_case("BaseAds", "predicted_crossing_applies_terminal_brake", test_predicted_crossing_applies_terminal_brake);
    registry.add_case("BaseAds", "screen_y_error_converts_to_stick_direction", test_screen_y_error_is_converted_to_stick_y_direction);
    registry.add_case("BaseAds", "target_above_gets_bounded_urgency", test_target_above_gets_bounded_directional_urgency);
    registry.add_case("BaseAds", "learned_slow_response_increases_request", test_learned_slow_camera_response_automatically_increases_ads_request);
    registry.add_case("BaseAds", "shorter_arrival_horizon_increases_speed", test_shorter_arrival_horizon_increases_ads_positioning_speed);
    registry.add_case("BaseAds", "close_target_converges_faster_than_mid_far", test_close_visual_target_converges_faster_than_mid_far_target);
    registry.add_case("BaseAds", "reliability_does_not_create_second_gain_policy", test_reliability_does_not_create_a_second_ads_gain_policy);
}
