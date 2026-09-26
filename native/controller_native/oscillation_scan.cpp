#include "assist_control_state_machine.h"
#include "ds4_output_report.h"
#include "incident_fixture_support.h"
#include "intent_filter.h"
#include "runtime_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using namespace controller_native;
using pipeline_contract::Vec2f;
template<class V> float axis(V v, int a) { return a ? v.y : v.x; }
Vec2f along(float v, int a) { return a ? Vec2f{0, v} : Vec2f{v, 0}; }

struct BoundaryReport {
    int triggers = 0, controls = 0;
    float max_step = 0, passthrough_loss = 0;
};

BoundaryReport boundaries(std::ostream& out) {
    BoundaryReport report;
    out << "[";
    for (int a : {0, 1}) for (float sign : {-1.f, 1.f})
    for (bool fresh : {false, true}) for (float evidence : {.7f, .803223f, 1.f})
    for (float ai : {.15f, .61f, .9f}) {
        IntentFilterConfig fc; fc.gamepad_right_stick_curve = true;
        IntentFilter filter(fc);
        AssistControlStateMachineConfig ac; ac.use_gamepad_intent_for_arbitration = true;
        AssistControlStateMachine arbiter(ac);
        std::array<float, 2> outputs{};
        for (int k = 0; k < 2; ++k) {
            const auto raw = along(sign * (k ? .24999f : .25001f), a);
            const auto intent = filter.update({}, raw, true, false, 1 + k * .004, true);
            AssistControlStateMachineInput in;
            in.aiming = in.target_authoritative = true;
            in.target_id = in.selector_target_generation = 1;
            in.mode = pipeline_contract::ControlMode::BodyLockFollow;
            in.now_seconds = 1 + k * .004;
            in.fresh_observation = fresh;
            in.visual_authority = evidence;
            in.manual_stick = raw;
            in.filtered_manual_stick = intent.filtered_right;
            in.manual_axis_activity = {intent.right_x.activity, intent.right_y.activity};
            in.ai_stick = along(-sign * ai, a);
            in.target_error_px = a ? Vec2f{0, sign * 56} : Vec2f{-sign * 56, 0};
            in.manual_correction_x = a == 0 && intent.filtered_right.x != 0;
            in.manual_correction_y = a == 1 && intent.filtered_right.y != 0;
            if (axis(intent.filtered_right, a) == 0)
                throw std::runtime_error("intent boundary trigger missing");
            const auto result = arbiter.update(in);
            if (result.phase != AssistControlPhase::Track)
                throw std::runtime_error("track trigger missing");
            outputs[k] = axis(result.stick, a);
            for (bool explicit_exit : {false, true}) {
                AssistControlStateMachine control(ac);
                auto cf = in;
                cf.target_authoritative = explicit_exit;
                cf.manual_exit_requested = explicit_exit;
                auto result_cf = control.update(cf);
                report.passthrough_loss = std::max(report.passthrough_loss,
                    std::fabs(axis(result_cf.stick, a) - axis(raw, a)));
                ++report.controls;
            }
        }
        const float step = std::fabs(outputs[1] - outputs[0]);
        report.max_step = std::max(report.max_step, step);
        out << (report.triggers++ ? "," : "") << "{\"axis\":" << a
            << ",\"sign\":" << sign << ",\"fresh\":" << fresh
            << ",\"authority\":" << evidence << ",\"ai\":" << ai
            << ",\"before\":" << outputs[0] << ",\"after\":" << outputs[1]
            << ",\"step\":" << step << "}";
    }
    out << "]";
    return report;
}

struct Spec {
    int id, axis, duration, vision_ms, age_ms, effect_ms, manual, motion;
    float response, slow, noise, rotation, initial, velocity;
    bool fire, left;
};

// Fully attributed, synthetic game plant. It does not estimate real Warzone AA.
Spec spec_for(int i, std::mt19937& rng) {
    const auto draw = [&](float low, float high) {
        return low + (high-low) * (rng() / 4294967295.0f);
    };
    // Draw dimensions independently: indexing every dimension by i would alias
    // stationary cases with free-space gain and miss strong-AA stationary loops.
    return {i, i%2, std::array<int,3>{600,1800,10000}[rng()%3],
        std::array<int,3>{5,10,16}[rng()%3],
        std::array<int,3>{3,8,14}[rng()%3],
        std::array<int,3>{4,9,20}[rng()%3], int(rng()%4), int(rng()%4),
        draw(350,1000), std::array<float,4>{1,.65f,.4f,.2f}[rng()%4],
        rng()%4 == 0 ? 0.f : draw(0,.7f), rng()%2 ? .65f : 0.f,
        (rng()%2 ? -1.f : 1.f)*draw(35,100), draw(20,65),
        rng()%2 != 0, rng()%2 != 0};
}

