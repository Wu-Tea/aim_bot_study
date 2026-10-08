#include "bodylock_follow_controller.h"
#include "ads_acquisition_controller.h"
#include "point_boundary_simulation.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <deque>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <random>
#include <vector>

namespace {

void require_true(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 0.0001f) {
    return std::fabs(left - right) <= tolerance;
}

pipeline_contract::TargetPlan active_plan(float error_x, float error_y) {
    pipeline_contract::TargetPlan plan;
    plan.target_id = 1;
    plan.position_response_radius_px = 150.f;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.error_px = {error_x, error_y};
    plan.aim_authority = 1.0f;
    plan.reliability = 1.0f;
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_inactive_plan_is_neutral() {
    controller_native::BodylockFollowController controller;
    pipeline_contract::TargetPlan plan;
    const auto output = controller.compute(plan, {}, 0.001f);
    require_true(near(output.x, 0.0f) && near(output.y, 0.0f),
                 "inactive plan must produce no target proposal");
}

void test_current_error_owns_position_proposal() {
    controller_native::BodylockFollowController controller;
    const auto right = controller.compute(active_plan(24.0f, 0.0f), {}, 0.001f);
    const auto below = controller.compute(active_plan(0.0f, 24.0f), {}, 0.001f);
    require_true(right.x > 0.0f && near(right.y, 0.0f),
                 "right-side source point must request right stick");
    require_true(below.y < 0.0f && near(below.x, 0.0f),
                 "screen-down source point must request negative stick Y");
}

void test_cue_lifecycle_uses_same_source_owned_solve() {
    controller_native::BodylockFollowController controller;
    auto observed = active_plan(18.0f, -12.0f);
    auto cue = observed;
    cue.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto observed_output = controller.compute(observed, {}, 0.001f);
    const auto cue_output = controller.compute(cue, {}, 0.001f);
    require_true(near(observed_output.x, cue_output.x) &&
                     near(observed_output.y, cue_output.y),
                 "BodyLock must not maintain fresh/non-fresh alternate solves");
}

void test_manual_input_does_not_create_a_second_authority_policy() {
    controller_native::BodylockFollowController controller;
    const auto plan = active_plan(30.0f, 0.0f);
    pipeline_contract::IntentState opposing;
    opposing.filtered_right.x = -1.0f;
    opposing.right_x.confidence = 1.0f;
    opposing.right_confidence = 1.0f;
    const auto neutral = controller.compute(plan, {}, 0.001f);
    const auto with_manual = controller.compute(plan, opposing, 0.001f);
    require_true(near(neutral.x, with_manual.x) && near(neutral.y, with_manual.y),
                 "manual authority must be owned after BodyLock by the state machine");
}

void test_motion_cannot_reverse_current_position_axis() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(-10.0f, 0.0f);
    plan.error_rate_px_per_sec = {260.0f, 120.0f};
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    require_true(output.radial_motion_bound_applied,
                 "opposing motion must exercise the current-position bound");
    require_true(output.constraint_reason ==
                     controller_native::ResponseModelConstraintReason::
                         PositionRadialMotionBound,
                 "position-motion bound must expose its single-path reason");
    require_true(output.stick.x < 0.0f,
                 "motion metadata must not reverse a material current error");
    require_true(near(output.effective_motion_stick.y, 0.0f),
                 "unconfirmed screen rate cannot own a centered orthogonal axis");
}

void test_orthogonal_motion_cannot_mask_bodylock_axis_reversal() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(-7.5f, -18.5f);
    plan.error_rate_px_per_sec = {190.0f, -250.0f};
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    const float vector_dot =
        output.position_stick.x * output.motion_stick.x +
        output.position_stick.y * output.motion_stick.y;

    require_true(output.position_stick.x * output.motion_stick.x < 0.0f,
                 "fixture must contain an X position-motion conflict");
    require_true(vector_dot > 0.0f,
                 "orthogonal motion must mask the old vector-wide conflict test");
    require_true(output.radial_motion_bound_applied,
                 "BodyLock must constrain the conflict on the affected axis");
    require_true(output.stick.x * output.position_stick.x >= 0.0f,
                 "BodyLock output must preserve the current X error direction");
    require_true(near(output.effective_motion_stick.y, output.motion_stick.y),
                 "BodyLock must retain compatible orthogonal feed-forward");
}

void test_force_envelope_remains_bounded() {
    controller_native::BodylockFollowControllerConfig config;
    config.max_force_x = 0.30f;
    config.max_force_y = 0.20f;
    controller_native::BodylockFollowController controller(config);
    const auto output = controller.compute(active_plan(300.0f, 300.0f), {}, 0.001f);
    const float ellipse = std::hypot(output.x / 0.30f, output.y / 0.20f);
    require_true(ellipse <= 1.0001f,
                 "BodyLock target proposal must stay inside its force ellipse");
}

