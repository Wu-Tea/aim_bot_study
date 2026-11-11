#include "manual_stick_translation.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"
#include "ds4_output_report.h"
#include "response_model_aim_solver.h"

#include <chrono>
#include <iostream>
#include <random>

namespace {
using namespace controller_native;
using pipeline_contract::Vec2f;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::fabs(a-b) < 2.e-6f; }
GamepadRuntimeConfig config_for(int shape) {
    auto config = incident_fixture::base_config(50, 180);
    config.follow_response_curve = true;
    config.enemy_mark.enabled = false;
    config.auto_fire.aim_only = true;
    auto& c = config.aim_response_curve;
    c.algorithm = shape == 0 ? AimResponseCurveAlgorithm::Linear : shape == 1
        ? AimResponseCurveAlgorithm::CodDynamicLegacyLut : AimResponseCurveAlgorithm::CustomLut;
    c.custom_count = 3;
    c.custom_stick = {0, .5f, 1};
    c.custom_response = {0, .15f, 1};
    return config;
}

void translation_geometry_and_endpoints() {
    auto config = config_for(2);
    const auto p = translate_manual_stick({.3f, -.4f}, config);
    require(near(p.x, .09f) && near(p.y, -.12f), "custom half-radius maps to .15 and keeps direction");
    config = config_for(1);
    for (std::size_t i=0; i<aim_response_curve_detail::kCodDynamicLegacyStick.size(); ++i) {
        const auto p = translate_manual_stick({aim_response_curve_detail::kCodDynamicLegacyStick[i], 0}, config);
        require(near(p.x, aim_response_curve_detail::kCodDynamicLegacyResponse[i]),
            "dynamic mapping must use forward seed points, not AI inverse/calibration gain");
    }
    for (int shape : {0,1,2}) {
        config = config_for(shape);
        for (auto p : {Vec2f{}, Vec2f{1,0}, Vec2f{-1,1}, Vec2f{0,-1}}) {
            const auto q = translate_manual_stick(p, config);
            require(q.x == p.x && q.y == p.y, "center and full-scale corners remain exact");
        }
        require(translate_manual_stick({1.e-8f,0},config).x > 0, "translation must not add a deadzone");
        require(controller_response_curve(config).algorithm == AimResponseCurveAlgorithm::Linear,
            "translated input requires a linear game model, not duplicate nonlinear compensation");
        config.follow_response_curve = false;
        require(&controller_response_curve(config) == &config.aim_response_curve, "disabled keeps original AI curve");
        const auto q = translate_manual_stick({.3f,-.4f},config);
        require(q.x==.3f && q.y==-.4f, "disabled is bit-exact passthrough");
        config.follow_response_curve = true;
        config.ai_aim.adapter_direct_mouse_manual = true;
        require(translate_manual_stick({.3f,-.4f},config).x==.3f, "mouse adapter is not translated");
    }
}

void translation_immediate_output_and_raw_intent() {
    // Independent deterministic seeds; 0.3 s and 12 s sequences at 1 kHz,
    // Off/ADS-without-target, diagonal, reversal, release and noisy input.
    for (unsigned seed : {19u, 2039u, 73013u, 991927u}) for (int shape : {0,1,2})
    for (int length : {300,12000}) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> input(-1,1);
        auto translated = config_for(shape), original = translated;
        original.follow_response_curve = false;
        double clock=100;
        NativeGamepadController a(translated,&clock), b(original,&clock);
        auto physical=incident_fixture::ads_input();
        for (int tick=0; tick<length; ++tick) {
            clock += .001;
            physical.left_trigger = tick%150 < 75 ? 0.f : 1.f;
            physical.right_x = tick%101==0 ? 0 : input(rng);
            physical.right_y = tick%101==0 ? 0 : input(rng);
            physical.left_x=.25f; physical.left_y=-.6f; physical.a=tick%2==0;
            const auto ia=a.begin_tick(physical).intent, ib=b.begin_tick(physical).intent;
            require(ia.filtered_right.x==ib.filtered_right.x && ia.filtered_right.y==ib.filtered_right.y &&
                ia.right_x.activity==ib.right_x.activity && ia.right_y.activity==ib.right_y.activity,
                "curve must not change physical intent or per-axis authority");
            const auto out=a.build_output_from_sampled_input(), raw=b.build_output_from_sampled_input();
            const auto expected=translate_manual_stick({physical.right_x,physical.right_y},translated);
            require(near(out.right_x,expected.x) && near(out.right_y,expected.y), "same-tick mapped output with no temporal delay");
            require(raw.right_x==physical.right_x && raw.right_y==physical.right_y, "disabled preserves original output");
            require(out.left_x==physical.left_x && out.left_y==physical.left_y && out.a==physical.a,
                "translation only affects right stick");
        }
    }
}

