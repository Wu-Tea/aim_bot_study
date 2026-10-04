#include "assist_control_state_machine.h"
#include "aim_response_curve_plugin.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <random>
#include <stdexcept>

namespace {

void zero_target_ownership(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("zero-target-ownership.json"));
    report << std::setprecision(9) << "{\"fidelity\":\"production sole arbiter, no game plant\",\"cases\":[";
    unsigned count=0, failures=0, controls=0;
    for (unsigned seed : {20261003u,8675309u,2026100301u,3141592653u}) {
        std::mt19937 rng(seed);
        for (int index=0;index<64;++index) for (float weight : {0.f,.5f,1.f}) {
            controller_native::AssistControlStateMachineConfig config;
            config.use_gamepad_intent_for_arbitration=true;
            controller_native::AssistControlStateMachine arbiter(config);
            controller_native::AssistControlStateMachineInput input;
            input.activation=pipeline_contract::AssistActivation::Engaged;
            input.target_authoritative=input.fresh_observation=true;
            input.target_id=17;input.selector_target_generation=91;
            input.mode=pipeline_contract::ControlMode::BodyLockFollow;
            input.visual_authority=1;
            input.solver_hold_x=input.solver_hold_y=true;
            input.manual_stick={.005f+(rng()%1400)*.0001f,-.005f-(rng()%1400)*.0001f};
            input.manual_axis_activity={weight,weight};
            const auto zero=arbiter.update(input);
            const float deviation=std::max(std::fabs(zero.stick.x-input.manual_stick.x*weight),
                std::fabs(zero.stick.y-input.manual_stick.y*weight));
            const float epsilon=config.material_ai_axis_output*1.01f;
            input.ai_stick={-epsilon,epsilon};
            const auto owned=arbiter.update(input);
            const float cliff=std::hypot(zero.stick.x-owned.stick.x,zero.stick.y-owned.stick.y);
            input.target_authoritative=false;
            const auto free=arbiter.update(input);
            const bool raw=free.stick.x==input.manual_stick.x && free.stick.y==input.manual_stick.y;
            controls+=raw;
            const bool pass=deviation<1e-6f && raw && (weight!=0 || cliff<.0002f);
            failures+=!pass;
            report << (count++?",":"") << "{\"seed\":"<<seed<<",\"case\":"<<index
                <<",\"manual_weight\":"<<weight<<",\"deviation\":"<<deviation
                <<",\"zero_boundary_cliff\":"<<cliff<<",\"no_target_raw\":"<<raw<<",\"pass\":"<<pass<<"}";
        }
    }
    report << "],\"count\":"<<count<<",\"failed\":"<<failures<<",\"raw_controls\":"<<controls<<"}";
    report.close();
    if(failures || controls!=count) throw std::runtime_error("An authoritative zero target command must retain the same manual intent allocation as nonzero target commands");
}

