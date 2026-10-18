#include "fixture.h"
#include "controller_native/target_coordinator.h"
#include "controller_native/response_model_aim_solver.h"
#include "runtime_app/vision_controller_adapter.h"
#include "vision_native/target_selector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace controller_native;
using namespace pipeline_contract;
namespace fixture = controller_native::incident_fixture;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

void recorded_regression() {
    std::ifstream file(RECORDED_APPROACH_PATH);
    check(bool(file),"missing recorded geometry");
    std::string line;std::getline(file,line);unsigned checked=0;
    while(std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        int burst,source_line;unsigned long long capture;float dt,ex,ey,vx,vy;
        row>>burst>>source_line>>capture>>dt>>ex>>ey>>vx>>vy;
        check(bool(row),"invalid recorded geometry");
        // These are recorded fresh source displacements, not unlogged live
        // estimator state. Sweep independent response/curve assumptions.
        for(auto curve:{AimResponseCurveAlgorithm::Linear,AimResponseCurveAlgorithm::CodDynamicLegacyLut})
        for(float response:{500.f,1500.f,3000.f}) {
            ResponseModelAimRequest r;
            r.error_px={ex,ey};r.relative_velocity_px_per_sec={};
            r.response_px_per_stick_second=response;
            r.arrival_horizon_seconds=r.arrival_horizon_y_seconds=.060f;
            r.motion_weight=.2f;r.motion_is_error_rate_lookahead=true;
            r.range_position_response=true;r.position_range_px=210;
            r.minimum_position_stick=.5f;r.arrival_radius_px=2;
            r.response_curve.algorithm=curve;
            const auto base=forward_aim_response_curve(solve_response_model_aim(r).stick,r.response_curve);
            // Isolate floor-owned work, below the unsaturated pursuit reference.
            // A joint force cap can legitimately release orthogonal demand when
            // X shrinks; that separate envelope must not be mistaken for floor
            // redistribution. The floor itself must not create new Y work.
            const auto unbraked=inverse_aim_response_curve(
                {ex/(response*.060f),-ey/(response*.060f)},r.response_curve);
            if(ex*vx<0 && std::fabs(ex)>1 && std::fabs(ey)>1 &&
               std::hypot(unbraked.x,unbraked.y)<r.minimum_position_stick) {
                r.relative_velocity_px_per_sec.x=std::clamp(vx,-4000.f,4000.f);
                const auto result=forward_aim_response_curve(solve_response_model_aim(r).stick,r.response_curve);
                check(std::fabs(result.x)<=std::fabs(base.x)+.0001f,"recorded approach amplified braked axis");
                check(std::fabs(result.y-base.y)<.0001f,"recorded X brake redistributed into Y");
                ++checked;
            }
        }
    }
    check(checked>50,"recorded fixture missed approach evidence");
    std::cout<<"PASS recorded approach: "<<checked<<" curve/response cases\n";
}

