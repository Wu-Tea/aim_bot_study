// Reuse the unchanged production-chain fixture and recorded counterexamples.
#define main evidence_noise_fixture_main
#include "../evidence_noise_20261018/braking.cpp"
#undef main
#include "controller_native/bodylock_follow_controller.h"
void weak_focused(unsigned seed,const std::string& directory) {
    std::ofstream csv(directory+"/weak-focused-"+std::to_string(seed)+".csv");
    csv<<"seed,kind,nonlinear,duration,late_mean,late_rms,peak,within_half,delivered_work,handoffs,raw_measurement_rms,control_measurement_rms\n";
    unsigned failures=0;
    for(int kind=0;kind<5;++kind)for(int nonlinear=0;nonlinear<2;++nonlinear)for(int duration:{2000,20000}) {
        auto cfg=fixture::base_config(40,190);
        cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.ai_input_deadzone=diagnostic_noise_fraction;cfg.ai_aim.minimum_position_stick=.5f;cfg.ai_aim.arrival_radius_px=2;
        cfg.ai_aim.aim_response_learning_enabled=false;cfg.ai_aim.aim_response_effect_delay_ms=9;
        cfg.ai_aim.ads_free_initial_scale=cfg.ai_aim.ads_slow_initial_scale=2500;
        cfg.ai_aim.body_free_initial_scale=cfg.ai_aim.body_slow_initial_scale=2500;
        cfg.ai_aim.bodylock_response_time_x_ms=cfg.ai_aim.bodylock_response_time_y_ms=100;
        cfg.aim_response_curve.algorithm=nonlinear?AimResponseCurveAlgorithm::CodDynamicLegacyLut:AimResponseCurveAlgorithm::Linear;
        cfg.recoil.enabled=kind==3;cfg.recoil.output_amount=.2f;
        double now=10;NativeGamepadController controller(cfg,&now);
        fixture::TargetSpec target;target.observation_id=1;target.selector_generation=7;
        target.has_enemy_cue=target.enemy_identity_confirmed=false;
        target.color_classified=true;target.confidence=.92f;
        std::mt19937 rng(seed+kind*104729+duration+nonlinear);
        Vec2f error{kind==4?-70.f:kind==1?1.5f:0.f,kind==1?-2.2f:0.f};
        const float theta=float(rng()%6284)*.001f;
        const auto demand=forward_aim_response_curve({.04f*std::cos(theta),.04f*std::sin(theta)},cfg.aim_response_curve);
        const Vec2f target_velocity{2500.f*demand.x,-2500.f*demand.y};
        // Establish a centered BodyLock before the target starts moving; the
        // separate randomized suite still covers targets moving during ADS.
        std::deque<Vec2f> pending(9);
        std::ofstream trace;
        if(duration==2000) {
            trace.open(directory+"/weak-trace-"+std::to_string(seed)+"-"+std::to_string(kind)+"-"+std::to_string(nonlinear)+".csv");
            trace<<"tick,error_x,error_y,mode,authority,motion_valid,target_motion_x,target_motion_y,response,request_x,request_y,out_x,out_y,fire,raw_x,raw_y,control_x,control_y,reserve_x,reserve_y\n";
        }
        double sum=0,square=0,peak=0,work=0,raw_noise=0,control_noise=0;
        int samples=0,inside=0,handoffs=0,captures=0;
        auto previous_mode=ControlMode::Manual;
        for(int tick=0;tick<duration;++tick) {
            now=10+tick*.001;const auto command=pending.front();pending.pop_front();
            error.x+=(kind==2 && tick>=300?target_velocity.x:0.f)*.001f-command.x*2.5f;
            error.y+=(kind==2 && tick>=300?target_velocity.y:0.f)*.001f+command.y*2.5f;
            auto input=fixture::ads_input();input.right_trigger=kind==3 && tick%1000>400 && tick%1000<800?1.f:0.f;
            if(input.right_trigger>0)error.y+=.2f*2.5f;
            if(tick%10==0) {
                const float noise=kind==0?.35f:kind==2?.2f:kind==1 && nonlinear?.1f:0.f;
                const auto jitter=[&] {return noise*(int(rng()%2001)-1000)/1000.f;};
                controller.submit_vision_snapshot(fixture::observed_snapshot(target,tick/10+1,now,error.x+jitter(),error.y+jitter()));
            }
            auto output=controller.build_output(input);
            const auto& plan=controller.last_target_plan();
            check(valid(plan),"focused plan invalid");
            if(plan.mode==ControlMode::BodyLockFollow && std::hypot(plan.error_px.x,plan.error_px.y)<=70.f)
                check(std::fabs(plan.aim_authority-.0736f)<1e-6f,"focused test must exercise checked weak 7.36% authority");
            check(plan.source_aim_px.x==plan.aim_px.x && plan.source_aim_px.y==plan.aim_px.y,"focused chest changed");
            if(previous_mode==ControlMode::AdsAcquire && plan.mode==ControlMode::BodyLockFollow)++handoffs;
            previous_mode=plan.mode;
            if(trace.is_open()) {
                const auto& c=controller.last_output_components();
                trace<<tick<<','<<error.x<<','<<error.y<<','<<int(plan.mode)<<','<<plan.aim_authority<<','<<plan.bodylock_target_motion_valid<<','<<plan.bodylock_target_motion_px_per_sec.x<<','<<plan.bodylock_target_motion_px_per_sec.y<<','<<plan.response_scale<<','<<c.requested_assist_stick.x<<','<<c.requested_assist_stick.y<<','<<output.right_x<<','<<output.right_y<<','<<input.right_trigger<<','<<plan.error_px.x<<','<<plan.error_px.y<<','<<c.control_error_px.x<<','<<c.control_error_px.y<<','<<c.stopping_reserve_px.x<<','<<c.stopping_reserve_px.y<<'\n';
            }
            controller.observe_delivered_output(output,true,now);
            pending.push_back(forward_aim_response_curve({output.right_x,output.right_y},cfg.aim_response_curve));
            if(tick>=500) {
                const float distance=std::hypot(error.x,error.y);
                sum+=distance;square+=distance*distance;peak=std::max(peak,double(distance));
                work+=std::hypot(output.right_x,output.right_y)*.001;
                ++samples;inside+=distance<.5f;
                if(tick%10==0 && kind==0) {
                    const auto& comp=controller.last_output_components();
                    raw_noise+=std::pow(plan.error_px.x-error.x,2)+std::pow(plan.error_px.y-error.y,2);
                    control_noise+=std::pow(comp.control_error_px.x-error.x,2)+std::pow(comp.control_error_px.y-error.y,2);
                    ++captures;
                }
            }
        }
        csv<<seed<<','<<kind<<','<<nonlinear<<','<<duration<<','<<sum/samples<<','<<std::sqrt(square/samples)<<','<<peak<<','<<double(inside)/samples<<','<<work<<','<<handoffs<<','<<std::sqrt(raw_noise/std::max(1,captures))<<','<<std::sqrt(control_noise/std::max(1,captures))<<'\n';
        if(kind==1 && sum/samples>=.5)++failures;
        if(kind==0 && std::sqrt(square/samples)>=.5)++failures;
        if(kind==0 && diagnostic_noise_fraction>0 && control_noise>=raw_noise)++failures;
        if(kind==2 && (sum/samples>=1 || !handoffs))++failures;
        if(kind==4 && !handoffs)++failures;
    }
    check(!failures,"focused noise/convergence/motion/handoff counterexample failed (see CSV)");
    std::cout<<"PASS 20 weak-evidence known-camera focused cases, 220000 ticks\n";
}