void centered_native_hold(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("centered-native-hold.json"));
    report << std::setprecision(9) << "{\"fidelity\":\"complete native controller, assumed static noiseless plant; not video replay\",\"cases\":[";
    unsigned count=0,failures=0;float worst_error=0;
    for(unsigned seed : {20261003u,8675309u,2026100301u,3141592653u}) {
        for(int index=0;index<64;++index) for(int duration : {600,2400}) {
            std::mt19937 rng(seed+index*1009u+duration);
            const int axis=index%2,vision_ms=std::array<int,3>{5,8,16}[rng()%3];
            const int delay_ms=std::array<int,3>{0,5,15}[rng()%3];
            const float gain=std::array<float,3>{500,1000,1800}[rng()%3];
            const float manual=(rng()%2?1.f:-1.f)*(.02f+(rng()%1200)*.0001f);
            auto config=controller_native::incident_fixture::base_config(100,200);
            config.ai_aim.aim_response_learning_enabled=false;
            config.ai_aim.aim_response_initial_scale=gain;
            config.ai_aim.body_free_initial_scale=config.ai_aim.body_slow_initial_scale=gain;
            config.ai_aim.ads_free_initial_scale=config.ai_aim.ads_slow_initial_scale=gain;
            config.ai_aim.aim_response_effect_delay_ms=delay_ms;
            config.aim_response_curve.algorithm=(index/2)%2
                ? controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut
                : controller_native::AimResponseCurveAlgorithm::Linear;
            double now=10;
            controller_native::NativeGamepadController controller(config,&now);
            controller_native::incident_fixture::TargetSpec target;
            target.selector_generation=991;
            target.has_enemy_cue=target.enemy_identity_confirmed=true;
            std::deque<float> commands(delay_ms,0);
            float error=0,peak=0;int body_ticks=0,direct_ticks=0,held_ticks=0;
            bool identity_ok=true,finite=true;std::uint64_t identity=0,frame=0;
            for(int ms=0;ms<duration;++ms) {
                now=10+ms*.001;
                if(ms%vision_ms==0) {
                    target.observation_id=++frame;
                    auto snapshot=controller_native::incident_fixture::observed_snapshot(
                        target,frame,now,axis?0:error,axis?error:0,ms==0);
                    auto& candidate=snapshot.candidates[0];
                    candidate.has_motion_anchor=true;candidate.motion_anchor_score=.9f;
                    candidate.motion_anchor_px=candidate.aim_point_px;
                    controller.submit_vision_snapshot(snapshot);
                }
                // Prime the actual ADS->BodyLock lifecycle before adding the
                // low-intent wrong input. Both short and long cases alternate
                // its direction; physical input is never edited by the fixture.
                auto input=controller_native::incident_fixture::ads_input();
                if(ms>=64) {
                    const float value=((ms-64)/120)%2 ? -manual:manual;
                    if(axis) input.right_y=-value;else input.right_x=value;
                }
                const auto output=controller.build_output(input);
                const auto& plan=controller.last_target_plan();
                if(plan.target_id && !identity) identity=plan.target_id;
                if(identity) identity_ok &= plan.target_id==identity;
                body_ticks+=plan.mode==pipeline_contract::ControlMode::BodyLockFollow;
                direct_ticks+=plan.direct_person_observation;
                held_ticks+=ms>=64 && plan.mode==pipeline_contract::ControlMode::BodyLockFollow;
                finite &= controller_native::incident_fixture::finite_unit(output.right_x) &&
                    controller_native::incident_fixture::finite_unit(output.right_y);
                const auto command=controller_native::forward_aim_response_curve(
                    {output.right_x,output.right_y},config.aim_response_curve);
                commands.push_back(axis?-command.y:command.x);
                error-=gain*commands.front()*.001f;commands.pop_front();
                if(ms>=64) peak=std::max(peak,std::fabs(error));
            }
            now+=.001;
            auto released=controller_native::incident_fixture::ads_input(.12f,-.12f);
            released.left_trigger=0;
            const auto free=controller.build_output(released);
            const bool raw=free.right_x==released.right_x && free.right_y==released.right_y;
            const bool trigger=body_ticks>100 && held_ticks>100 && direct_ticks>10 && identity && identity_ok;
            const bool pass=trigger && finite && raw && peak<=.05f;
            failures+=!pass;worst_error=std::max(worst_error,peak);
            report << (count++?",":"") << "{\"seed\":"<<seed<<",\"case\":"<<index
                <<",\"duration_ms\":"<<duration<<",\"axis\":"<<axis<<",\"curve\":"<<static_cast<int>(config.aim_response_curve.algorithm)
                <<",\"vision_ms\":"<<vision_ms<<",\"delay_ms\":"<<delay_ms<<",\"gain\":"<<gain
                <<",\"manual\":"<<manual<<",\"body_ticks\":"<<body_ticks<<",\"held_ticks\":"<<held_ticks
                <<",\"direct_ticks\":"<<direct_ticks<<",\"peak_error_px\":"<<peak<<",\"raw_release\":"<<raw
                <<",\"trigger\":"<<trigger<<",\"finite\":"<<finite<<",\"pass\":"<<pass<<"}";
        }
    }
    report << "],\"count\":"<<count<<",\"failed\":"<<failures<<",\"maximum_error_px\":"<<worst_error<<"}";
    report.close();
    if(failures) throw std::runtime_error("A centered static native target must not be displaced by input below the confirmed intent band, with exact raw release preserved");
}

