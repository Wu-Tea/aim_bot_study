#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_dynamics_shaper.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mouse_native;
namespace f = controller_native::incident_fixture;
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
MouseControllerFacadeConfig config(bool enabled=true) {
    MouseControllerFacadeConfig c; c.tuning={2,4,0.5f};
    c.controller.ai_aim.body_lock_activation_box_px=120;
    c.controller.ai_aim.body_lock_max_ai_force=.60f;
    c.controller.ai_aim.body_lock_max_ai_force_y=.66f;
#ifndef MOUSE_BODYLOCK_CURVE_REFERENCE
    if(enabled) {
        c.tuning.bodylock_range_px=180;
        c.tuning.bodylock_accel_ms=40;
        c.tuning.bodylock_decel_ms=25;
    }
#else
    (void)enabled;
#endif
    return c;
}
struct Rig {
    MouseControllerFacade controller;
    MouseResponseProfile response=make_cod_default_profile({},MouseAimMode::Ads);
    f::TargetSpec target;
    double now=1; std::uint64_t tick=0;
    explicit Rig(bool enabled=true):controller(config(enabled)) {
        target.observation_id=77;target.selector_generation=9;
        target.body_width=24;target.body_height=50;
        target.color_classified=target.has_enemy_cue=target.enemy_identity_confirmed=true;
    }
    MouseControllerTickResult step(float x=0,float y=0,MouseSourceCounts m={},bool ads=true,bool present=true,bool fresh=true) {
        now+=.001;++tick;
        if(fresh) controller.submit_vision_snapshot(present
            ? f::observed_snapshot(target,tick,now,x,y) : f::empty_snapshot(target,tick,now));
        return controller.tick({m,response,now,tick,ads,false});
    }
    void acquire() {
        for(int i=0;i<100;++i) step();
        check(controller.controller().last_ai_aim_mode()=="body_lock","fixture must establish BodyLock");
    }
};
bool range(std::ostream& report) {
    bool passed=true;
    for(int axis=0;axis<2;++axis) {
        Rig candidate, reference(false); candidate.acquire();reference.acquire();
        int ctotal=0,rtotal=0;
        for(int i=0;i<70;++i) {
            const auto c=candidate.step(axis ? 0:160,axis ? 160:0);
            const auto r=reference.step(axis ? 0:160,axis ? 160:0);
            ctotal+=std::abs(axis ? c.actuation.dy:c.actuation.dx);
            rtotal+=std::abs(axis ? r.actuation.dy:r.actuation.dx);
        }
        const bool owns=candidate.controller.controller().last_ai_aim_mode()=="body_lock";
        const bool ok=owns && ctotal>0 && rtotal==0;
        report<<"{\"case\":\"range\",\"axis\":"<<axis<<",\"bodylock\":"<<owns
            <<",\"candidate_counts\":"<<ctotal<<",\"legacy_counts\":"<<rtotal<<",\"pass\":"<<ok<<"}\n";
        passed &= ok;
    }
    return passed;
}
bool dynamics(std::ostream& report) {
    bool passed=true;
    for(int axis=0;axis<2;++axis) for(int ms:{1,2,4,8}) {
        controller_native::AimDynamicsShaperConfig c;
        const float peak=axis ? .33f:.30f;
#ifndef MOUSE_BODYLOCK_CURVE_REFERENCE
        c.bodylock_accel_ms=40; c.bodylock_decel_ms=25;
        c.bodylock_max_force={.30f,.33f}; c.bodylock_authority_budget_scale=.5f;
#endif
        controller_native::AimDynamicsShaper shaper(c);
        pipeline_contract::TargetPlan p;p.target_id=77;
        p.mode=pipeline_contract::ControlMode::BodyLockFollow;
        p.lifecycle=pipeline_contract::TargetLifecycle::Observed;
        p.aim_authority=p.reliability=1;
        float previous=0,worst=0; bool bounded=true;float twenty_four=0;
        for(int elapsed=ms;elapsed<=40;elapsed+=ms) {
            const auto v=shaper.shape(axis ? pipeline_contract::Vec2f{0,peak}:pipeline_contract::Vec2f{peak,0},{},p,ms*.001f);
            const float out=axis ? v.y:v.x;
            const float expected=peak*elapsed/40;
            worst=std::max(worst,std::abs(out-expected));
            bounded &= out>=previous && out-previous<=peak*ms/40+1e-5f;
            if(elapsed==24) twenty_four=out;
            previous=out;
        }
        float decay_error=0;
        for(int elapsed=ms;elapsed<=32;elapsed+=ms) {
            const auto v=shaper.shape({}, {},p,ms*.001f);
            const float out=axis ? v.y:v.x;
            decay_error=std::max(decay_error,std::abs(out-peak*std::max(0.0f,1-elapsed/25.0f)));
            bounded &= out<=previous+1e-6f && out>=0;previous=out;
        }
        const bool ok=bounded && worst<1e-5f && decay_error<1e-5f;
        report<<"{\"case\":\"dynamics\",\"axis\":"<<axis<<",\"tick_ms\":"<<ms
            <<",\"at_24ms\":"<<twenty_four<<",\"ramp_error\":"<<worst
            <<",\"decay_error\":"<<decay_error<<",\"pass\":"<<ok<<"}\n";
        passed &= ok;
    }
    return passed;
}
void full_chain_boundaries() {
    for(int axis=0;axis<2;++axis) for(int frame_ms:{1,16,33}) {
        Rig rig;rig.acquire();float prior=0;int moved=0;
        const float peak=axis ? .33f:.30f;
        for(int i=0;i<120;++i) {
            const auto out=rig.step(axis ? 0:40,axis ? 40:0,{},true,true,i%frame_ms==0);
            const auto& d=rig.controller.controller().last_output_components();
            const float value=axis ? -d.shaped_assist_stick.y:d.shaped_assist_stick.x;
            check(std::abs(value-prior)<=peak/25+1e-5f,"full mouse chain must obey elapsed acceleration/braking limits");
            moved+=std::abs(axis ? out.actuation.dy:out.actuation.dx); prior=value;
        }
        check(moved>0,"full chain must deliver real counts with sparse source frames");
        const auto release=rig.step(40,40,{2,1},false);
        check(release.actuation.dx==2 && release.actuation.dy==1,"manual release must bypass AI ramp immediately");
        Rig loss;loss.acquire();for(int i=0;i<30;++i) loss.step(40,40);
        const auto out=loss.step(0,0,{1,2},true,false);
        check(out.actuation.dx==1 && out.actuation.dy==2,"target loss cannot coast with old AI output");
    }
}
}
int main(int argc,char** argv) {
    std::ofstream file;std::ostream* report=&std::cout;
    if(argc==2) { file.open(argv[1]);report=&file; }
    try {
        const bool r=range(*report), d=dynamics(*report);
#ifndef MOUSE_BODYLOCK_CURVE_REFERENCE
        full_chain_boundaries();
#endif
        return r && d ? 0:1;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