struct LoopMetrics {
    int acquired, body, observer, crossings;
    float overshoot;
};

LoopMetrics closed_loop(const Spec& s, GamepadRuntimeConfig config,
                 std::ostream& out, std::ostream* trace, bool quantized_receipt) {
    // Recoil's independent owner is verified by the existing feature suite;
    // these cases isolate aim while retaining firing's learning-ambiguity input.
    config.recoil.enabled = false;
    config.recoil.profile_playback_enabled = false;
    double now = 1;
    NativeGamepadController controller(config, &now);
    incident_fixture::TargetSpec target;
    target.observation_id = 1;
    target.selector_generation = 1;
    target.aim_height_ratio = config.tracker.aim_height_ratio;
    std::deque<std::pair<int, ControllerVisionSnapshot>> captures;
    std::deque<float> commands(s.effect_ms, 0);
    float error = s.initial, previous_output = 0, max_step = 0;
    float max_error = 0, overshoot = 0, squared_error = 0;
    int acquired = -1, ads = 0, body = 0, observer = 0, flips = 0;
    int last_sign = 0, correction = 0, target_ticks = 0, tracking_ticks = 0;
    int fresh = 0;
    float passthrough = 0;
    std::uint64_t frame = 0;
    for (int ms = 0; ms < s.duration; ++ms) {
        now = 1 + ms * .001;
        const float seconds = ms * .001f;
        float velocity = s.motion == 0 ? 0 : s.velocity;
        if (s.motion == 2 && ms > s.duration/2) velocity = 0;
        if (s.motion == 3 && (ms/400)%2) velocity = -velocity;
        const float left = s.left ? (ms/600%2 ? -.8f : .8f) : 0.f;
        const float relative_velocity = velocity - (s.axis == 0 ? left*35.f : 0.f);
        if (ms % s.vision_ms == 0) {
            const float measurement = error + s.noise * std::sin(ms * 1.719f);
            auto snapshot = incident_fixture::observed_snapshot(target, ++frame, now,
                s.axis ? 0 : measurement, s.axis ? measurement : 0);
            snapshot.ready_time_seconds = now + s.age_ms * .001;
            captures.emplace_back(ms + s.age_ms, std::move(snapshot));
        }
        while (!captures.empty() && captures.front().first <= ms) {
            controller.submit_vision_snapshot(captures.front().second);
            captures.pop_front(); ++fresh;
        }
        float manual = 0;
        if (ms >= 200) {
            if (s.manual == 1) manual = -.0117798f;
            if (s.manual == 2) manual = .25f + .07f*std::sin(seconds*13.f);
            if (s.manual == 3) manual = .45f*std::sin(seconds*8.f);
        }
        auto physical = incident_fixture::ads_input(
            s.axis ? 0 : manual, s.axis ? -manual : 0, s.fire);
        physical.left_x = left;
        auto output = controller.build_output(physical);
        const auto& plan = controller.last_target_plan();
        const auto& components = controller.last_output_components();
        const float u = s.axis ? -output.right_y : output.right_x;
        if (!std::isfinite(u) || std::fabs(u)>1.00001f)
            throw std::runtime_error("invalid output");
        max_step = std::max(max_step,std::fabs(u-previous_output)); previous_output = u;
        ads += plan.mode == pipeline_contract::ControlMode::AdsAcquire;
        body += plan.mode == pipeline_contract::ControlMode::BodyLockFollow;
        observer += plan.bodylock_target_motion_valid;
        target_ticks += plan.target_id != 0;
        correction += plan.manual_correction_x || plan.manual_correction_y;
        if (acquired < 0 && std::fabs(error)<=8) acquired = ms;
        if (ms > 200 && acquired >= 0) {
            ++tracking_ticks;
            squared_error += error*error;
            max_error = std::max(max_error,std::fabs(error));
            overshoot = std::max(overshoot, -std::copysign(1.f,s.initial)*error);
            if (std::fabs(error)>3) {
                int sign = error>0 ? 1 : -1;
                if (last_sign && sign != last_sign) ++flips;
                last_sign = sign;
            }
        }
        // The actual DS4 serializer owns quantization. Decode its X/Y bytes
        // in screen coordinates for the game plant; no invented input deadzone.
        const auto report = to_ds4_report(output);
        if (quantized_receipt) {
            auto delivered = output;
            delivered.right_x = ds4_axis_value(report.bytes[2]);
            delivered.right_y = -ds4_axis_value(report.bytes[3]);
            // Replace the facade's same-timestamp float acknowledgement with
            // the exact coordinates consumed by this DS4 plant, as runtime does.
            controller.observe_delivered_output(delivered, true, now);
        }
        const int code = report.bytes[s.axis ? 3 : 2];
        const float decoded = (code-128.f)/(code<128 ? 128.f : 127.f);
        commands.push_back(decoded);
        const float applied = commands.front(); commands.pop_front();
        const auto response = forward_aim_response_curve(along(applied,s.axis), config.aim_response_curve);
        const float proximity = std::clamp(1.f-std::fabs(error)/40.f,0.f,1.f);
        const float slowdown = 1.f-(1.f-s.slow)*proximity;
        // Rotational assistance has independent known target/POV velocity. It
        // is never fed into the controller as target truth.
        const float aa_rate = s.rotation * proximity * std::fabs(left) * relative_velocity;
        if (trace) *trace << s.id << ',' << ms << ',' << error << ',' << manual << ','
            << u << ',' << axis(components.requested_assist_stick,s.axis) << ','
            << axis(components.bodylock_position_stick,s.axis) << ','
            << axis(components.bodylock_motion_stick,s.axis) << ','
            << plan.response_scale << ',' << static_cast<int>(plan.mode) << '\n';
        error += (relative_velocity - aa_rate - s.response*slowdown*axis(response,s.axis))*.001f;
    }
    now += .001;
    auto released = incident_fixture::ads_input(.12f,-.12f); released.left_trigger=0;
    const auto raw = controller.build_output(released);
    passthrough=std::max(std::fabs(raw.right_x-.12f),std::fabs(raw.right_y+.12f));
    if (passthrough>1e-6f || target_ticks==0 || fresh<2)
        throw std::runtime_error("closed loop trigger/passthrough failure");
    out << "{\"id\":"<<s.id<<",\"axis\":"<<s.axis<<",\"duration_ms\":"<<s.duration
        <<",\"vision_ms\":"<<s.vision_ms<<",\"age_ms\":"<<s.age_ms<<",\"effect_ms\":"<<s.effect_ms
        <<",\"response\":"<<s.response<<",\"initial_response\":"<<config.ai_aim.aim_response_initial_scale
        <<",\"slowdown\":"<<s.slow<<",\"noise\":"<<s.noise
        <<",\"rotation\":"<<s.rotation<<",\"initial\":"<<s.initial<<",\"velocity\":"<<s.velocity
        <<",\"manual\":"<<s.manual<<",\"motion\":"<<s.motion<<",\"fire\":"<<s.fire<<",\"left\":"<<s.left
        <<",\"acquire_ms\":"<<acquired<<",\"ads_ticks\":"<<ads<<",\"body_ticks\":"<<body
        <<",\"observer_ticks\":"<<observer<<",\"correction_ticks\":"<<correction<<",\"target_ticks\":"<<target_ticks
        <<",\"tracking_ticks\":"<<tracking_ticks<<",\"fresh\":"<<fresh<<",\"crossings\":"<<flips
        <<",\"max_error\":"<<max_error<<",\"overshoot\":"<<overshoot
        <<",\"rms_error\":"<<std::sqrt(squared_error/std::max(1,tracking_ticks))
        <<",\"max_step\":"<<max_step<<",\"passthrough_loss\":"<<passthrough<<"}";
    return {acquired, body, observer, flips, overshoot};
}
}