void hold_evidence_countercontrols(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("hold-evidence-countercontrols.json"));
    unsigned cases = 0;
    const auto check = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    for (unsigned seed : {20261003u, 8675309u, 2026100301u, 3141592653u}) {
        std::mt19937 rng(seed);
        for (int index = 0; index < 64; ++index) {
            controller_native::BodylockFollowController solver;
            pipeline_contract::TargetPlan plan;
            plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
            plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
            plan.aim_authority = plan.reliability = 1.0f;
            plan.response_scale = 1000.0f;
            plan.bodylock_target_motion_valid = true;
            const auto solve = [&] { return solver.compute_detailed(plan, {}, .001f); };
            auto held = solve();
            check(held.position_hold_x && held.position_hold_y,
                "Observed zero position and zero sustaining motion must request hold");

            const float motion = .01f + (rng() % 20000) * .01f;
            plan.bodylock_target_motion_px_per_sec = {motion, -motion};
            auto moving = solve();
            check(!moving.position_hold_x && !moving.position_hold_y,
                "A centered moving target still requires sustaining work");
            plan.bodylock_target_motion_px_per_sec = {};
            plan.error_px = {.00001f, -.00001f};
            auto correcting = solve();
            check(!correcting.position_hold_x && !correcting.position_hold_y,
                "Sub-material but unfinished positioning is not an exact hold");
            plan.error_px = {};
            plan.bodylock_target_motion_valid = false;
            auto unknown = solve();
            check(!unknown.position_hold_x && !unknown.position_hold_y,
                "Missing motion evidence cannot establish hold");
            plan.bodylock_target_motion_valid = true;
            plan.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
            auto cue = solve();
            check(!cue.position_hold_x && !cue.position_hold_y,
                "Identity-only cue cannot establish point hold");

            controller_native::AssistControlStateMachineConfig config;
            config.use_gamepad_intent_for_arbitration = true;
            controller_native::AssistControlStateMachine arbiter(config);
            controller_native::AssistControlStateMachineInput input;
            input.activation = pipeline_contract::AssistActivation::Engaged;
            input.target_authoritative = input.fresh_observation = true;
            input.mode = pipeline_contract::ControlMode::BodyLockFollow;
            input.target_id = 17; input.selector_target_generation = 91;
            input.manual_stick = {.02f + (rng() % 1300) * .0001f, -.35f};
            const float activity = std::array<float, 3>{0.0f, .5f, 1.0f}[index % 3];
            input.manual_axis_activity = {activity, 1.0f};
            // AI shaped zero with no hold evidence: transition/braking retains
            // the original physical proposal on both axes.
            auto transition = arbiter.update(input);
            check(transition.stick.x == input.manual_stick.x &&
                transition.stick.y == input.manual_stick.y,
                "A shaped zero without hold evidence must preserve manual authority");
            input.solver_hold_x = true;
            auto one_axis = arbiter.update(input);
            check(std::fabs(one_axis.stick.x - input.manual_stick.x * activity) < 1e-6f &&
                one_axis.stick.y == input.manual_stick.y,
                "Hold ownership must remain per axis with strong manual response immediate");
            input.solver_hold_y = true;
            auto strong_manual = arbiter.update(input);
            check(strong_manual.stick.y == input.manual_stick.y,
                "Full manual activity must remain exact even at an owned zero");
            input.activation = pipeline_contract::AssistActivation::Off;
            auto off = arbiter.update(input);
            check(off.stick.x == input.manual_stick.x && off.stick.y == input.manual_stick.y,
                "Lifecycle release must remain exact raw passthrough");
            ++cases;
        }
    }
    report << "{\"cases\":" << cases << ",\"assertions\":" << cases * 9
        << ",\"passed\":true,\"fidelity\":\"solver semantics and sole arbiter; no game plant\"}";
}