void simulations(unsigned seed,const std::string& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory+"/seed-"+std::to_string(seed)+".csv");
    csv<<"seed,case,duration_ms,kind,nonlinear,floor,enemy_evidence,response,effect_delay_ms,scope_ms,scope_gain,mean_error,mean_late_error,within_4_fraction,center_crossings,ads_ticks,bodylock_ticks,first_20_ms,first_2_ms,max_delta,initial_error_x,initial_error_y,first_stable_4_ms,late_peak,late_rms,handoffs,post_handoff_peak\n";
    for(int duration:{2000,20000}) for(int scenario=0;scenario<32;++scenario) {
        std::mt19937 rng(seed ^ (scenario*104729u+duration));
        const int kind=scenario/8;
        const bool nonlinear=scenario%2;
        const bool enemy=kind==0 || scenario%2;
        const float response=900.f+rng()%1901;
        const int delay=6+rng()%25,scope_ms=250+rng()%401;
        const float scope_gain=.45f+float(rng()%46)*.01f;
        const float floor=kind==0?.5f:std::array<float,3>{.2f,.5f,.7f}[scenario%3];
        auto cfg=fixture::base_config(40,190);
        cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.minimum_position_stick=floor;cfg.ai_aim.arrival_radius_px=2;
        cfg.ai_aim.ads_response_time_ms=60;
        cfg.ai_aim.bodylock_response_time_x_ms=cfg.ai_aim.bodylock_response_time_y_ms=10;
        cfg.ai_aim.ads_output_limit_x=cfg.ai_aim.ads_output_limit_y=1;
        cfg.ai_aim.bodylock_output_limit_x=cfg.ai_aim.bodylock_output_limit_y=1;
        cfg.ai_aim.ads_free_initial_scale=1400;cfg.ai_aim.ads_slow_initial_scale=900;
        cfg.ai_aim.body_free_initial_scale=2000;cfg.ai_aim.body_slow_initial_scale=1500;
        cfg.aim_response_curve.algorithm=nonlinear?AimResponseCurveAlgorithm::CodDynamicLegacyLut:AimResponseCurveAlgorithm::Linear;
        cfg.recoil.enabled=kind==2;cfg.recoil.output_amount=.2f;
        double now=10;NativeGamepadController controller(cfg,&now);
        vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);
        std::deque<Vec2f> motion(delay);
        std::deque<std::pair<int,ControllerVisionSnapshot>> pending;
        float ex=0,ey=0,initial_x=0,initial_y=0,previous_side=0,max_delta=0;
        GamepadOutputState previous{};
        double error_sum=0,late_sum=0;unsigned late_count=0,inside=0,crossings=0,ads=0,body=0,frame=0;
        int next_capture=0,first20=-1,first2=-1,first_stable=-1,stable_run=0,last_handoff=-1000,handoffs=0;
        float late_peak=0,post_handoff_peak=0;double late_squared=0;
        auto previous_mode=ControlMode::Manual;
        std::ofstream trace;
        if(duration==2000 && scenario<2) {
            trace.open(directory+"/trace-"+std::to_string(seed)+"-"+std::to_string(scenario)+".csv");
            trace<<"tick,error_x,error_y,output_x,output_y,mode,authority,source_age_ms,manual_exit,source_error_x,source_error_y,target_motion_x,target_motion_y,response,request_x,request_y\n";
        }
        for(int tick=0;tick<duration;++tick) {
            now=10+tick*.001;const int phase=tick%1625;
            if(phase==0) {
                ex=(scenario%2 ? -95.6667f : -136.125f)+float(int(rng()%21)-10);
                ey=(scenario%2 ? -14.2f : -27.f)+float(int(rng()%11)-5);
                previous_side=0;
                if(tick==0){initial_x=ex;initial_y=ey;}
            }
            // Point truth lives in an unzoomed camera plane. Scope projection
            // and sensitivity change independently of the controller estimate.
            const float scope=std::clamp(float(phase-60)/scope_ms,0.f,1.f);
            const float zoom=1.f+.4f*scope;
            const float gain=response*(1.f+(scope_gain-1.f)*scope);
            auto applied=motion.front();motion.pop_front();
            const float vx=kind==1?(scenario%4?75.f:140.f)*std::sin(tick*.004f+scenario):0.f;
            const float vy=kind==1?30.f*std::cos(tick*.003f+scenario):0.f;
            ex=std::clamp(ex+(vx-gain*applied.x)*.001f,-300.f,300.f);
            ey=std::clamp(ey+(vy+gain*applied.y)*.001f,-220.f,220.f);
            PhysicalGamepadState input=fixture::ads_input();input.left_trigger=phase<1500?1.f:0.f;
            input.right_x=kind==3 && phase>350 && phase<1000?(scenario%2?.2f:-.6f):0.f;
            input.right_y=kind==3 && phase>650 && phase<1200?(scenario%3?.21f:-.45f):0.f;
            input.right_trigger=kind==2 && phase>400 && phase<1100?1.f:0.f;
            // Model the gun kick independently from controller compensation.
            if(input.right_trigger>0) {ey+=gain*.2f*.001f;if(phase%80==0)ey+=2.5f;}
            if(tick>=next_capture) {
                next_capture=tick+7+rng()%6;++frame;
                vision_native::DetectionBatch b;b.frame_width=640;b.frame_height=512;
                b.frame_id=frame;b.captured_at_ns=std::uint64_t(now*1e9);
                const int gap_ms=std::array<int,4>{0,22,35,75}[scenario%4];
                const bool gap=phase>=1575 || (kind>=2 && phase>=450 && phase<450+gap_ms);
                if(!gap) {
                    vision_native::Detection d;
                    const float w=33*zoom,h=74*zoom;
                    d.x1=320+ex*zoom-w*.5f;d.x2=d.x1+w;
                    d.y1=256+ey*zoom-h*.2f;d.y2=d.y1+h;
                    // Logged flash/occlusion failure class, separate from truth.
                    if(kind==2 && phase>600 && phase<680){d.x1-=10;d.x2+=10;d.y1-=15;}
                    d.conf=.92f;d.color_classified=true;
                    d.is_friendly=kind==3 && scenario%4==0 && phase>900 && phase<1000;
                    if(enemy)d.color_bonus=.3f;
                    b.detections={d};
                }
                auto v=selector.select(b);const int latency=2+rng()%11;
                v.frame_id=frame;v.captured_at_ns=b.captured_at_ns;v.frame_updated=true;
                v.result_at_ns=b.captured_at_ns+std::uint64_t(latency)*1000000;
                pending.emplace_back(tick+latency,runtime_app::adapt_vision_result(v));
            }
            while(!pending.empty() && pending.front().first<=tick) {
                controller.submit_vision_snapshot(pending.front().second);pending.pop_front();
            }
            auto out=controller.build_output(input);
            const auto& p=controller.last_target_plan();const auto& comp=controller.last_output_components();
            check(valid(p),"invalid plan");
            check(std::isfinite(out.right_x)&&std::isfinite(out.right_y)&&std::fabs(out.right_x)<=1&&std::fabs(out.right_y)<=1,"physical output bounds");
            if(p.target_id) {
                check(p.observation_age_ms<40.01f,"source exceeded 40ms");
                check(std::fabs(p.source_aim_px.x-p.aim_px.x)<.001f&&std::fabs(p.source_aim_px.y-p.aim_px.y)<.001f,"manual changed chest reference");
                if(p.observation_gap){check(!p.fire_authority,"gap granted fire");if(p.observation_age_ms>32.001f)check(!p.gap_prediction_valid&&p.aim_authority==0,"prediction exceeded 32ms");}
            }
            if(input.left_trigger==0 && input.right_trigger==0)check(out.right_x==input.right_x&&out.right_y==input.right_y,"release changed raw passthrough");
            if(p.manual_exit_requested)check(comp.manual_passthrough_x&&comp.manual_passthrough_y,"manual exit authority");
            check(!(p.mode==ControlMode::BodyLockFollow&&p.ads_acquisition_active),"ADS/BodyLock ownership");
            const float e=std::hypot(ex*zoom,ey*zoom);
            if(previous_mode==ControlMode::AdsAcquire && p.mode==ControlMode::BodyLockFollow){last_handoff=tick;++handoffs;}
            previous_mode=p.mode;
            if(tick-last_handoff<100 && phase<1500)post_handoff_peak=std::max(post_handoff_peak,e);
            if(tick<1500){stable_run=e<=4?stable_run+1:0;if(stable_run>=50 && first_stable<0)first_stable=tick-49;}
            if(tick<1500){if(first20<0&&e<=20)first20=tick;if(first2<0&&e<=2)first2=tick;}
            if(phase>=100&&phase<1500)error_sum+=e;
            if(phase>=400&&phase<1400){late_sum+=e;late_squared+=e*e;late_peak=std::max(late_peak,e);++late_count;inside+=e<=4;}
            if(std::fabs(ex*zoom)>2 && phase<1500){if(previous_side*ex<0)++crossings;previous_side=ex;}
            ads+=p.mode==ControlMode::AdsAcquire;body+=p.mode==ControlMode::BodyLockFollow;
            max_delta=std::max(max_delta,std::hypot(out.right_x-previous.right_x,out.right_y-previous.right_y));previous=out;
            if(trace.is_open())trace<<tick<<','<<ex*zoom<<','<<ey*zoom<<','<<out.right_x<<','<<out.right_y<<','<<int(p.mode)<<','<<p.aim_authority<<','<<p.observation_age_ms<<','<<p.manual_exit_requested<<','<<p.error_px.x<<','<<p.error_px.y<<','<<p.bodylock_target_motion_px_per_sec.x<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<p.response_scale<<','<<comp.requested_assist_stick.x<<','<<comp.requested_assist_stick.y<<'\n';
            controller.observe_delivered_output(out,true,now);
            motion.push_back(forward_aim_response_curve({out.right_x,out.right_y},cfg.aim_response_curve));
        }
        unsigned tracking=0;for(int t=0;t<duration;++t)tracking+=t%1625>=100&&t%1625<1500;
        csv<<seed<<','<<scenario<<','<<duration<<','<<kind<<','<<nonlinear<<','<<floor<<','<<enemy<<','<<response<<','<<delay<<','<<scope_ms<<','<<scope_gain<<','<<error_sum/tracking<<','<<late_sum/late_count<<','<<double(inside)/late_count<<','<<crossings<<','<<ads<<','<<body<<','<<first20<<','<<first2<<','<<max_delta<<','<<initial_x<<','<<initial_y<<','<<first_stable<<','<<late_peak<<','<<std::sqrt(late_squared/late_count)<<','<<handoffs<<','<<post_handoff_peak<<'\n';
    }
    check(bool(csv),"simulation result write failed");
    std::cout<<"PASS 64 short/long cases, 704000 ticks; seed "<<seed<<'\n';
}