void translation_ai_chain_and_expired_output(bool compensate = false) {
    // Same moving source for translated-dynamic/custom and baseline-linear
    // controllers, neutral manual: AI must be identical (no double shaping).
    for (unsigned seed : {31u, 2027u, 71023u, 829013u}) for (int shape : {1,2})
    for (int length : {300,12000}) {
        auto cfg=config_for(shape), linear=config_for(0);
        cfg.output_transfer.enabled=compensate;
        cfg.output_transfer.deadzone=0;
        cfg.output_transfer.response_model=GameStickResponseModel::CustomLut;
        cfg.output_transfer.curve.custom_count=kCodLinearReferenceStick.size();
        std::copy(kCodLinearReferenceStick.begin(),kCodLinearReferenceStick.end(),cfg.output_transfer.curve.custom_stick.begin());
        std::copy(kCodLinearReferenceResponse.begin(),kCodLinearReferenceResponse.end(),cfg.output_transfer.curve.custom_response.begin());
        linear.follow_response_curve=false;
        double clock=100;
        NativeGamepadController a(cfg,&clock), b(linear,&clock);
        incident_fixture::TargetSpec target;
        target.observation_id=target.selector_generation=1;
        auto physical=incident_fixture::ads_input();
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> jitter(-2,2);
        for (int tick=0; tick<length; ++tick) {
            clock+=.001;
            if (tick%8==0 && tick%500<420) {
                auto source=incident_fixture::observed_snapshot(target,tick+1,clock,
                    30.f*std::sin(tick*.003f)+jitter(rng),12.f*std::cos(tick*.002f));
                a.submit_vision_snapshot(source); b.submit_vision_snapshot(source);
            }
            const auto x=a.build_output(physical), y=b.build_output(physical);
            const auto actual=transfer_game_stick({x.right_x,x.right_y},cfg.output_transfer,true);
            require(std::fabs(actual.x-y.right_x)<1.e-4f && std::fabs(actual.y-y.right_y)<1.e-4f, "AI response must match intended game response through tracking/gaps");
            const auto& plan=a.last_target_plan();
            const auto& baseline=b.last_target_plan();
            require(plan.target_id==baseline.target_id && plan.aim_px.x==baseline.aim_px.x && plan.aim_px.y==baseline.aim_px.y,
                "translation does not alter target acquisition or source aim point");
        }
        clock+=.001;
        a.submit_vision_snapshot(incident_fixture::observed_snapshot(target,length+1,clock,30,5));
        physical.right_x=.3f; physical.right_y=-.4f;
        a.begin_tick(physical);
        auto frame=a.resolve_control_frame();
        require(a.expire_visual_output(frame,clock+1,50), "fixture must expire an admitted visual target");
        const auto expected=translate_manual_stick({.3f,-.4f},cfg);
        require(near(frame.pre_recoil_command().stick.x,expected.x) && near(frame.pre_recoil_command().stick.y,expected.y),
            "expiry must return mapped manual, never stale AI or untransformed input");
    }
}