void native_hold_restarts_on_target_motion(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("native-hold-motion-restart.json"));
    report << std::setprecision(9) << "{\"cases\":[";
    unsigned count = 0, failures = 0;
    for (unsigned seed : {20261003u, 8675309u, 2026100301u, 3141592653u}) {
        for (int index = 0; index < 16; ++index) {
            std::mt19937 rng(seed + index * 1009u);
            const int axis = index % 2;
            const int vision_ms = std::array<int, 3>{5, 8, 16}[rng() % 3];
            const int delay_ms = std::array<int, 3>{0, 5, 15}[rng() % 3];
            const float gain = std::array<float, 3>{500, 1000, 1800}[rng() % 3];
            const float sign = rng() % 2 ? 1.0f : -1.0f;
            const int deadline = 4 * vision_ms + delay_ms + 2;
            auto config = controller_native::incident_fixture::base_config(100, 200);
            config.ai_aim.aim_response_learning_enabled = false;
            config.ai_aim.aim_response_initial_scale = gain;
            config.ai_aim.body_free_initial_scale = config.ai_aim.body_slow_initial_scale = gain;
            config.ai_aim.ads_free_initial_scale = config.ai_aim.ads_slow_initial_scale = gain;
            config.ai_aim.aim_response_effect_delay_ms = delay_ms;
            config.aim_response_curve.algorithm = (index / 2) % 2
                ? controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut
                : controller_native::AimResponseCurveAlgorithm::Linear;
            double now = 10;
            controller_native::NativeGamepadController controller(config, &now);
            controller_native::incident_fixture::TargetSpec target;
            target.selector_generation = 991;
            target.has_enemy_cue = target.enemy_identity_confirmed = true;
            std::deque<float> commands(delay_ms, 0);
            float error = 0, rest_peak = 0, tracking_peak = 0;
            int first_follow = -1, first_motion = -1, body_ticks = 0, rest_sources = 0;
            bool identity_ok = true, finite = true;
            std::uint64_t identity = 0, frame = 0;
            for (int ms = 0; ms < 1200; ++ms) {
                now = 10 + ms * .001;
                if (ms % vision_ms == 0) {
                    target.observation_id = ++frame;
                    auto snapshot = controller_native::incident_fixture::observed_snapshot(
                        target, frame, now, axis ? 0 : error, axis ? error : 0, ms == 0);
                    auto& candidate = snapshot.candidates[0];
                    candidate.has_motion_anchor = true; candidate.motion_anchor_score = .9f;
                    candidate.motion_anchor_px = candidate.aim_point_px;
                    controller.submit_vision_snapshot(snapshot);
                    rest_sources += ms >= 64 && ms < 200;
                }
                auto input = controller_native::incident_fixture::ads_input();
                if (ms >= 64) {
                    if (axis) input.right_y = sign * .08f;
                    else input.right_x = -sign * .08f;
                }
                const auto output = controller.build_output(input);
                const auto& plan = controller.last_target_plan();
                if (plan.target_id && !identity) identity = plan.target_id;
                if (identity) identity_ok &= plan.target_id == identity;
                body_ticks += plan.mode == pipeline_contract::ControlMode::BodyLockFollow;
                finite &= controller_native::incident_fixture::finite_unit(output.right_x) &&
                    controller_native::incident_fixture::finite_unit(output.right_y);
                const auto command = controller_native::forward_aim_response_curve(
                    {output.right_x, output.right_y}, config.aim_response_curve);
                const float delivered = axis ? -command.y : command.x;
                const float motion = axis ? plan.bodylock_target_motion_px_per_sec.y
                    : plan.bodylock_target_motion_px_per_sec.x;
                if (ms >= 200) {
                    if (first_follow < 0 && delivered * sign > 1.0e-4f) first_follow = ms - 200;
                    if (first_motion < 0 && plan.bodylock_target_motion_valid && motion * sign > 1.0e-4f)
                        first_motion = ms - 200;
                }
                commands.push_back(delivered);
                error += (ms >= 200 ? sign * gain * .06f * .001f : 0) - gain * commands.front() * .001f;
                commands.pop_front();
                if (ms >= 64 && ms < 200) rest_peak = std::max(rest_peak, std::fabs(error));
                if (ms >= 200) tracking_peak = std::max(tracking_peak, std::fabs(error));
            }
            now += .001;
            auto released = controller_native::incident_fixture::ads_input(.12f, -.12f);
            released.left_trigger = 0;
            const auto free = controller.build_output(released);
            const bool raw = free.right_x == released.right_x && free.right_y == released.right_y;
            const bool trigger = body_ticks > 100 && rest_sources >= 8 && identity && identity_ok;
            const bool pass = trigger && finite && raw && rest_peak <= .05f &&
                first_follow >= 0 && first_follow <= deadline && first_motion >= 0 && first_motion <= deadline;
            failures += !pass;
            report << (count++ ? "," : "") << "{\"seed\":" << seed << ",\"case\":" << index
                << ",\"axis\":" << axis << ",\"gain\":" << gain << ",\"vision_ms\":" << vision_ms
                << ",\"delay_ms\":" << delay_ms << ",\"deadline_ms\":" << deadline
                << ",\"first_follow_ms\":" << first_follow << ",\"first_motion_ms\":" << first_motion
                << ",\"rest_peak_px\":" << rest_peak << ",\"tracking_peak_px_informational\":" << tracking_peak
                << ",\"trigger\":" << trigger << ",\"finite\":" << finite << ",\"raw_release\":" << raw
                << ",\"pass\":" << pass << "}";
        }
    }
    report << "],\"count\":" << count << ",\"failed\":" << failures
        << ",\"fidelity\":\"complete native static-then-moving assumed noiseless plant; no live-game acceptance\"}";
    report.close();
    if (failures) throw std::runtime_error("Holding must not stop observation or timely following of newly effective target motion");
}
}

void register_zero_target_hold_incident(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock","zero_target_retains_manual_allocation",zero_target_ownership);
    registry.add_context_case("BaseBodyLock","centered_target_ignores_wrong_low_intent",centered_native_hold);
    registry.add_context_case("BaseBodyLock","hold_evidence_preserves_moving_and_manual_work",hold_evidence_countercontrols);
    registry.add_context_case("BaseBodyLock","native_hold_restarts_on_effective_motion",native_hold_restarts_on_target_motion);
}

#ifdef ZERO_TARGET_HOLD_STANDALONE
int main(int argc,char** argv) {
    native_test::Registry registry;register_zero_target_hold_incident(registry);
    return native_test::run(registry,argc,argv,"ZeroTargetHoldIncident");
}
#endif
