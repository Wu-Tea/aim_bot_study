#include "aim_dynamics_shaper.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>

namespace {
void signal_contract(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("gamepad_signal_onset.json"));
    report << "{\"seeds\":[20261001,8675309],\"fidelity\":\"production AI dynamics component, no game plant\",\"cases\":[";
    unsigned count=0, failures=0, reversals=0, completed_samples=0;
    float worst_old_direction=0, worst_onset_fraction=0, worst_peer_delta=0;
    for (unsigned seed : {20261001u,8675309u}) {
        std::mt19937 rng(seed);
        for (int index=0;index<128;++index) for (int length : {32,192}) {
            controller_native::AimDynamicsShaperConfig config;
            controller_native::AimDynamicsShaper shaper(config);
            pipeline_contract::TargetPlan plan;
            plan.target_id=71;
            plan.mode=(index/2)%2 ? pipeline_contract::ControlMode::AdsAcquire : pipeline_contract::ControlMode::BodyLockFollow;
            plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
            plan.aim_authority=plan.reliability=1;
            const int axis=index%2;
            const float amplitude=.005f+(rng()%7451)*.0001f;
            const float peer=.005f+(rng()%4000)*.0001f;
            const float dt=(1+rng()%8)*.00025f;
            // Both the normalized phase and the existing absolute per-tick
            // force cap must have time to finish. At slower ticks the latter
            // can be the tighter bound for high-amplitude requests.
            const float completion_seconds=std::max(1.f/config.rise_slew_per_second,
                amplitude*1.2f*dt/config.max_step_per_tick);
            const auto vector=[&](float value) {
                return axis ? pipeline_contract::Vec2f{peer,value} : pipeline_contract::Vec2f{value,peer};
            };
            const auto component=[axis](pipeline_contract::Vec2f value) { return axis ? value.y:value.x; };
            const auto peer_component=[axis](pipeline_contract::Vec2f value) { return axis ? value.x:value.y; };
            const auto onset=shaper.shape(vector(amplitude),{},plan,dt);
            const float first_fraction=component(onset)/amplitude;
            float old_direction=0, peer_delta=0;
            bool finite=true, completed=true;
            for (int warm=0;warm<static_cast<int>(.08f/dt)+1;++warm)
                shaper.shape(vector(amplitude),{},plan,dt);
            float direction=1, elapsed=1, demand=amplitude;
            for (int step=0;step<length;++step) {
                const bool reverse=step%24==0;
                if (reverse) {direction=-direction;elapsed=0;++reversals;}
                // Continuous amplitude updates must not restart onset. Keep
                // those variations within the existing absolute slew limit.
                demand=std::clamp(demand+(static_cast<int>(rng()%3)-1)*.001f,
                    amplitude*.8f,amplitude*1.2f);
                const auto value=shaper.shape(vector(direction*demand),{},plan,dt);
                elapsed+=dt;
                const float actual=component(value);
                finite &= std::isfinite(actual) && std::fabs(actual)<=1;
                old_direction=std::max(old_direction,std::max(0.f,-direction*actual));
                peer_delta=std::max(peer_delta,std::fabs(peer_component(value)-peer));
                if (elapsed>=completion_seconds+dt) {
                    ++completed_samples;
                    completed &= std::fabs(actual-direction*demand)<.003f;
                }
            }
            // A neutral AI request owns no old-direction work. It must not
            // erase the independent continuing demand of the other axis.
            const auto released=shaper.shape(vector(0),{},plan,dt);
            const bool zero=std::fabs(component(released))<=1e-6f;
            const bool pass=finite && completed && zero && old_direction<=1e-6f &&
                peer_delta<=1e-6f && first_fraction>0 && first_fraction<=dt*config.rise_slew_per_second+1e-5f;
            failures+=!pass;
            worst_old_direction=std::max(worst_old_direction,old_direction);
            worst_onset_fraction=std::max(worst_onset_fraction,first_fraction);
            worst_peer_delta=std::max(worst_peer_delta,peer_delta);
            report << (count++?",":"") << "{\"seed\":"<<seed<<",\"case\":"<<index
                <<",\"ticks\":"<<length<<",\"dt_ms\":"<<dt*1000<<",\"amplitude\":"<<amplitude
                <<",\"onset_fraction\":"<<first_fraction<<",\"old_direction\":"<<old_direction
                <<",\"peer_delta\":"<<peer_delta<<",\"completed\":"<<completed<<",\"neutral_zero\":"<<zero
                <<",\"pass\":"<<pass<<"}";
        }
    }
    report << "],\"count\":"<<count<<",\"failed\":"<<failures<<",\"reversals\":"<<reversals
        <<",\"completed_samples\":"<<completed_samples<<",\"maximum_old_direction\":"<<worst_old_direction<<",\"maximum_first_fraction\":"<<worst_onset_fraction
        <<",\"maximum_peer_delta\":"<<worst_peer_delta<<"}";
    report.close();
    if (failures || completed_samples<1000) throw std::runtime_error("AI reversal must clear stale direction; onset must scale weak and strong signals by elapsed progress without resetting peer axes");
}

