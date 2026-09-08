#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"
#include "controller_native/bodylock_follow_controller.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mouse_native;
namespace c=controller_native;
namespace f=c::incident_fixture;
void check(bool v,const char* why) { if(!v) throw std::runtime_error(why); }
MouseControllerFacadeConfig config() {
    MouseControllerFacadeConfig v;v.tuning={2,4,.5f,180,40,25,3};
    v.controller.ai_aim.body_lock_max_ai_force=.60f;
    v.controller.ai_aim.body_lock_max_ai_force_y=.66f;
    return v;
}
f::TargetSpec target() {
    f::TargetSpec t;t.observation_id=77;t.selector_generation=9;
    t.body_width=300;t.body_height=700;
    t.color_classified=t.has_enemy_cue=t.enemy_identity_confirmed=true;
    return t;
}
void solver_boundaries() {
    c::BodylockFollowControllerConfig cfg;cfg.bodylock_point_tolerance_px=3;
    c::BodylockFollowController owner(cfg);
    pipeline_contract::TargetPlan p;p.target_id=77;
    p.mode=pipeline_contract::ControlMode::BodyLockFollow;
    p.lifecycle=pipeline_contract::TargetLifecycle::Observed;
    p.aim_authority=p.reliability=1;p.bodylock_target_motion_valid=true;
    p.bodylock_target_motion_px_per_sec={10000,-10000};
    for(int axis=0;axis<2;++axis) for(int sign:{-1,1}) {
        p.error_px={};(axis?p.error_px.y:p.error_px.x)=sign*3;
        auto u=owner.compute(p,{},.001f);
        check(u.x==0 && u.y==0,"tolerance edge is inclusive and motion cannot awaken a centered axis");
        (axis?p.error_px.y:p.error_px.x)=sign*3.001f;
        u=owner.compute(p,{},.001f);
        check((axis?-u.y:u.x)*sign>0,"outside tolerance must request point correction");
        check((axis?u.x:u.y)==0,"quiet axis must not spend the other axis's velocity");
    }
    p.aim_authority=0;
    auto u=owner.compute(p,{},.001f);
    check(u.x==0 && u.y==0,"point policy cannot restore revoked authority");
    p.aim_authority=1;p.lifecycle=pipeline_contract::TargetLifecycle::None;
    u=owner.compute(p,{},.001f);
    check(u.x==0 && u.y==0,"no target means no point control");
}
void moving_plant() {
    // Declared synthetic, linear and calibrated plant. This tests feedback
    // invariants under elapsed time and sparse observations, not live feel.
    for(int axis=0;axis<2;++axis) for(int tick_ms:{1,2,4,8}) for(int frame_ms:{16,33}) {
        MouseControllerFacade v(config()); auto t=target();
        auto p=make_cod_default_profile({},MouseAimMode::Ads);
        double now=1;std::uint64_t tick=0;float error=0;int frame_due=0;
        for(int elapsed=0;elapsed<160;elapsed+=tick_ms) {
            now+=tick_ms*.001;++tick;
            v.submit_vision_snapshot(f::observed_snapshot(t,tick,now,0,0));
            v.tick({{},p,now,tick,true,false});
        }
        check(v.controller().last_ai_aim_mode()=="body_lock","plant fixture must enter BodyLock");
        float maximum=0;int movement=0,late_stop_counts=0;
        for(int elapsed=0;elapsed<1600;elapsed+=tick_ms) {
            now+=tick_ms*.001;++tick;
            if(elapsed<900) error+=120*tick_ms*.001f;
            if(elapsed>=frame_due) {
                v.submit_vision_snapshot(f::observed_snapshot(t,tick,now,axis?0:error,axis?error:0));
                frame_due+=frame_ms;
            }
            const auto a=v.tick({{},p,now,tick,true,false});
            const int counts=axis?a.actuation.dy:a.actuation.dx;
            error-=counts*(axis?p.px_per_count_y:p.px_per_count_x);
            if(elapsed>200 && elapsed<900) {maximum=std::max(maximum,std::fabs(error));movement+=counts;}
            if(elapsed>=1450) late_stop_counts+=std::abs(counts);
        }
        check(maximum<20 && movement>100,"continued translation must be followed inside the box");
        check(std::fabs(error)<=3.5f && late_stop_counts==0,"stationary point must settle with no sustained oscillation");
        auto release=v.tick({{13,-7},p,now+.001,++tick,false,false});
        check(release.actuation.dx==13 && release.actuation.dy==-7,"release bypasses the ramp");
        std::cout<<"moving axis="<<axis<<" tick_ms="<<tick_ms<<" frame_ms="<<frame_ms
                 <<" maximum_error="<<maximum<<" final_error="<<error<<" stop_counts="<<late_stop_counts<<'\n';
    }
}
void manual_and_recoil() {
    auto cfg=config();cfg.recoil.enabled=true;cfg.recoil.counts_per_second=30;
    MouseControllerFacade v(cfg);auto t=target();auto p=make_cod_default_profile({},MouseAimMode::Ads);
    double now=1;std::uint64_t tick=0;
    auto step=[&](MouseSourceCounts m,bool fire,bool present=true) {
        now+=.001;++tick;
        v.submit_vision_snapshot(present?f::observed_snapshot(t,tick,now,0,0):f::empty_snapshot(t,tick,now));
        return v.tick({m,p,now,tick,true,fire});
    };
    for(int i=0;i<100;++i) step({},false);
    int down=0;
    for(int i=0;i<=1000;++i) {auto a=step({},true);down+=a.recoil.dy;
        check(a.actuation.dy==a.aim_counts.dy+a.recoil.dy,"recoil must be appended exactly once");}
    check(down==30,"tolerance may not swallow continuous recoil");
    int drag=0;
    for(int i=0;i<80;++i) drag+=step({1,0},false).actuation.dx;
    check(drag>=60,"deliberate slow mouse drag must retain manual authority");
    const auto lost=step({11,-8},false,false);
    check(lost.actuation.dx==11 && lost.actuation.dy==-8,"loss must return native input on the same tick");
}
}
int main() {try {solver_boundaries();moving_plant();manual_and_recoil();return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