void cod_linear_reference_composition() {
    auto cfg=config_for(1);
    cfg.output_transfer.enabled=true;
    cfg.output_transfer.deadzone=0;
    cfg.output_transfer.response_model=GameStickResponseModel::CodLinearLegacyLut;
    // An independently calculated source row: input .50185, revolution 1.466s,
    // full revolution .366s. This is a quarter-speed response, not half-speed.
    require(near(cod_linear_reference_magnitude(.50185f,true),.366f/1.466f), "reference must retain source normalized rates");
    const auto middle=transfer_game_stick(translate_manual_stick({.5f,0},cfg),cfg.output_transfer);
    require(std::fabs(middle.x-.461432846f)<1.e-6f, "half input requires 46.143% wire output to match existing dynamic");
    for (bool axial:{false,true}) for (float deadzone:{0.f,.08f}) {
        cfg.output_transfer.axial=axial;
        cfg.output_transfer.deadzone=deadzone;
        float previous=-1;
        for (int i=0;i<=10000;++i) {
            const float input=i/10000.f;
            const auto desired=translate_manual_stick({input,0},cfg);
            const auto wire=transfer_game_stick(desired,cfg.output_transfer);
            const auto realized=transfer_game_stick(wire,cfg.output_transfer,true);
            require(wire.x>=previous && near(realized.x,desired.x), "monotonic compensation composes back to target curve");
            previous=wire.x;
        }
    }
    cfg.output_transfer.axial=false; cfg.output_transfer.deadzone=0;
    std::mt19937 rng(651781);
    std::uniform_real_distribution<float> input(-1,1);
    float max_quantization_error=0;
    double clock=100;
    NativeGamepadController controller(cfg,&clock);
    PhysicalGamepadState physical;
    physical.connected=true;
    for (int i=0;i<20000;++i) {
        clock+=.001;
        physical.right_x=input(rng); physical.right_y=input(rng);
        const auto desired=translate_manual_stick({physical.right_x,physical.right_y},cfg);
        const auto output=controller.build_output(physical);
        const auto realized=transfer_game_stick({output.right_x,output.right_y},cfg.output_transfer,true);
        require(near(realized.x,desired.x) && near(realized.y,desired.y), "manual composer applies inverse only once and keeps direction");
        const auto quantized=transfer_game_stick({ds4_axis_value(ds4_axis(output.right_x)),ds4_axis_value(ds4_axis(output.right_y))},cfg.output_transfer,true);
        max_quantization_error=std::max(max_quantization_error,std::hypot(quantized.x-desired.x,quantized.y-desired.y));
    }
    std::cout<<"COD reference DS4 max normalized vector error="<<max_quantization_error<<'\n';
    require(max_quantization_error<.02f,"DS4 quantization stays within the modeled output precision");
    require(transfer_game_stick({},cfg.output_transfer).x==0,"compensated neutral remains exact");
    cfg.output_transfer.enabled=false;
    require(transfer_game_stick({.3f,-.4f},cfg.output_transfer).x==.3f,"disabling reference restores prior output");
}

void translation_manual_exit_and_axis_authority() {
    AssistControlStateMachineConfig config;
    config.use_gamepad_intent_for_arbitration=true;
    for (auto mode : {pipeline_contract::ControlMode::AdsAcquire, pipeline_contract::ControlMode::BodyLockFollow}) {
        AssistControlStateMachine machine(config);
        AssistControlStateMachineInput input;
        input.activation=pipeline_contract::AssistActivation::Engaged;
        input.target_authoritative=true; input.target_id=1;
        input.mode=mode; input.visual_authority=1;
        input.manual_stick={.3f,-.4f}; input.manual_output_stick={.09f,-.12f};
        input.manual_output_available=true; input.manual_axis_activity={1,1};
        input.filtered_manual_stick=input.manual_stick;
        input.ai_stick={.2f,.2f}; input.firing=true;
        input.manual_correction_y=true;
        const auto out=machine.update(input);
        require(near(out.stick.y,-.12f), "firing downward authority uses translated output magnitude");
        input.manual_exit_requested=true;
        const auto exit=machine.update(input);
        require(exit.handover_requested && near(exit.stick.x,.09f) && near(exit.stick.y,-.12f),
            "explicit exit immediately returns translated manual on both axes");
    }
}