void test_aligned_target_motion_replaces_screen_relative_hint() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(0.0f, 0.0f);
    plan.error_rate_px_per_sec = {0.0f, 0.0f};
    plan.bodylock_target_motion_px_per_sec = {200.0f, 0.0f};
    plan.bodylock_target_motion_confidence = 0.8f;
    plan.bodylock_target_motion_valid = true;
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    require_true(near(output.error_rate_px_per_sec.x, 200.0f),
                 "BodyLock must expose the aligned target-motion demand");
    require_true(near(output.motion_stick.x, 0.4f),
                 "target motion must be a full sustaining total, not a 0.72 hint");
}

// Log-derived local invariant, not a replay of the recorded game/plant.
// Freeze before the production repair: continuous work through e=0, nonzero
// sustaining demand, and no centered work from untrusted screen-rate noise.
void test_center_crossing_incident(const native_test::TestContext& context) {
    float maximum_crossing_step = 0.0f;
    float minimum_sustaining_ratio = 1.0f;
    float maximum_untrusted_jitter = 0.0f;
    float maximum_static_jitter = 0.0f;
    int crossings = 0;
    int far_position_controls = 0;
    int reversal_controls = 0;
    int lifecycle_controls = 0;
    int zero_motion_controls = 0;
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("bodylock-center-crossing.json"));
    report << std::setprecision(9) << "{\n\"samples\":[";
    bool first = true;
    for (const bool dynamic : {false, true}) {
        controller_native::BodylockFollowControllerConfig config;
        config.max_force_x = 0.60f;
        config.max_force_y = 0.66f;
        config.feedback_range_x_px = config.feedback_range_y_px = 24.0f;
        config.feedforward_gain = 0.72f;
        config.response_curve.algorithm = dynamic
            ? controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut
            : controller_native::AimResponseCurveAlgorithm::Linear;
        controller_native::BodylockFollowController controller(config);
        for (const bool y_axis : {false, true}) {
            const auto axis = [y_axis](pipeline_contract::Vec2f value) {
                return y_axis ? -value.y : value.x; // screen coordinate
            };
            for (const float direction : {-1.0f, 1.0f}) {
                for (const float authority : {0.65f, 1.0f}) {
                    auto plan = active_plan(0.0f, 0.0f);
                    plan.selector_target_generation = 340;
                    plan.physical_ads_epoch = 148;
                    plan.direct_person_observation = true;
                    plan.aim_authority = authority;
                    plan.bodylock_target_motion_valid = true;
                    plan.bodylock_target_motion_confidence = 0.8f;
                    plan.bodylock_target_motion_px_per_sec = y_axis
                        ? pipeline_contract::Vec2f{0.0f, direction * 100.0f}
                        : pipeline_contract::Vec2f{direction * 100.0f, 0.0f};
                    const auto set_error = [&](float value) {
                        plan.error_px = y_axis
                            ? pipeline_contract::Vec2f{0.0f, value}
                            : pipeline_contract::Vec2f{value, 0.0f};
                    };
                    const float centered = axis(controller.compute(plan, {}, 0.001f));
                    require_true(centered * direction > 0.01f,
                        "trigger requires nonzero sustaining work at center");
                    float previous = 0.0f;
                    bool have_previous = false;
                    for (const float error : {-0.001f, 0.0f, 0.001f}) {
                        set_error(error);
                        const auto result = controller.compute_detailed(plan, {}, 0.001f);
                        require_true(error == 0.0f || std::fabs(axis(result.position_stick)) > 1e-5f,
                            "nonzero probes must lie outside the known-bad exact-zero exception");
                        const float value = axis(result.stick);
                        require_true(std::fabs(axis(result.motion_stick) - direction * 0.2f) < 1e-6f,
                            "trigger must hold total motion fixed on both sides of zero");
                        if (have_previous) maximum_crossing_step = std::max(
                            maximum_crossing_step, std::fabs(value - previous));
                        previous = value;
                        have_previous = true;
                        if (!first) report << ',';
                        first = false;
                        report << "{\"dynamic\":" << dynamic << ",\"y_axis\":" << y_axis
                               << ",\"direction\":" << direction << ",\"authority\":" << authority
                               << ",\"error\":" << error << ",\"request\":" << value << '}';
                    }
                    ++crossings;
                    for (const float error : {-1.0f, 0.0f, 1.0f}) {
                        set_error(error);
                        minimum_sustaining_ratio = std::min(minimum_sustaining_ratio,
                            axis(controller.compute(plan, {}, 0.001f)) / centered);
                    }
                    set_error(-direction * 20.0f);
                    if (axis(controller.compute(plan, {}, 0.001f)) * direction < 0.0f)
                        ++far_position_controls;
                    set_error(0.0f);
                    plan.bodylock_target_motion_px_per_sec = y_axis
                        ? pipeline_contract::Vec2f{0.0f, -direction * 100.0f}
                        : pipeline_contract::Vec2f{-direction * 100.0f, 0.0f};
                    if (axis(controller.compute(plan, {}, 0.001f)) * direction < -0.01f)
                        ++reversal_controls;
                    plan.lifecycle = pipeline_contract::TargetLifecycle::None;
                    if (axis(controller.compute(plan, {}, 0.001f)) == 0.0f)
                        ++lifecycle_controls;
                    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
                    plan.bodylock_target_motion_px_per_sec = {};
                    if (axis(controller.compute(plan, {}, 0.001f)) == 0.0f)
                        ++zero_motion_controls;
                    plan.bodylock_target_motion_valid = false;
                    for (const float error : {-0.25f, 0.0f, 0.25f}) {
                        set_error(error);
                        plan.error_rate_px_per_sec = {};
                        maximum_static_jitter = std::max(maximum_static_jitter,
                            std::fabs(axis(controller.compute(plan, {}, 0.001f))));
                        // Screen-rate disturbance does not establish target velocity.
                        plan.error_rate_px_per_sec = y_axis
                            ? pipeline_contract::Vec2f{0.0f, direction * 100.0f}
                            : pipeline_contract::Vec2f{direction * 100.0f, 0.0f};
                        maximum_untrusted_jitter = std::max(maximum_untrusted_jitter,
                            std::fabs(axis(controller.compute(plan, {}, 0.001f))));
                    }
                }
            }
        }
    }
    const bool controls_valid = crossings == 16 && far_position_controls == 16 &&
        reversal_controls == 16 && lifecycle_controls == 16 && zero_motion_controls == 16;
    report << "],\n\"trigger_count\":" << crossings
           << ",\n\"counterfactuals_valid\":" << std::boolalpha << controls_valid
           << ",\n\"maximum_crossing_step\":" << maximum_crossing_step
           << ",\n\"minimum_sustaining_ratio\":" << minimum_sustaining_ratio
           << ",\n\"maximum_untrusted_jitter\":" << maximum_untrusted_jitter
           << ",\n\"maximum_static_jitter\":" << maximum_static_jitter << "\n}\n";
    report.close();
    require_true(controls_valid, "incident trigger and negative controls must execute");
    require_true(maximum_crossing_step <= 0.001f && minimum_sustaining_ratio >= 0.5f &&
                     maximum_untrusted_jitter <= 0.03f && maximum_static_jitter <= 0.03f,
                 "BodyLock crossing/noise contract failed; see measured incident artifact");
}