void recorded_closing_rate() {
    std::ifstream file(RECORDED_SETTLING_PATH);check(bool(file),"missing live settling geometry");std::string line;std::getline(file,line);
    TargetCoordinatorConfig cfg;cfg.max_observation_age_ms=40;cfg.missing_observation_grace_ms=32;cfg.settle_radius_px=2;
    TargetCoordinator coordinator(cfg);IntentState intent;intent.activation=AssistActivation::Engaged;
    TargetControlFeedback feedback;feedback.firing_recently=true;feedback.aim_response_px_per_stick_second=1500;
    double first=0,previous_time=0;Vec2f previous{};unsigned count=0;
    while(std::getline(file,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        unsigned source_line,frame;unsigned long long capture;float ex,ey,w,h;row>>source_line>>frame>>capture>>ex>>ey>>w>>h;check(bool(row),"bad settling row");
        if(!count){first=double(capture)*1e-9;coordinator.begin_ads_epoch(1,10);}
        double t=10+double(capture)*1e-9-first;VisionObservationBatch b;b.frame_id=frame;b.source_time_seconds=b.publish_time_seconds=t;b.frame_width_px=640;b.frame_height_px=512;b.capture_fresh=true;b.selector_identity_protocol=true;b.selector_target_generation=7;b.preferred_source_id=1;b.count=1;
        auto& c=b.candidates[0];c.source_id=1;c.aim_px={320+ex,256+ey};c.has_aim_point=true;c.aim_region_px={c.aim_px.x-w*.5f,c.aim_px.y-h*.2f,w,h};c.aim_region_source=AimRegionSource::VisionGeometry;c.has_aim_region=true;c.body_box_px=c.aim_region_px;c.has_body_box=true;c.box_size_px={w,h};c.confidence=.95f;c.reliability=.9f;c.normalized_size=h/512;c.body_cue=true;
        auto p=coordinator.update(b,intent,t,feedback);
        if(count){
            const float dt=float(t-previous_time);
            check(std::fabs(p.error_rate_px_per_sec.x-std::clamp((ex-previous.x)/dt,-4000.f,4000.f))<.5f,"live source closing speed erased by firing policy");
            check(std::fabs(p.error_rate_px_per_sec.y-std::clamp((ey-previous.y)/dt,-4000.f,4000.f))<.5f,"live vertical closing speed erased by firing policy");
        }
        previous_time=t;previous={ex,ey};++count;
    }
    check(count>=80,"too few live source pairs");std::cout<<"PASS recorded closing: "<<count<<" captures\n";
}

int main(int argc,char**argv) {
    int failed=0;
    try{recorded_closing_rate();}catch(const std::exception&e){std::cout<<"FAIL closing: "<<e.what()<<'\n';++failed;}
    try{recorded_regression();}catch(const std::exception&e){std::cout<<"FAIL recorded: "<<e.what()<<'\n';++failed;}
    if(argc==3)try{simulations(std::stoul(argv[1]),argv[2]);}catch(const std::exception&e){std::cout<<"FAIL simulation: "<<e.what()<<'\n';++failed;}
    return failed?1:0;
}