void translation_mapping_cost() {
    std::mt19937 rng(37013);
    std::uniform_real_distribution<float> input(-.7f,.7f);
    std::array<Vec2f,4096> values;
    for (auto& p:values) p={input(rng),input(rng)};
    for (int shape : {0,1,2}) {
        const auto cfg=config_for(shape);
        float sum=0;
        const auto start=std::chrono::steady_clock::now();
        for (int round=0;round<256;++round) for (const auto p:values) sum+=translate_manual_stick(p,cfg).x;
        const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(256*values.size());
        std::cout<<"translation shape="<<shape<<" ns/input="<<ns<<" checksum="<<sum<<'\n';
        require(std::isfinite(sum), "mapping cost probe has finite output");
    }
    for (int round=0;round<3;++round) for (bool enabled : {false,true}) {
        auto cfg=config_for(1);
        cfg.follow_response_curve=enabled;
        double clock=100;
        NativeGamepadController controller(cfg,&clock);
        PhysicalGamepadState physical;
        physical.connected=true;
        float sum=0;
        const auto start=std::chrono::steady_clock::now();
        for (int repeat=0;repeat<4;++repeat) for (const auto p:values) {
            clock+=.001;
            physical.right_x=p.x; physical.right_y=p.y;
            sum+=controller.build_output(physical).right_x;
        }
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/(4*values.size());
        std::cout<<"manual controller round="<<round<<" translation="<<enabled<<" us/tick="<<us<<" checksum="<<sum<<'\n';
    }
    for (bool compensate:{false,true}) {
        auto cfg=config_for(1);
        cfg.output_transfer.enabled=compensate;
        cfg.output_transfer.deadzone=0;
        cfg.output_transfer.response_model=GameStickResponseModel::CodLinearLegacyLut;
        float sum=0;
        const auto start=std::chrono::steady_clock::now();
        for(int repeat=0;repeat<256;++repeat) for(const auto p:values)
            sum+=transfer_game_stick(translate_manual_stick(p,cfg),cfg.output_transfer).x;
        const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(256*values.size());
        std::cout<<"dynamic + output compensation="<<compensate<<" ns/input="<<ns<<" checksum="<<sum<<'\n';
    }
}

GamepadRuntimeConfig prepared_config(bool compensate) {
    auto cfg=config_for(1);
    cfg.output_transfer.enabled=true;
    cfg.output_transfer.deadzone=0;
    cfg.output_transfer.response_model=GameStickResponseModel::OutputLut;
    auto& c=cfg.output_transfer.curve;
    c.algorithm=AimResponseCurveAlgorithm::CustomLut;
    // Build the exact piecewise C=A^-1(B) from both sets of breakpoints.
    std::vector<float> knots{0,1};
    if (compensate) {
        for (auto x:aim_response_curve_detail::kCodDynamicLegacyStick) knots.push_back(x);
        for (auto y:kCodLinearReferenceResponse) knots.push_back(curve_inverse_magnitude(y,cfg.aim_response_curve));
    }
    std::sort(knots.begin(),knots.end());
    knots.erase(std::unique(knots.begin(),knots.end()),knots.end());
    c.custom_count=knots.size();
    for (std::size_t i=0;i<knots.size();++i) {
        c.custom_stick[i]=knots[i];
        c.custom_response[i]=compensate ? cod_linear_reference_magnitude(curve_forward_magnitude(knots[i],cfg.aim_response_curve),false) : knots[i];
    }
    return cfg;
}

