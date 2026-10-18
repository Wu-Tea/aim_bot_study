// Read-only policy experiment: links the unchanged production solver.
#include "controller_native/response_model_aim_solver.h"
#include "controller_native/bodylock_follow_controller.h"
#include "controller_native/assist_control_state_machine.h"
#include "controller_native/target_coordinator.h"
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace controller_native;
using namespace pipeline_contract;

void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
ResponseModelAimRequest request(float authority,float error,float velocity,int curve) {
    ResponseModelAimRequest r;
    r.authority=authority;r.error_px={error,0};r.relative_velocity_px_per_sec={velocity,0};
    r.response_px_per_stick_second=1000;r.arrival_horizon_seconds=.1f;
    r.range_position_response=r.delay_aware_velocity_response=true;
    r.position_range_px=210;r.minimum_position_stick=.5f;r.arrival_radius_px=2;
    r.motion_is_sustaining_target_motion=true;r.feedback_delay_seconds=.01f;
    r.response_curve.algorithm=curve?AimResponseCurveAlgorithm::CodDynamicLegacyLut:AimResponseCurveAlgorithm::Linear;
    return r;
}
ResponseModelAimOutput solve(ResponseModelAimRequest r,int policy) {
    // Counterfactual equations only, no product setting or changed library.
    // Policy 1 leaves the existing position gain but consumes qualified motion
    // once. Encoding the equation through the current API is NOT a patch plan.
    if(policy==1 && r.authority>0)r.relative_velocity_px_per_sec.x/=r.authority;
    // Policy 2 interprets authority solely as the same final physical budget.
    if(policy==2) {r.max_force={r.authority,r.authority};r.authority=1;}
    return solve_response_model_aim(r);
}
int main(int argc,char**argv) {
    try {
        check(argc==2,"output directory required");std::filesystem::create_directories(argv[1]);
        std::ofstream points(std::string(argv[1])+"/point.csv");
        points<<"curve,authority,velocity,policy,demand,pre_curve,output,camera_velocity,limited\n";
        unsigned count=0;
        for(int curve:{0,1})for(float a:{0.f,.0736f,.45f,.92f,1.f})
        for(float v:{0.f,10.f,30.f,60.f,100.f,200.f})for(int p:{0,1,2}) {
            auto r=request(a,0,v,curve);auto o=solve(r,p);
            float camera=forward_aim_response_curve(o.stick,r.response_curve).x*1000;
            check(std::isfinite(camera)&&std::fabs(o.stick.x)<=a+1e-6f,"changed authority envelope");
            if(!curve && a==.0736f && v==60 && p==0)check(std::fabs(o.stick.x-.004416f)<1e-6f,"production counterexample changed");
            points<<curve<<','<<a<<','<<v<<','<<p<<','<<o.motion_stick.x<<','<<o.pre_curve_stick.x<<','<<o.stick.x<<','<<camera<<','<<o.limited<<'\n';++count;
        }
        BodylockFollowControllerConfig cfg;cfg.range_position_response=true;cfg.minimum_position_stick=.5f;
        cfg.response_time_x_seconds=cfg.response_time_y_seconds=.1f;
        cfg.response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        TargetPlan plan;plan.mode=ControlMode::BodyLockFollow;plan.lifecycle=TargetLifecycle::Observed;
        plan.target_id=1;plan.reliability=.92f;plan.aim_authority=plan.visual_authority=.0736f;
        plan.response_scale=1000;plan.position_response_radius_px=210;plan.position_arrival_radius_px=2;
        plan.bodylock_target_motion_valid=true;plan.bodylock_target_motion_px_per_sec={60,0};
        auto output=BodylockFollowController(cfg).compute(plan);
        check(std::fabs(output.x-.004416f)<1e-6f,"full BodyLock disagrees with solver counterexample");
        AssistControlStateMachineConfig ac;ac.use_gamepad_intent_for_arbitration=true;
        AssistControlStateMachine arbiter(ac);AssistControlStateMachineInput input;
        input.activation=AssistActivation::Engaged;input.target_authoritative=input.fresh_observation=true;
        input.target_id=1;input.selector_target_generation=1;input.now_seconds=10;
        input.mode=ControlMode::BodyLockFollow;input.visual_authority=.0736f;input.ai_stick=output;
        auto delivered=arbiter.update(input);
        check(std::fabs(delivered.stick.x-output.x)<1e-6f,"neutral manual arbitration attenuated twice");
        TargetCoordinatorConfig tc;tc.settle_frames=1;TargetCoordinator coordinator(tc);
        coordinator.begin_ads_epoch(1,10);IntentState intent;intent.activation=AssistActivation::Engaged;
        VisionObservationBatch batch;batch.frame_width_px=480;batch.frame_height_px=416;
        batch.capture_fresh=batch.selector_identity_protocol=batch.selector_enemy_cue_checked=true;
        batch.selector_target_generation=1;batch.preferred_source_id=1;batch.count=1;
        auto& c=batch.candidates[0];c.source_id=1;c.has_aim_point=c.has_aim_region=c.has_body_box=c.body_cue=true;
        c.aim_region_source=AimRegionSource::VisionGeometry;c.reliability=c.confidence=.92f;
        c.normalized_size=.25f;c.box_size_px={48,80};
        std::ofstream boundaries(std::string(argv[1])+"/coordinator.csv");
        boundaries<<"error,authority,mode,target_id\n";
        for(float e:{0.f,69.9f,70.1f}) {
            ++batch.frame_id;batch.source_time_seconds=batch.publish_time_seconds=10+batch.frame_id*.01;
            c.aim_px={240+e,208};c.aim_region_px=c.body_box_px={c.aim_px.x-24,168,48,80};
            const auto p=coordinator.update(batch,intent,batch.source_time_seconds);
            check(p.mode==ControlMode::BodyLockFollow,"boundary probe not in BodyLock");
            check(std::fabs(p.aim_authority-(e<=70?.0736f:.2208f))<1e-6f,"authority producer changed");
            boundaries<<e<<','<<p.aim_authority<<','<<int(p.mode)<<','<<p.target_id<<'\n';
        }
        std::ofstream loop(std::string(argv[1])+"/loop.csv");
        loop<<"duration_ms,velocity,policy,late_mean_abs_error,late_mean_output,peak_output\n";
        for(int duration:{2000,20000})for(float v:{10.f,30.f,60.f,100.f})for(int policy:{0,1,2}) {
            float error=0,peak=0;double sum=0,work=0;unsigned ticks=0;
            std::deque<float> pending(10);
            for(int t=0;t<duration;++t) {
                error+=(v-1000*pending.front())*.001f;pending.pop_front();
                auto r=request(.0736f,error,v,0);auto out=solve(r,policy).stick.x;
                check(std::isfinite(out)&&std::fabs(out)<=.073601f,"loop expanded budget");
                pending.push_back(out);peak=std::max(peak,std::fabs(out));
                if(t>=500){sum+=std::fabs(error);work+=out;++ticks;}
            }
            loop<<duration<<','<<v<<','<<policy<<','<<sum/ticks<<','<<work/ticks<<','<<peak<<'\n';
        }
        check(bool(points)&&bool(loop),"result write failed");
        std::cout<<"PASS "<<count<<" point policy cases; 24 isolated loops; production BodyLock and neutral manual counterexample\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