int main(int argc,char** argv) {
    try {
        std::string output, config_path="config.toml", trace_path;
        unsigned seed=1337; int cases=96, case_id=-1, fire_override=-1, effect_override=-1;
        float prior_override=0;
        bool stationary_incident=false, quantized_receipt=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(i+1>=argc) throw std::runtime_error("missing argument");
            const std::string value=argv[++i];
            if(arg=="--output") output=value;
            else if(arg=="--config") config_path=value;
            else if(arg=="--trace") trace_path=value;
            else if(arg=="--seed") seed=std::stoul(value);
            else if(arg=="--cases") cases=std::stoi(value);
            else if(arg=="--case-id") case_id=std::stoi(value);
            else if(arg=="--fire") fire_override=std::stoi(value);
            else if(arg=="--effect-ms") effect_override=std::stoi(value);
            else if(arg=="--initial-response") prior_override=std::stof(value);
            else if(arg=="--stationary-incident" && value=="1") stationary_incident=true;
            else if(arg=="--quantized-receipt" && (value=="0" || value=="1")) quantized_receipt=value=="1";
            else throw std::runtime_error("unknown option");
        }
        if(output.empty()||cases<0||cases>10000||case_id>=cases||case_id< -1||
           fire_override< -1||fire_override>1||effect_override< -1||effect_override>100||
           !std::isfinite(prior_override)||prior_override<0)
            throw std::runtime_error("invalid options");
        if(std::filesystem::exists(output)) throw std::runtime_error("output exists");
        if(!std::filesystem::path(output).parent_path().empty())
            std::filesystem::create_directories(std::filesystem::path(output).parent_path());
        std::ofstream out(output); out<<std::setprecision(9);
        std::ofstream trace;
        if(!trace_path.empty()) {
            if(std::filesystem::exists(trace_path)) throw std::runtime_error("trace exists");
            trace.open(trace_path);trace<<"case,ms,error,manual,final,requested,position,motion,response,mode\n";
        }
        out<<"{\"schema\":\"oscillation-scan-v1\",\"seed\":"<<seed
           <<",\"receipt\":\""<<(quantized_receipt?"ds4_decoded":"legacy_float")<<"\""
           <<",\"plant_source\":\"assumption\",\"boundary\":";
        const auto boundary=boundaries(out);
        out<<",\"max_boundary_step\":"<<boundary.max_step
           <<",\"boundary_triggers\":"<<boundary.triggers<<",\"counterfactuals\":"<<boundary.controls
           <<",\"passthrough_loss\":"<<boundary.passthrough_loss;
        auto config=load_runtime_config(config_path).gamepad;
        if(prior_override>0) config.ai_aim.aim_response_initial_scale=prior_override;
        out<<",\"initial_response\":"<<config.ai_aim.aim_response_initial_scale<<",\"cases\":[";
        std::mt19937 rng(seed);
        int written=0;
        bool stationary_pass=true;
        int stationary_triggers=0, stationary_controls=0;
        // Promoted from seed 20260926/case 768. Fixed explicit covariates avoid
        // tying the regression to a standard library's random distribution.
        // Both axes/signs run the actual controller, then change ONLY either
        // plant delay or configured response prior in each negative control.
        if(stationary_incident) {
            if(case_id>=0 || fire_override>=0 || effect_override>=0 || prior_override>0)
                throw std::runtime_error("stationary fixture covariates are frozen");
            for(int a : {0,1}) for(float sign : {-1.f,1.f}) for(int control=0;control<3;++control) {
                Spec spec{written,a,10000,16,8,control==1?9:20,1,0,
                    918.611084f,1,0,0,sign*39.4805908f,0,true,false};
                auto fixture_config=config;
                fixture_config.ai_aim.aim_response_initial_scale=control==2?spec.response:650.f;
                if(written++) out<<',';
                const auto m=closed_loop(spec,fixture_config,out,trace.is_open()?&trace:nullptr,quantized_receipt);
                if(m.body<8000 || m.observer<8000)
                    throw std::runtime_error("stationary fixture did not exercise sustained BodyLock observer");
                const bool stable=m.acquired>=0 && m.acquired<=250 && m.crossings<=4 && m.overshoot<=8.f;
                if(control==0) {++stationary_triggers;stationary_pass=stationary_pass&&stable;}
                else {++stationary_controls;if(!stable) throw std::runtime_error("stationary negative control failed");}
            }
        }
        for(int i=0;!stationary_incident && i<cases;++i) {
            auto spec=spec_for(i,rng);
            if(case_id>=0 && i!=case_id) continue;
            if(fire_override>=0) spec.fire=fire_override!=0;
            if(effect_override>=0) spec.effect_ms=effect_override;
            if(written++) out<<',';
            closed_loop(spec,config,out,trace.is_open()?&trace:nullptr,quantized_receipt);
        }
        const bool boundary_pass=boundary.triggers==72 && boundary.controls==288 &&
            boundary.max_step<=.02f && boundary.passthrough_loss<=1e-6f;
        const bool pass=stationary_incident?stationary_pass:boundary_pass;
        out<<"],\"boundary_pass\":"<<(boundary_pass?"true":"false")
           <<",\"stationary_incident\":"<<(stationary_incident?"true":"false")
           <<",\"stationary_triggers\":"<<stationary_triggers
           <<",\"stationary_controls\":"<<stationary_controls
           <<",\"stationary_incident_pass\":"<<(stationary_pass&&stationary_incident?"true":"false")<<"}\n";
        if(!out) throw std::runtime_error("report write failed");
        std::cout<<"boundary="<<boundary.max_step<<" cases="<<written<<"\n";
        return pass?0:1;
    } catch(const std::exception& ex) {std::cerr<<ex.what()<<"\n";return 2;}
}