void independent_input_and_ai_curves() {
    for (bool compensate:{false,true}) for (bool manual:{false,true}) {
        auto cfg=prepared_config(compensate); cfg.follow_response_curve=manual;
        double clock=100; NativeGamepadController controller(cfg,&clock);
        PhysicalGamepadState physical; physical.connected=true;
        float sum=0;
        const auto start=std::chrono::steady_clock::now();
        for (int i=0;i<20000;++i) {
            clock+=.001; physical.right_x=.6f*std::sin(i*.017f); physical.right_y=.4f*std::cos(i*.013f);
            sum+=controller.build_output(physical).right_x;
        }
        std::cout<<"prepared controller C="<<compensate<<" manual="<<manual<<" us/tick="
            <<std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/20000<<" checksum="<<sum<<'\n';
    }
    for (unsigned seed:{41u,2089u,81173u,971903u}) for (bool compensate:{false,true})
    for (bool manual:{false,true}) for (int length:{300,12000}) {
        auto cfg=prepared_config(compensate);
        cfg.follow_response_curve=manual;
        require(&controller_response_curve(cfg)==&cfg.aim_response_curve,"AI always uses B, independently of C and manual toggle");
        auto baseline=cfg;
        baseline.output_transfer.enabled=false;
        baseline.follow_response_curve=false;
        double clock=100;
        NativeGamepadController a(cfg,&clock), b(baseline,&clock);
        PhysicalGamepadState physical; physical.connected=true;
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> input(-1,1);
        for (int tick=0;tick<length;++tick) {
            clock+=.001;
            physical.right_x=tick%101==0 ? 0 : input(rng);
            physical.right_y=tick%101==0 ? 0 : input(rng);
            auto ia=a.begin_tick(physical).intent, ib=b.begin_tick(physical).intent;
            require(ia.filtered_right.x==ib.filtered_right.x && ia.filtered_right.y==ib.filtered_right.y &&
                ia.right_x.activity==ib.right_x.activity && ia.right_y.activity==ib.right_y.activity,
                "prepared curves preserve raw per-axis intent");
            auto out=a.build_output_from_sampled_input(); b.build_output_from_sampled_input();
            const Vec2f raw{physical.right_x,physical.right_y};
            auto expected=manual ? transfer_game_stick(raw,cfg.output_transfer) : raw;
            require(near(out.right_x,expected.x) && near(out.right_y,expected.y),"Off output maps C once, or preserves raw when manual translation is off");
            if (manual && compensate && std::hypot(raw.x,raw.y)<1) {
                require(near(cod_linear_reference_magnitude(std::hypot(out.right_x,out.right_y),true),
                    curve_forward_magnitude(std::hypot(raw.x,raw.y),cfg.aim_response_curve)),"game A composed with prepared C realizes B");
            }
        }
        // Same observations, neutral manual: compare against a game already
        // using B. Decoding C must recover the same AI command and feedback.
        physical=incident_fixture::ads_input();
        incident_fixture::TargetSpec target; target.observation_id=target.selector_generation=1;
        for (int tick=0;tick<length;++tick) {
            clock+=.001;
            if (tick%8==0 && tick%500<420) {
                auto source=incident_fixture::observed_snapshot(target,tick+1,clock,30*std::sin(tick*.003f),12*std::cos(tick*.002f));
                a.submit_vision_snapshot(source); b.submit_vision_snapshot(source);
            }
            auto x=a.build_output(physical), y=b.build_output(physical);
            auto decoded=transfer_game_stick({x.right_x,x.right_y},cfg.output_transfer,true);
            require(std::fabs(decoded.x-y.right_x)<1.e-4f && std::fabs(decoded.y-y.right_y)<1.e-4f,"prepared C preserves B-based AI solve and feedback across target gaps");
            require(a.last_target_plan().target_id==b.last_target_plan().target_id,"curve selection preserves target acquisition");
        }
    }
}

