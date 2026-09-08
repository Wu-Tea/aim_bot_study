#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_cod_default_profile.h"
#include "controller_native/incident_fixture_support.h"
#include "controller_native/bodylock_follow_controller.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mouse_native;
namespace c = controller_native;
namespace f = c::incident_fixture;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
// Compile the identical fixture on the pre-policy headers for the RED proof.
template<class T> auto tolerance(T& v, int) -> decltype(v.bodylock_point_tolerance_px=3.0f, void()) {
    v.bodylock_point_tolerance_px=3.0f;
}
template<class T> void tolerance(T&, long) {}

MouseControllerFacadeConfig config(bool enabled=true) {
    MouseControllerFacadeConfig v; v.tuning={2,4,.5f,180,40,25};
    v.controller.ai_aim.body_lock_max_ai_force=.60f;
    v.controller.ai_aim.body_lock_max_ai_force_y=.66f;
    if (enabled) tolerance(v.tuning,0);
    return v;
}
struct Rig {
    MouseControllerFacade facade;
    MouseResponseProfile response=make_cod_default_profile({},MouseAimMode::Ads);
    f::TargetSpec target;
    double now=1; std::uint64_t tick=0;
    explicit Rig(bool enabled=true):facade(config(enabled)) {
        target.observation_id=77;target.selector_generation=9;
        target.body_width=300;target.body_height=700;
        target.color_classified=target.has_enemy_cue=target.enemy_identity_confirmed=true;
    }
    MouseControllerTickResult step(float x,float y,bool fresh=true,MouseSourceCounts m={},bool ads=true,bool present=true) {
        now+=.001;++tick;
        if(fresh) facade.submit_vision_snapshot(present
            ? f::observed_snapshot(target,tick,now,x,y):f::empty_snapshot(target,tick,now));
        return facade.tick({m,response,now,tick,ads,false});
    }
    void acquire() {
        for(int i=0;i<100;++i) step(0,0);
        check(facade.controller().last_ai_aim_mode()=="body_lock","must enter BodyLock");
    }
};
bool solver(std::ostream& out) {
    bool pass=true;
    for(int axis=0;axis<2;++axis) for(int sign:{-1,1}) {
        c::BodylockFollowControllerConfig cfg;
        cfg.max_force_x=.30f;cfg.max_force_y=.33f;
        cfg.feedback_range_x_px=cfg.feedback_range_y_px=24;
        cfg.fallback_response_px_per_stick_second=2000;
        cfg.response_curve.algorithm=c::AimResponseCurveAlgorithm::Linear;
        tolerance(cfg,0);
        c::BodylockFollowController owner(cfg);
        pipeline_contract::TargetPlan p;
        p.target_id=77;p.mode=pipeline_contract::ControlMode::BodyLockFollow;
        p.lifecycle=pipeline_contract::TargetLifecycle::Observed;
        p.aim_authority=p.reliability=1;
        p.bodylock_target_motion_valid=true;
        (axis?p.error_px.y:p.error_px.x)=sign*4.0f;
        (axis?p.bodylock_target_motion_px_per_sec.y:p.bodylock_target_motion_px_per_sec.x)=-sign*400;
        const auto r=owner.compute_detailed(p,{},.001f);
        const float u=(axis?-r.stick.y:r.stick.x)*sign;
        const float expected=.5f*4*(axis?.33f:.30f)/24;
        const bool ok=u>=expected-1e-6f;
        out<<"{\"case\":\"opposing_velocity\",\"axis\":"<<axis<<",\"sign\":"<<sign
           <<",\"toward_point_u\":"<<u<<",\"minimum_u\":"<<expected<<",\"pass\":"<<ok<<"}\n";
        pass &= ok;
        float maximum=0;
        for(float e:{-2.9f,-1.0f,0.0f,1.0f,2.9f}) {
            (axis?p.error_px.y:p.error_px.x)=e;
            const auto v=owner.compute(p,{},.001f);
            maximum=std::max(maximum,std::fabs(axis?v.y:v.x));
        }
        const bool quiet=maximum<1e-7f;
        out<<"{\"case\":\"tolerance_velocity_noise\",\"axis\":"<<axis
           <<",\"sign\":"<<sign<<",\"maximum_u\":"<<maximum<<",\"pass\":"<<quiet<<"}\n";
        pass &= quiet;
    }
    return pass;
}
bool facade(std::ostream& out) {
    bool pass=true;
    for(int axis=0;axis<2;++axis) for(int period:{1,16,33}) {
        Rig v, legacy(false);v.acquire();legacy.acquire();
        int noise=0,old_noise=0;
        for(int i=0;i<300;++i) {
            const float e=((i/std::max(16,period))%2 ? -2.0f:2.0f);
            auto a=v.step(axis?0:e,axis?e:0,i%period==0);
            auto b=legacy.step(axis?0:e,axis?e:0,i%period==0);
            if(i>=50) {noise+=std::abs(a.actuation.dx)+std::abs(a.actuation.dy);
                old_noise+=std::abs(b.actuation.dx)+std::abs(b.actuation.dy);}
        }
        const bool quiet=noise==0 && old_noise>0;
        out<<"{\"case\":\"small_point_jitter\",\"axis\":"<<axis<<",\"frame_ms\":"<<period
           <<",\"counts_after_settle\":"<<noise<<",\"legacy_counts\":"<<old_noise<<",\"pass\":"<<quiet<<"}\n";
        pass &= quiet;
        for(int sign:{-1,1}) {
            // R encloses the reticle even at 12 px error. Keyboard strafe is
            // represented by screen target displacement, with physical M=0.
            Rig moving;moving.acquire(); int toward=0;bool inside=true;
            for(int i=0;i<100;++i) {
                auto a=moving.step(axis?0:sign*12,axis?sign*12:0,i%period==0);
                auto p=moving.facade.controller().last_target_plan();
                auto r=p.aim_region_px;
                const float x=p.aim_px.x-p.error_px.x,y=p.aim_px.y-p.error_px.y;
                inside &= x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h;
                if(i>=40) toward+=(axis?a.actuation.dy:a.actuation.dx)*sign;
            }
            check(inside,"reticle must remain inside aim region");
            const bool ok=toward>0;
            out<<"{\"case\":\"inside_region_point_error\",\"axis\":"<<axis<<",\"sign\":"<<sign
               <<",\"frame_ms\":"<<period<<",\"inside_region\":"<<inside<<",\"toward_counts\":"<<toward<<",\"pass\":"<<ok<<"}\n";
            pass &= ok;
            const auto released=moving.step(0,0,true,{17,-9},false);
            check(released.actuation.dx==17 && released.actuation.dy==-9,"RMB release must be immediate native input");
        }
    }
    return pass;
}
}
int main(int argc,char** argv) {
    try {
        std::ofstream file;if(argc>1) file.open(argv[1]);
        std::ostream& out=argc>1 ? file:std::cout;
        const bool a=solver(out),b=facade(out);
        return a && b ? 0:1;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 2; }
}