void ideal(unsigned seed,const std::string& directory) {
    std::ofstream csv(directory+"/ideal-"+std::to_string(seed)+".csv");
    csv<<"seed,curve,case,duration,velocity_x,velocity_y,late_mean,late_peak,peak_stick,authority\n";
    for(int curve=0;curve<2;++curve)for(int scenario=0;scenario<12;++scenario)for(int duration:{2000,20000}) {
        std::mt19937 rng(seed+scenario*104729+curve);
        BodylockFollowControllerConfig cfg;cfg.range_position_response=true;
        cfg.response_time_x_seconds=cfg.response_time_y_seconds=.1f;
        cfg.minimum_position_stick=.5f;
        cfg.response_curve.algorithm=curve?AimResponseCurveAlgorithm::CodDynamicLegacyLut:AimResponseCurveAlgorithm::Linear;
        TargetPlan p;p.target_id=1;p.position_response_radius_px=150;p.mode=ControlMode::BodyLockFollow;
        p.lifecycle=TargetLifecycle::Observed;p.reliability=.92f;p.aim_authority=.0736f;
        p.response_scale=1000;p.bodylock_target_motion_valid=true;
        const float theta=float(rng()%6284)*.001f;
        const float radius=scenario==0?.06f:scenario==11?.09f:.01f+float(rng()%5000)*.00001f;
        auto demand=forward_aim_response_curve({radius*std::cos(theta),radius*std::sin(theta)},cfg.response_curve);
        p.bodylock_target_motion_px_per_sec={1000*demand.x,-1000*demand.y};
        BodylockFollowController controller(cfg);std::deque<Vec2f> pending(9);
        double sum=0,peak=0,stick=0;int count=0;
        for(int tick=0;tick<duration;++tick) {
            auto sent=pending.front();pending.pop_front();
            p.error_px.x+=p.bodylock_target_motion_px_per_sec.x*.001f-sent.x;
            p.error_px.y+=p.bodylock_target_motion_px_per_sec.y*.001f+sent.y;
            const auto output=controller.compute(p);
            check(std::hypot(output.x,output.y)<=p.aim_authority+1e-6f,"ideal authority raised");
            pending.push_back(forward_aim_response_curve(output,cfg.response_curve));
            stick=std::max(stick,double(std::hypot(output.x,output.y)));
            if(tick>=500){const double e=std::hypot(p.error_px.x,p.error_px.y);sum+=e;peak=std::max(peak,e);++count;}
        }
        csv<<seed<<','<<curve<<','<<scenario<<','<<duration<<','<<p.bodylock_target_motion_px_per_sec.x<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<sum/count<<','<<peak<<','<<stick<<','<<p.aim_authority<<'\n';
    }
    std::cout<<"PASS 48 ideal 2D budget cases, 528000 ticks (case11 intentionally exceeds budget)\n";
}
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    std::filesystem::create_directories(argv[2]);
    int failed=0;const unsigned seed=std::stoul(argv[1]);
    try{ideal(seed,argv[2]);}catch(const std::exception&e){std::cout<<"FAIL ideal: "<<e.what()<<'\n';++failed;}
    try{weak_focused(seed,argv[2]);}catch(const std::exception&e){std::cout<<"FAIL weak focused: "<<e.what()<<'\n';++failed;}
    return failed?1:0;
}