void full_response_calibration_across_curves() {
    for (unsigned seed : {73u, 1093u, 81239u, 924119u}) for (int count : {300, 12000})
    for (float gain : {3000.f, 5000.f}) for (int shape : {0, 1, 2}) {
        auto curve = config_for(shape).aim_response_curve;
        // Old anchor values must not secretly scale either demand or learning.
        curve.calibration_reference_stick = seed % 2 ? .2f : .8f;
        AimResponseEstimatorConfig learner_config; learner_config.fallback_scale = gain * .6f;
        AimResponseEstimator learner(learner_config);
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> unit(.05f, .8f);
        for (int tick = 0; tick < count; ++tick) {
            const float demand = unit(random);
            const float stick = curve_inverse_magnitude(demand, curve);
            const auto delivered = forward_aim_response_curve({stick, 0}, curve);
            require(std::fabs(delivered.x - demand) < 2.e-6f,
                "learning command is normalized response, independent of curve and old anchor");
            AimResponseInterval interval;
            interval.target_id = 1 + tick / 200;
            interval.average_final_stick = delivered;
            interval.observed_error_rate_px_per_sec = {75.f - gain * demand, 0};
            interval.dt_seconds = .01f; interval.reliability = 1; interval.observed = true;
            learner.update(interval);
            for (bool follow : {false, true}) {
                ResponseModelAimRequest request;
                request.range_position_response = true;
                request.delay_aware_velocity_response = follow;
                request.position_range_px = 200;
                request.response_px_per_stick_second = gain;
                request.arrival_horizon_seconds = .1f;
                request.error_px = {demand * gain * .1f, 0};
                request.response_curve = curve;
                const auto output = solve_response_model_aim(request);
                // Independent expected response, including ADS distance envelope.
                const float expected = follow ? demand : std::min(demand,
                    std::sqrt(std::min(1.f, request.error_px.x / 200.f)));
                require(std::fabs(curve_forward_magnitude(output.stick.x, curve) - expected) < 3.e-6f,
                    "same px/s prior and target error must request the same camera speed across curves");
            }
        }
        require(learner.estimate().accepted_samples > 100 &&
            std::fabs(learner.estimate().scale_px_per_stick_second - gain) < gain * .01f,
            "auto learning must recover the same full-response px/s value for every curve");
    }
}

void recoil_response_composition_randomized() {
    for (unsigned seed : {181u, 2987u, 82103u, 781321u}) for (int count : {300, 12000})
    for (bool compensate : {false, true}) {
        auto cfg = prepared_config(compensate);
        cfg.recoil.enabled = true; cfg.recoil.output_amount = .2f; cfg.recoil.hipfire_multiplier = .5f;
        double clock = 100.; NativeGamepadController controller(cfg, &clock);
        std::mt19937 random(seed); std::uniform_real_distribution<float> axis(-.35f, .35f);
        for (int tick = 0; tick < count; ++tick) {
            clock += .001;
            PhysicalGamepadState physical; physical.connected = true;
            physical.right_x = axis(random); physical.right_y = axis(random);
            physical.left_trigger = tick % 200 < 100 ? 1.f : 0.f;
            const bool firing = tick % 41 != 0;
            physical.rb = firing; physical.right_trigger = firing ? 1.f : 0.f;
            const auto preparation = controller.begin_tick(physical);
            const auto output = controller.build_output_from_sampled_input();
            const auto internal = transfer_game_stick({output.right_x, output.right_y}, cfg.output_transfer, true);
            const float raw_radius = std::hypot(physical.right_x, physical.right_y);
            const float base = curve_forward_magnitude(raw_radius, cfg.aim_response_curve);
            const Vec2f expected{physical.right_x * base / raw_radius,
                physical.right_y * base / raw_radius - (firing ? .2f * (preparation.scope.physical_ads_ready ? 1.f : .5f) : 0.f)};
            const auto actual = forward_aim_response_curve(internal, cfg.aim_response_curve);
            require(std::fabs(actual.x - expected.x) < 4.e-6f && std::fabs(actual.y - expected.y) < 4.e-6f,
                "recoil adds target response once, preserving horizontal response and input mapping");
            if (compensate) {
                const float radius = std::hypot(output.right_x, output.right_y);
                const float game_response = cod_linear_reference_magnitude(radius, true);
                require(std::fabs(game_response - std::hypot(expected.x, expected.y)) < 4.e-6f,
                    "game A composed with C must realize requested B response including recoil");
            }
        }
    }
}
}