void native_onset_and_raw_manual() {
    auto config=controller_native::incident_fixture::base_config(100,200);
    config.ai_aim.aim_response_learning_enabled=false;
    config.aim_response_curve.algorithm=controller_native::AimResponseCurveAlgorithm::Linear;
    double now=10;
    controller_native::NativeGamepadController controller(config,&now);
    controller_native::incident_fixture::TargetSpec target;
    target.observation_id=target.selector_generation=1;
    target.has_enemy_cue=target.enemy_identity_confirmed=true;
    controller.submit_vision_snapshot(controller_native::incident_fixture::observed_snapshot(target,1,now,6,0,true));
    controller.build_output(controller_native::incident_fixture::ads_input());
    const auto& components=controller.last_output_components();
    if (!(components.requested_assist_stick.x>.001f && components.shaped_assist_stick.x>0 &&
        components.shaped_assist_stick.x<components.requested_assist_stick.x*.5f))
        throw std::runtime_error("native gamepad must use normalized startup for a small owned ADS request");
    for (float raw : {.8f,-.8f,.01f,-.01f,0.f}) {
        now+=.001;
        controller_native::PhysicalGamepadState input;
        input.connected=true;input.right_x=raw;input.right_y=-raw;
        const auto output=controller.build_output(input);
        if (output.right_x!=raw || output.right_y!=-raw)
            throw std::runtime_error("AI signal onset must preserve immediate zero-deadzone manual reversal/release");
    }
}
void direction_and_evidence_ownership() {
    controller_native::AimDynamicsShaperConfig config;
    controller_native::AimDynamicsShaper shaper(config);
    pipeline_contract::TargetPlan plan;
    plan.target_id=17;
    plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
    plan.mode=pipeline_contract::ControlMode::AdsAcquire;
    for (int step=0;step<100;++step) shaper.shape({-.6f,.2f},{},plan,.001f);
    plan.mode=pipeline_contract::ControlMode::BodyLockFollow;
    const auto handed=shaper.shape({.2f,.2f},{},plan,.001f);
    if (!(handed.x>0 && handed.x<=.2f*.064f+1e-6f && std::fabs(handed.y-.2f)<1e-6f))
        throw std::runtime_error("mode handoff must cancel old direction before new onset, preserving a continuing peer axis");
    plan.lifecycle=pipeline_contract::TargetLifecycle::CueContinuation;
    for (int step=0;step<10;++step) {
        const auto held=shaper.shape({.8f,.2f},{},plan,.001f);
        if (std::fabs(held.x-handed.x)>1e-6f)
            throw std::runtime_error("continuation-only evidence must hold existing work without restarting or advancing onset");
    }
    const auto revoked=shaper.shape({-.8f,.2f},{},plan,.001f);
    const auto blind=shaper.shape({-.8f,.2f},{},plan,.001f);
    if (revoked.x!=0 || blind.x!=0 || std::fabs(blind.y-.2f)>1e-6f)
        throw std::runtime_error("continuation can revoke a direction but cannot authorize a fresh direction");
    plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
    const auto recovered=shaper.shape({-.2f,.2f},{},plan,.001f);
    if (!(recovered.x<0 && recovered.x>=-.2f*.064f-1e-6f))
        throw std::runtime_error("fresh evidence must reestablish revoked direction through onset");
    for (int step=0;step<100;++step) shaper.shape({-.6f,.2f},{},plan,.001f);
    plan.target_id=18;
    const auto changed=shaper.shape({-.6f,.2f},{},plan,.001f);
    if (!(changed.x<0 && changed.x>=-.6f*.064f-1e-6f && changed.y<=.2f*.064f+1e-6f))
        throw std::runtime_error("new identity must not inherit the previous identity's onset progress");
    plan.lifecycle=pipeline_contract::TargetLifecycle::None;
    const auto stopped=shaper.shape({-.6f,.2f},{},plan,.001f);
    if (stopped.x!=0 || stopped.y!=0)
        throw std::runtime_error("revoked lifecycle must stop all AI work this tick");
}
}
int main(int argc, char** argv) {
    native_test::Registry registry;
    registry.add_context_case("OpenIncident","gamepad_signal_zero_and_onset",signal_contract);
    registry.add_case("OpenIncident","native_gamepad_onset_and_raw_manual",native_onset_and_raw_manual);
    registry.add_case("OpenIncident","gamepad_signal_evidence_and_handoff",direction_and_evidence_ownership);
    return native_test::run(registry,argc,argv,"GamepadSignalOnsetIncident");
}