void test_feedback_distance_delay_noise_sweep(const native_test::TestContext& context) {
    // A component-level ideal game plant, not live-game acceptance. The real
    // controller solves at 1 kHz; sampled observations have explicit delay,
    // noise and cadence. No Vision/identity/manual/weapon model is invented.
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("follow-response-sweep.csv"));
    require_true(report.good(), "response diagnostic artifact must be writable");
    report << "seed,case,ms,delay_ms,capture_ms,noise_px,plant_scale,moving,curve,distance_px,force,mean_abs_error_px,rms_error_px,peak_error_px,center_crossings\n";
    for (unsigned seed : {5071u,90439u}) {
        std::mt19937 random(seed);
        auto unit=[&]() { return static_cast<float>(random()%10001)/10000.f; };
        for (int scenario=0;scenario<20;++scenario) {
            const int duration=scenario%2 ? 5000 : 1000;
            const int delay=4+random()%21, cadence=4+random()%9;
            const float noise=.02f+unit()*.98f, plant=500.f*(.8f+unit()*.4f);
            const bool moving=scenario%3!=0;
            const float amplitude=6.f+unit()*14.f, frequency=.7f+unit()*1.6f;
            for (auto curve : {controller_native::AimResponseCurveAlgorithm::Linear,
                               controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut,
                               controller_native::AimResponseCurveAlgorithm::CustomLut}) {
                for (float distance : {1.f,6.f,18.f,36.f}) {
                    for (float force : {.3f,.6f,1.f}) {
                        // Reuse precisely the same observation-noise stream
                        // across parameter candidates for each scenario.
                        std::mt19937 sensor(seed+scenario*101);
                        controller_native::BodylockFollowControllerConfig config;
                        config.feedback_range_x_px=config.feedback_range_y_px=distance;
                        config.max_force_x=force; config.max_force_y=force*.8f;
                        config.response_curve.algorithm=curve;
                        if (curve==controller_native::AimResponseCurveAlgorithm::CustomLut) {
                            config.response_curve.custom_count=4;
                            config.response_curve.custom_stick={0,.2f,.5f,1};
                            config.response_curve.custom_response={0,.08f,.37f,1};
                        }
                        controller_native::BodylockFollowController controller(config);
                        pipeline_contract::Vec2f camera{};
                        std::vector<pipeline_contract::Vec2f> history;
                        auto plan=active_plan(8,4);
                        double sum=0,squares=0; float peak=0; unsigned measured=0,crossings=0;
                        int last_sign=1;
                        for (int tick=0;tick<duration;++tick) {
                            const float t=tick*.001f, phase=t*frequency;
                            const pipeline_contract::Vec2f target{8+(moving ? amplitude*std::sin(phase) : 0),
                                4+(moving ? amplitude*.4f*std::sin(phase*.7f) : 0)};
                            const pipeline_contract::Vec2f error{target.x-camera.x,target.y-camera.y};
                            history.push_back(error);
                            if (tick%cadence==0) {
                                const auto observed=history[std::max(0,tick-delay)];
                                auto jitter=[&]() { return (static_cast<float>(sensor()%10001)/5000.f-1)*noise; };
                                plan.error_px={observed.x+jitter(),observed.y+jitter()};
                                plan.bodylock_target_motion_valid=true;
                                const float observed_t=std::max(0,tick-delay)*.001f;
                                plan.bodylock_target_motion_px_per_sec={
                                    moving ? amplitude*frequency*std::cos(observed_t*frequency) : 0,
                                    moving ? amplitude*.4f*frequency*.7f*std::cos(observed_t*frequency*.7f) : 0};
                            }
                            const auto out=controller.compute_detailed(plan,{},.001f);
                            const float ellipse=std::hypot(out.stick.x/force,out.stick.y/(force*.8f));
                            require_true(std::isfinite(out.stick.x) && std::isfinite(out.stick.y) && ellipse<=1.00001f,
                                         "every response candidate must obey finite joint force limits");
                            const auto effective=controller_native::forward_aim_response_curve(out.stick,config.response_curve);
                            camera.x+=effective.x*plant*.001f;
                            camera.y-=effective.y*plant*.001f;
                            if (tick>=duration/5) {
                                const float magnitude=std::hypot(error.x,error.y);
                                sum+=magnitude;squares+=magnitude*magnitude;peak=std::max(peak,magnitude);++measured;
                                const int sign=error.x>.05f ? 1 : error.x<-.05f ? -1 : 0;
                                if (sign && sign!=last_sign) { ++crossings;last_sign=sign; }
                            }
                        }
                        report << seed << ',' << scenario << ',' << duration << ',' << delay << ',' << cadence << ','
                               << noise << ',' << plant << ',' << moving << ',' << static_cast<int>(curve) << ','
                               << distance << ',' << force << ',' << sum/measured << ',' << std::sqrt(squares/measured)
                               << ',' << peak << ',' << crossings << '\n';
                    }
                }
            }
        }
    }
}

}  // namespace