void register_manual_stick_translation_tests(native_test::Registry& registry) {
    registry.add_case("BaseTranslation", "first_capture_feedback_uses_response_units", [] {
        float expected = 0;
        for (int shape : {0, 1, 2}) {
            auto cfg = config_for(shape);
            cfg.follow_response_curve = false; cfg.output_transfer.enabled = false;
            cfg.ai_aim.aim_response_learning_enabled = false;
            cfg.ai_aim.aim_response_initial_scale = 3000;
            double clock = 100.; NativeGamepadController controller(cfg, &clock);
            const float stick = curve_inverse_magnitude(.2f, cfg.aim_response_curve);
            controller.build_output(incident_fixture::ads_input(stick, 0));
            clock += .01;
            incident_fixture::TargetSpec target; target.observation_id = target.selector_generation = 1;
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(target, 1, clock, 20, 0));
            controller.build_output(incident_fixture::ads_input());
            const auto& plan = controller.last_target_plan();
            require(plan.target_id != 0, "first-capture regression must acquire a target");
            if (!shape) expected = plan.radial_closing_velocity_px_per_sec;
            require(std::fabs(plan.radial_closing_velocity_px_per_sec - expected) < .002f,
                "first-capture closing prediction must use response, not nonlinear stick position");
        }
    });
    registry.add_case("BaseTranslation", "full_response_calibration_across_curves", full_response_calibration_across_curves);
    registry.add_case("BaseTranslation", "recoil_response_composition_randomized", recoil_response_composition_randomized);
    registry.add_case("BaseTranslation", "force_limit_is_target_response", [] {
        for (bool follow : {false, true}) for (float cap : {.2f, .5f, .8f}) {
            ResponseModelAimRequest request;
            request.range_position_response = true;
            request.delay_aware_velocity_response = follow;
            request.motion_is_error_rate_lookahead = !follow;
            request.position_range_px = 200.f;
            request.error_px = {10000.f, 0};
            request.max_force = {cap, cap};
            request.response_curve.algorithm = AimResponseCurveAlgorithm::CodDynamicLegacyLut;
            const auto output = solve_response_model_aim(request);
            require(near(curve_forward_magnitude(output.stick.x, request.response_curve), cap),
                "ADS/BodyLock cap is normalized target response, not stick displacement");
        }
    });
    registry.add_case("BaseTranslation", "recoil_amount_is_target_response", [] {
        auto cfg = config_for(1);
        cfg.follow_response_curve = false;
        cfg.output_transfer.enabled = false;
        cfg.recoil.enabled = true;
        cfg.recoil.output_amount = .5f;
        cfg.recoil.hipfire_multiplier = 1.f;
        double clock = 100.; NativeGamepadController controller(cfg, &clock);
        PhysicalGamepadState physical; physical.connected = true;
        physical.rb = true; physical.right_trigger = 1.f;
        const auto output = controller.build_output(physical);
        require(near(curve_forward_magnitude(-output.right_y, cfg.aim_response_curve), .5f),
            "recoil amount must realize fifty percent target response");
    });
    registry.add_case("BaseTranslation", "geometry_and_endpoints", translation_geometry_and_endpoints);
    registry.add_case("BaseTranslation", "immediate_output_and_raw_intent", translation_immediate_output_and_raw_intent);
    registry.add_case("BaseTranslation", "ai_chain_and_expired_output", [] { translation_ai_chain_and_expired_output(); });
    registry.add_case("BaseTranslation", "cod_linear_reference_ai_chain", [] { translation_ai_chain_and_expired_output(true); });
    registry.add_case("BaseTranslation", "cod_linear_reference_composition", cod_linear_reference_composition);
    registry.add_case("BaseTranslation", "manual_exit_and_axis_authority", translation_manual_exit_and_axis_authority);
    registry.add_case("BaseTranslation", "mapping_cost", translation_mapping_cost);
    registry.add_case("BaseTranslation", "independent_input_and_ai_curves", independent_input_and_ai_curves);
}