void register_bodylock_follow_controller_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "velocity_feedback_delay_curve_and_geometry_ownership", [] {
        using namespace controller_native;
        for (auto curve : {AimResponseCurveAlgorithm::Linear, AimResponseCurveAlgorithm::CodDynamicLegacyLut}) {
            BodylockFollowControllerConfig config;
            config.range_position_response = true;
            config.response_time_x_seconds = config.response_time_y_seconds = .005f;
            config.response_curve.algorithm = curve;
            config.max_force_x = config.max_force_y = .8f;
            BodylockFollowController controller(config);
            auto plan = active_plan(1, 0);
            plan.response_scale = 1600;
            plan.position_arrival_radius_px = 2;
            const auto fresh = controller.compute(plan, {}, .001f);
            plan.source_capture_age_ms = 40;
            const auto delayed = controller.compute(plan, {}, .001f);
            const auto speed = forward_aim_response_curve(delayed, config.response_curve);
            const float budget_seconds = 2.f * (.040f + config.response_effect_delay_seconds);
            require_true(speed.x * plan.response_scale <= 1.f / budget_seconds + .001f,
                "the nonlinear inverse cannot bypass the delayed camera-velocity budget");
            require_true(delayed.x > 0 && delayed.x < fresh.x,
                "older feedback lowers correction gain without adding an output wait");
            plan.position_response_radius_px = 300;
            require_true(near(controller.compute(plan, {}, .001f).x, delayed.x),
                "search geometry is not a hidden BodyLock velocity gain");
            plan.error_px = {};
            plan.bodylock_target_motion_valid = true;
            plan.bodylock_target_motion_px_per_sec = {90, 0};
            const auto following = forward_aim_response_curve(controller.compute(plan, {}, .001f), config.response_curve);
            require_true(near(following.x * plan.response_scale, 90, .002f),
                "feedback damping does not slow confirmed centered target motion");
            plan.bodylock_target_motion_valid = false;
            plan.error_rate_px_per_sec = {500, 500};
            const auto unconfirmed = controller.compute(plan, {}, .001f);
            require_true(unconfirmed.x == 0 && unconfirmed.y == 0,
                "unconfirmed screen rate cannot create centered motion authority");
            plan.error_px = {.3f, 0};
            plan.error_rate_px_per_sec = {-100, 0};
            require_true(controller.compute(plan, {}, .001f).x >= 0,
                "unconfirmed motion must not reverse the delay-limited position correction");
        }
    });
    registry.add_case("BaseBodyLock", "delayed_stationary_target_converges_without_limit_cycle", [] {
        using namespace controller_native;
        BodylockFollowControllerConfig config;
        config.range_position_response = true;
        config.response_time_x_seconds = config.response_time_y_seconds = .180f;
        config.max_force_x = config.max_force_y = .30f;
        config.minimum_position_stick = .20f;
        BodylockFollowController controller(config);
        AimDynamicsShaper shaper;
        auto plan = active_plan(30, 0);
        plan.response_scale = 938;
        plan.position_arrival_radius_px = 2;
        plan.bodylock_target_motion_valid = true;
        struct Capture { int tick; float error; };
        std::deque<Capture> pending;
        std::vector<float> outputs(4000);
        float camera = 0, last_nonzero = 0;
        int next_capture = 0, capture_tick = -1, reversals = 0;
        double error_sum = 0;
        for (int tick = 0; tick < 4000; ++tick) {
            if (tick > 23) camera += outputs[tick-24] * 938.f * .001f;
            if (tick * 90 >= next_capture * 1000) {
                pending.push_back({tick, 30-camera});
                ++next_capture;
            }
            while (!pending.empty() && pending.front().tick + 17 <= tick) {
                capture_tick = pending.front().tick;
                plan.error_px = {pending.front().error, 0};
                pending.pop_front();
            }
            if (capture_tick < 0) continue;
            plan.source_capture_age_ms = static_cast<float>(tick-capture_tick);
            const auto request = controller.compute(plan, {}, .001f);
            const auto value = filter_ai_input(shaper.shape(request, {}, plan, .001f), .03f);
            const float quantizer = value.x >= 0 ? 127.f : 128.f;
            outputs[tick] = std::round(value.x * quantizer) / quantizer;
            require_true(std::isfinite(outputs[tick]) && std::abs(outputs[tick]) <= .305f,
                "delayed loop must respect the quantized output envelope");
            if (tick >= 3000) {
                error_sum += std::abs(30-camera);
                if (outputs[tick] != 0) {
                    reversals += last_nonzero * outputs[tick] < 0;
                    last_nonzero = outputs[tick];
                }
            }
        }
        require_true(error_sum / 1000 < 2 && reversals <= 2,
            "a stationary noiseless target must settle instead of sustained delay-induced oscillation");
    });
    registry.add_case("BaseBodyLock","continuous_point_braking_without_floor_override", [] {
        using namespace controller_native;
        for (float radius : {2.f,12.f}) {
            ResponseModelAimRequest r;
            r.range_position_response=true;r.position_range_px=150;
            r.minimum_position_stick=.3f;r.arrival_radius_px=radius;
            r.response_px_per_stick_second=1600;r.arrival_horizon_seconds=.04f;
            r.error_px={radius-.001f,0};const auto a=solve_response_model_aim(r).stick;
            r.error_px={radius+.001f,0};const auto b=solve_response_model_aim(r).stick;
            require_true(std::abs(b.x-a.x)<.001f,"point radius cannot introduce a force step");
            r.error_px={.01f,0};
            require_true(solve_response_model_aim(r).stick.x<.001f,"pursuit floor must yield at the actual point");
            r.error_px={0,0};r.relative_velocity_px_per_sec={90,0};r.motion_is_sustaining_target_motion=true;
            require_true(near(solve_response_model_aim(r).stick.x,90.f/1600),"centered moving target retains sustaining speed");
        }
    });
    registry.add_case("BaseBodyLock","range_envelope_survives_curves_and_preserves_motion", [] {
        using namespace controller_native;
        ResponseModelAimRequest request;
        request.range_position_response=true;request.position_range_px=150;
        request.arrival_horizon_seconds=.005f;request.max_force={.8f,.6f};
        for(auto curve : {AimResponseCurveAlgorithm::Linear,AimResponseCurveAlgorithm::CodDynamicLegacyLut,AimResponseCurveAlgorithm::CustomLut}) {
            request.response_curve.algorithm=curve;
            request.response_curve.custom_count=3;
            request.response_curve.custom_stick={0,.5f,1};request.response_curve.custom_response={0,.02f,1};
            float previous=0;
            for(float ratio : {0.f,.0001f,.001f,.01f,.05f,.1f,.25f,.5f,1.f}) {
                request.error_px={150*ratio,0};
                const auto output=solve_response_model_aim(request);
                require_true(output.stick.x<=.8f*std::sqrt(ratio)+1e-6f && output.stick.x>=previous-1e-6f,
                    "fast timing and nonlinear curves cannot bypass distance envelope or reverse monotonicity");
                previous=output.stick.x;
            }
            request.error_px={0,0};request.relative_velocity_px_per_sec={100,0};
            request.motion_is_sustaining_target_motion=true;
            const auto moving=solve_response_model_aim(request);
            require_true(moving.stick.x>0,"confirmed moving target still needs motion at the center");
            request.motion_is_sustaining_target_motion=false;
            require_true(solve_response_model_aim(request).stick.x==0,"unconfirmed rate cannot own centered force");
            request.relative_velocity_px_per_sec={};
        }
        request.response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        request.minimum_position_stick=.15f;request.arrival_radius_px=2;request.arrival_horizon_seconds=1;
        for(float error : {30.f,10.f,3.f,2.01f}) {
            request.error_px={error,0};
            require_true(solve_response_model_aim(request).stick.x>0,"continue toward the point with a bounded final approach");
        }
        for(float error : {2.f,1.f,0.f,-1.f,-2.f}) {
            request.error_px={error,0};
            const float actual=solve_response_model_aim(request).stick.x;
            require_true(error==0 ? actual==0 : actual*error>0,"point neighborhood tapers continuously toward the actual point");
        }
        request.error_px={3,0};request.relative_velocity_px_per_sec={-100,0};
        request.motion_is_error_rate_lookahead=true;
        require_true(solve_response_model_aim(request).stick.x==0,
            "pursuit floor must not override a complete stopping lookahead");
        request.minimum_position_stick=0;
        require_true(solve_response_model_aim(request).stick.x==0,
            "zero low-speed reference must preserve full lookahead cancellation");
        request.minimum_position_stick=.15f;
        request.error_px={2,0};
        require_true(solve_response_model_aim(request).stick.x==0,
            "lookahead must stop at the point radius");
        request.motion_is_error_rate_lookahead=false;
        request.motion_is_sustaining_target_motion=true;request.relative_velocity_px_per_sec={100,0};
        require_true(solve_response_model_aim(request).stick.x>0,
            "point arrival retains verified moving target feedforward");
        request.motion_is_sustaining_target_motion=false;request.relative_velocity_px_per_sec={};
        request.minimum_position_stick=0;request.arrival_radius_px=0;request.arrival_horizon_seconds=.005f;
        request.response_curve.algorithm=AimResponseCurveAlgorithm::Linear;request.error_px={15,0};
        request.position_range_px=150;const float near=solve_response_model_aim(request).stick.x;
        request.position_range_px=300;const float wider=solve_response_model_aim(request).stick.x;
        require_true(std::abs(wider/near-std::sqrt(.5f))<1e-5f,"real range changes must reach the solver");
    });
    registry.add_case("BaseBodyLock","bounded_position_and_ai_deadzone_boundaries", [] {
        controller_native::ResponseModelAimRequest request;
        request.response_curve.algorithm=controller_native::AimResponseCurveAlgorithm::Linear;
        request.range_position_response=true;request.position_range_px=150.f;request.arrival_horizon_seconds=.08f;
        request.max_force={.8f,.8f};request.error_px={32,0};
        const auto output=controller_native::solve_response_model_aim(request);
        require_true(near(output.stick.x,.8f*std::sqrt(32.f/150.f)),"32 px / 80 ms must not hit the 80 percent cap");
        request.error_px={150,0};
        require_true(near(controller_native::solve_response_model_aim(request).stick.x,.8f),"far position retains full configured force");
        request.max_force={0,.8f};request.error_px={10000,1};
        const auto disabled=controller_native::solve_response_model_aim(request);
        require_true(disabled.stick.x==0 && near(disabled.stick.y,-.025f),
            "disabled horizontal axis must not attenuate the vertical position response");
        for(float sign : {-1.f,1.f}) {
            for(float value : {0.f,.029f,.03f,.030001f,.3f,1.f}) {
                const auto input=pipeline_contract::Vec2f{sign*value,-sign*.2f};
                const auto actual=controller_native::filter_ai_input(input,.03f);
                require_true(actual.x==(value<=.03f ? 0.f : input.x) && actual.y==input.y,
                    "threshold is inclusive per axis and does not stretch surviving input");
                require_true(controller_native::filter_ai_input(input,0).x==input.x,"zero disables filtering");
            }
        }
        controller_native::AdsAcquisitionControllerConfig ads;
        ads.range_position_response=true;ads.arrival_horizon_seconds=.08f;ads.force_headroom=1;
        ads.response_curve.algorithm=controller_native::AimResponseCurveAlgorithm::Linear;
        auto plan=active_plan(10,-5);plan.mode=pipeline_contract::ControlMode::AdsAcquire;
        plan.normalized_size=.01f;
        const auto small=controller_native::AdsAcquisitionController(ads).compute(plan,{},.001f);
        plan.normalized_size=.9f;
        const auto large=controller_native::AdsAcquisitionController(ads).compute(plan,{},.001f);
        require_true(near(small.x,large.x) && near(small.y,large.y),"explicit ADS time cannot shrink with size");
        plan.error_px.y=5;
        const auto below=controller_native::AdsAcquisitionController(ads).compute(plan,{},.001f);
        require_true(near(small.y,-below.y),"explicit ADS time is direction independent");
    });
    registry.add_context_case("BaseBodyLock","range_response_delayed_plant_sweep", [](const native_test::TestContext& context) {
        std::ofstream report(context.artifact_path("range-response-plant.csv"));
        report << "seed,case,ticks,time_ms,cap,delay_ms,noise_px,initial_px,final_px,peak_overshoot_px,first_within_8_ms,radius_px,minimum_stick,first_within_2_ms\n";
        bool all_long_arrived=true;
        for(unsigned seed : {100621u,915731u}) {
            std::mt19937 rng(seed);std::uniform_real_distribution<float> unit(0,1);
            for(int scenario=0;scenario<80;++scenario) {
                const int ticks=scenario%2 ? 12000 : 2000;
                const int delay=(scenario%4)*10;
                const float time=.02f+unit(rng)*.33f,cap=.2f+unit(rng)*.8f,noise=unit(rng)*.4f;
                float error=20+unit(rng)*180,overshoot=0;const float initial=error;
                std::vector<float> history(delay+1,0);int arrival=-1,point_arrival=-1;
                controller_native::ResponseModelAimRequest request;
                request.response_curve.algorithm=controller_native::AimResponseCurveAlgorithm::Linear;
                request.range_position_response=true;request.position_range_px=250.f;request.minimum_position_stick=.10f+.05f*(scenario%3);request.arrival_radius_px=2;request.arrival_horizon_seconds=time;request.max_force={cap,cap};
                for(int tick=0;tick<ticks;++tick) {
                    request.error_px={error+(unit(rng)*2-1)*noise,0};
                    const auto aim=controller_native::filter_ai_input(controller_native::solve_response_model_aim(request).stick,.03f);
                    require_true(std::isfinite(aim.x) && std::abs(aim.x)<=cap+1e-6f &&
                        (aim.x==0 || std::abs(aim.x)>.03f),"random delayed plant obeys caps and deadzone");
                    require_true(aim.x*request.error_px.x>=0,
                        "continuous position approach cannot push away from the observed point");
                    const int slot=tick%history.size();const float delivered=history[slot];history[slot]=aim.x;
                    error-=delivered*500*.001f;overshoot=std::max(overshoot,-error);
                    if(arrival<0 && std::abs(error)<=8)arrival=tick;
                    if(point_arrival<0 && std::abs(error)<=2)point_arrival=tick;
                }
                report << seed << ',' << scenario << ',' << ticks << ',' << time*1000 << ',' << cap << ',' << delay << ','
                    << noise << ',' << initial << ',' << error << ',' << overshoot << ',' << arrival << ',' << request.position_range_px << ',' << request.minimum_position_stick << ',' << point_arrival << '\n';
                if(ticks>2000) all_long_arrived = all_long_arrived && point_arrival>=0;
            }
        }
        require_true(all_long_arrived,"long runs must reach the target; short-run deadlines remain reported explicitly");
    });

    registry.add_context_case("BaseBodyLock", "independent_limits_and_response_randomized", [](const native_test::TestContext& context) {
        std::ofstream report(context.artifact_path("independent-assist-parameters.csv"));
        require_true(report.good(),"parameter scenario report must open");
        report << "seed,scenario,ticks,cap_x,cap_y,time_x_ms,time_y_ms,max_follow_ellipse,max_ads_ellipse\n";
        // Independent development/validation seeds; short and long streams.
        for (unsigned seed : {61006u, 817193u}) {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> unit(0.f,1.f);
            for (int scenario=0;scenario<80;++scenario) {
                controller_native::BodylockFollowControllerConfig config;
                config.range_position_response=true;
                config.minimum_position_stick=0;
                config.response_curve.algorithm=scenario%2 ? controller_native::AimResponseCurveAlgorithm::Linear : controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
                config.max_force_x=.02f+.98f*unit(random);
                config.max_force_y=.02f+.98f*unit(random);
                config.response_time_x_seconds=.005f+.995f*unit(random);
                config.response_time_y_seconds=.005f+.995f*unit(random);
                controller_native::BodylockFollowController controller(config);
                controller_native::AdsAcquisitionControllerConfig ads_config;
                ads_config.force_headroom=1.f;
                ads_config.range_position_response=true;
                ads_config.minimum_position_stick=0;
                ads_config.response_curve=config.response_curve;
                ads_config.max_force_x=config.max_force_x;
                ads_config.max_force_y=config.max_force_y;
                ads_config.arrival_horizon_seconds=.06f+.29f*unit(random);
                controller_native::AdsAcquisitionController ads(ads_config);
                const int ticks=scenario%2 ? 2400 : 128;
                float follow_peak=0,ads_peak=0;
                for (int tick=0;tick<ticks;++tick) {
                    auto plan=active_plan((unit(random)-.5f)*640,(unit(random)-.5f)*512);
                    plan.position_response_radius_px=1.f+1999.f*unit(random);
                    plan.aim_authority=unit(random);
                    plan.reliability=unit(random);
                    plan.normalized_size=unit(random);
                    plan.response_scale=80.f+3920.f*unit(random);
                    plan.error_rate_px_per_sec={(unit(random)-.5f)*1000,(unit(random)-.5f)*1000};
                    if (tick%29==0) plan.lifecycle=pipeline_contract::TargetLifecycle::None;
                    const auto result=controller.compute_detailed(plan,{},.001f);
                    follow_peak=std::max(follow_peak,std::hypot(result.stick.x/config.max_force_x,result.stick.y/config.max_force_y));
                    require_true(std::isfinite(result.stick.x) && std::isfinite(result.stick.y) &&
                        std::abs(result.stick.x)<=config.max_force_x+1e-6f &&
                        std::abs(result.stick.y)<=config.max_force_y+1e-6f,"follow must honor independent normalized limits");
                    if(plan.lifecycle!=pipeline_contract::TargetLifecycle::None) {
                        require_true(near(result.response_horizon_seconds,config.response_time_x_seconds) &&
                            near(result.response_horizon_y_seconds,config.response_time_y_seconds),"time is independent of limit, error, reliability and plant response");
                    } else require_true(result.stick.x==0 && result.stick.y==0,"inactive follow must remain neutral");
                    const float fraction=std::sqrt(std::min(1.f,std::hypot(plan.error_px.x,plan.error_px.y)/plan.position_response_radius_px));
                    require_true(std::hypot(result.stick.x/config.max_force_x,result.stick.y/config.max_force_y)<=1.f+1e-5f,
                        "velocity feedback stays inside the configured joint actuator budget");
                    plan.mode=pipeline_contract::ControlMode::AdsAcquire;
                    const auto acquisition=ads.compute(plan,{},.001f);
                    require_true(std::hypot(acquisition.x/config.max_force_x,acquisition.y/config.max_force_y)<=fraction+1e-5f,
                        "ADS demand stays inside the geometry budget after the response curve");
                    ads_peak=std::max(ads_peak,std::hypot(acquisition.x/config.max_force_x,acquisition.y/config.max_force_y));
                    require_true(std::abs(acquisition.x)<=config.max_force_x+1e-6f &&
                        std::abs(acquisition.y)<=config.max_force_y+1e-6f,"ADS must not multiply explicit limits by hidden headroom");
                }
                require_true(follow_peak<=1.00001f && ads_peak<=1.00001f,"both stages retain joint ellipse bounds");
                report << seed << ',' << scenario << ',' << ticks << ',' << config.max_force_x << ',' << config.max_force_y << ','
                    << config.response_time_x_seconds*1000 << ',' << config.response_time_y_seconds*1000 << ',' << follow_peak << ',' << ads_peak << '\n';
                const auto before=controller.compute_detailed(active_plan(.01f,.01f),{},.001f);
                controller.set_force_limits(1.f,1.f);
                const auto after=controller.compute_detailed(active_plan(.01f,.01f),{},.001f);
                require_true(near(before.position_stick.x,after.position_stick.x) &&
                    near(before.position_stick.y,after.position_stick.y),"changing a cap must not change unsaturated position demand");
            }
        }
    });
    registry.add_context_case("BaseBodyLock", "point_boundary_closed_loop_diagnostic", simulate_point_boundary);
    registry.add_context_case("BaseBodyLock", "feedback_distance_delay_noise_sweep", test_feedback_distance_delay_noise_sweep);
    registry.add_context_case("BaseBodyLock", "center_crossing_incident", test_center_crossing_incident);
    registry.add_case("BaseBodyLock", "inactive_plan_is_neutral", test_inactive_plan_is_neutral);
    registry.add_case("BaseBodyLock", "current_error_owns_position_proposal", test_current_error_owns_position_proposal);
    registry.add_case("BaseBodyLock", "cue_uses_same_source_owned_solve", test_cue_lifecycle_uses_same_source_owned_solve);
    registry.add_case("BaseBodyLock", "manual_input_does_not_create_second_policy", test_manual_input_does_not_create_a_second_authority_policy);
    registry.add_case("BaseBodyLock", "motion_cannot_reverse_position_axis", test_motion_cannot_reverse_current_position_axis);
    registry.add_case("BaseBodyLock", "orthogonal_motion_cannot_mask_reversal", test_orthogonal_motion_cannot_mask_bodylock_axis_reversal);
    registry.add_case("BaseBodyLock", "force_envelope_remains_bounded", test_force_envelope_remains_bounded);
    registry.add_case("BaseBodyLock", "aligned_motion_replaces_screen_hint", test_aligned_target_motion_replaces_screen_relative_hint);
}
