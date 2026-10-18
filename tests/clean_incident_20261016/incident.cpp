#include "controller_native/target_coordinator.h"
#include "fixture.h"
#include "pipeline_contract/target_acquisition.h"
#include "runtime_app/vision_controller_adapter.h"
#include "vision_native/target_selector.h"
#include <array>
#include <functional>
#include <iostream>
#include <random>
#include <deque>
#include <fstream>

using namespace pipeline_contract;
using namespace controller_native;
namespace fixture=controller_native::incident_fixture;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
VisionObservationBatch observation(unsigned frame,double capture,bool present=true,unsigned generation=90) {
    VisionObservationBatch b; b.frame_id=frame;b.capture_fresh=true;b.source_time_seconds=capture;
    b.frame_width_px=640;b.frame_height_px=512;b.selector_identity_protocol=true;b.selector_target_generation=generation;
    if(present) {b.count=1;b.preferred_source_id=frame;auto& p=b.candidates[0];p.source_id=frame;
      p.aim_px={244.25f,210.8667f};p.has_aim_point=true;p.reliability=1;p.confidence=.58f;
      p.box_size_px={179.5f,205.167f};p.normalized_size=.4f;p.has_aim_region=true;p.aim_region_px={205,180,80,65};p.aim_region_source=AimRegionSource::VisionGeometry;}
    return b;
}
IntentState intent() {IntentState i;i.activation=AssistActivation::Engaged;i.independent_right_axes=true;return i;}
TargetCoordinatorConfig config() {TargetCoordinatorConfig c;c.max_observation_age_ms=40;c.missing_observation_grace_ms=32;return c;}
void geometry() {
    // Real boxes at JSONL lines 117283,117347,117382,117494,117529.
    for(auto wh:std::array<Vec2f,5>{{{258.5f,183},{235.1667f,198.25f},{264.3337f,200.833f},{168.666f,141.334f},{194,155.833f}}}) {
        check(body_geometry_admitted(wh.x,wh.y,640,512,true),"recorded upper-body box rejected by aspect discontinuity");
    }
    for(int h=60;h<=180;++h) check(body_geometry_admitted(200,float(h),640,512,true),"continuous body aspect sweep rejected");
    check(!body_geometry_admitted(200,59,640,512,true),"implausibly flat box admitted");
    check(!body_geometry_admitted(20,20,640,512,true),"small target geometry guard removed");
    check(!body_geometry_admitted(20,100,640,512,true),"implausibly narrow box admitted");
    vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);
    vision_native::Detection d;d.x1=133.833f;d.y1=169;d.x2=392.333f;d.y2=352;d.conf=.92f;d.color_classified=true;
    vision_native::DetectionBatch b;b.frame_width=640;b.frame_height=512;b.detections={d};
    bool found=false;
    for(int n=1;n<=4;++n){b.frame_id=n;b.captured_at_ns=1000000000ull+n*6000000ull;found=selector.select(b).has_target||found;}
    check(found,"upper-body geometry never reaches selector selection");
    selector.reset();b.detections[0].is_friendly=true;
    for(int n=5;n<=8;++n){b.frame_id=n;b.captured_at_ns+=6000000;check(!selector.select(b).has_target,"friendly guard removed");}
    selector.reset();b.detections[0].is_friendly=false;b.detections[0].class_id=9;
    check(!selector.select(b).has_target,"class guard removed");
}
void short_gap() {
    TargetCoordinator c(config());auto i=intent();c.begin_ads_epoch(68,1.48);
    auto first=c.update(observation(1,1.48),i,1.485);
    check(first.ads_acquisition_active,"fixture did not admit ADS");
    c.update(observation(2,1.6965),i,1.710);
    TargetControlFeedback f;f.continuation_rejection="manual_prediction_override";
    c.update(observation(3,1.714,false),i,1.724,f);
    auto held=c.update({},i,1.729,f);
    check(held.target_id==first.target_id && held.ads_acquisition_active,"32ms prediction expiry destroyed same-target ADS");
    check(held.aim_authority==0 && !held.fire_authority && !held.gap_prediction_valid,"prediction survived 32ms");
    auto recovered=c.update(observation(4,1.7221),i,1.735,f);
    check(recovered.target_id==first.target_id && recovered.target_acquisition_id==first.target_acquisition_id &&
        recovered.ads_acquisition_active && recovered.mode==ControlMode::AdsAcquire,"fresh same-generation frame did not resume original ADS");
    check(recovered.source_aim_px.x==recovered.aim_px.x && recovered.source_aim_px.y==recovered.aim_px.y,"manual rewrote anatomical point");
}
void boundaries() {
    for(int variant=0;variant<7;++variant) {
        TargetCoordinator c(config());auto i=intent();c.begin_ads_epoch(1,2);
        const auto first=c.update(observation(1,2),i,2);
        TargetControlFeedback f;f.continuation_valid=true;
        c.update(observation(2,2.01,false),i,2.01,f);
        auto b=observation(3,2.033,false);
        if(variant==1)b.rejected_friendly_count=1;
        if(variant==2)b.rejected_low_reliability_count=1;
        if(variant==3)b.selector_target_generation=91;
        if(variant==4)i.activation=AssistActivation::Off;
        if(variant==5)i.right_purpose=UserAimIntentPurpose::HandoverTarget;
        if(variant==6){b.capture_fresh=false;}
        auto p=c.update(b,i,2.033,f);
        check(p.aim_authority==0 && !p.fire_authority,"33ms gap authorized output");
        if(variant==0 || variant==6) check(p.target_id==first.target_id && p.ads_acquisition_active,"same generation identity lost");
        else check(p.target_id==0,"explicit invalidation retained target");
        p=c.update({},i,2.0401,f);
        check(p.target_id==0 && p.aim_authority==0 && !p.fire_authority,"40ms source deadline extended");
    }
}
void continuous_geometry() {
    const auto before=body_aim_geometry(270,220,370,284.8f,.2f,.65f);
    const auto after=body_aim_geometry(270,220,370,285.2f,.2f,.65f);
    check(std::abs(after.aim_y-before.aim_y)<.5f,"0.4px box jitter jumps source chest reference");
    check(std::abs(after.top-before.top)<.5f && std::abs(after.bottom-before.bottom)<.5f,
        "0.65 aspect boundary jumps source aim region");
    for(auto ratios:std::array<Vec2f,4>{{{.2f,.65f},{.4f,.65f},{.8f,.25f},{.4f,.4f}}}) {
        const auto flat=body_aim_geometry(0,0,100,30,ratios.x,ratios.y);
        const auto upright=body_aim_geometry(0,0,100,100,ratios.x,ratios.y);
        check(std::abs(flat.aim_y-30*ratios.y)<.001f && std::abs(upright.aim_y-100*ratios.x)<.001f,
            "configured chest reference endpoints changed");
        auto previous=body_aim_geometry(0,0,100,29.9f,ratios.x,ratios.y);
        for(int n=300;n<=1001;++n) {
            const float height=n*.1f;
            const auto current=body_aim_geometry(0,0,100,height,ratios.x,ratios.y);
            const float ratio=current.aim_y/height;
            check(ratio>=std::min(ratios.x,ratios.y)-1e-6f && ratio<=std::max(ratios.x,ratios.y)+1e-6f,
                "geometry introduced an anatomical reference outside configured endpoints");
            check(current.top<=current.aim_y && current.aim_y<=current.bottom && current.top>=0 && current.bottom<=height,
                "source chest reference outside body/aim region");
            check(std::abs(current.aim_y-previous.aim_y)<.5f && std::abs(current.top-previous.top)<.5f &&
                std::abs(current.bottom-previous.bottom)<.5f,"geometry sweep found a relocated discontinuity");
            const auto moved=body_aim_geometry(17,29,217,29+height*2,ratios.x,ratios.y);
            check(std::abs(moved.aim_y-(29+current.aim_y*2))<.002f,"geometry depends on box scale/translation");
            previous=current;
        }
    }
}
void simulations(unsigned seed,const std::string& directory,int extra_delay_ms=0,bool boundary_scene=false,bool exercise_manual=true,bool trace_enabled=false,bool confirmed_enemy=false) {
    check(extra_delay_ms>=0 && extra_delay_ms<=20,"unsupported synthetic latency offset");
    std::filesystem::create_directories(directory);
    std::ofstream report(directory+"/seed-"+std::to_string(seed)+".csv");
    report<<"seed,scenario,duration_ms,ticks,selected_frames,assist_ticks,prediction_ticks,identity_only_ticks,expired_ticks,mean_error_px,max_error_px,center_crossings,max_output_delta,extra_delay_ms,tracking_ticks,tracking_mean_error_px,boundary_scene,exercise_manual,pre_manual_error_px,manual_error_px,release_error_px,confirmed_enemy,ads_ticks,bodylock_ticks\n";
    for(int duration:{2000,20000}) for(int scenario=0;scenario<16;++scenario) {
        std::mt19937 rng(seed ^ (scenario*7919u+duration));
        auto cfg=fixture::base_config(40,190);cfg.ai_aim.normalized_assist_parameters=true;
        cfg.ai_aim.ads_response_time_ms=60;cfg.ai_aim.bodylock_response_time_x_ms=cfg.ai_aim.bodylock_response_time_y_ms=30;
        cfg.ai_aim.ads_output_limit_x=cfg.ai_aim.ads_output_limit_y=1;
        cfg.ai_aim.bodylock_output_limit_x=cfg.ai_aim.bodylock_output_limit_y=1;
        cfg.ai_aim.body_free_initial_scale=2000;cfg.ai_aim.body_slow_initial_scale=1500;
        cfg.ai_aim.ads_free_initial_scale=1400;cfg.ai_aim.ads_slow_initial_scale=900;
        cfg.recoil.enabled=exercise_manual && scenario%4==3;cfg.recoil.output_amount=.2f;
        double now=10;NativeGamepadController controller(cfg,&now);
        vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);
        std::deque<std::pair<int,ControllerVisionSnapshot>> pending;
        std::deque<GamepadOutputState> motion(9);
        float ex=80,ey=-35,previous_x=0,previous_error=80,maximum_delta=0,maximum_error=0;
        double error_sum=0,tracking_error=0;unsigned selected=0,assist=0,predicted=0,identity_only=0,expired=0,crossings=0,tracking_ticks=0;
        unsigned frame=0,ads_ticks=0,bodylock_ticks=0;int next_capture=0;
        std::array<double,3> stage_error{};std::array<unsigned,3> stage_ticks{};
        std::ofstream trace;
        if(trace_enabled && duration==2000 && scenario<3) {
            std::filesystem::create_directories(directory+"/traces");
            trace.open(directory+"/traces/case-"+std::to_string(scenario)+".csv");
            trace<<"tick,ex,ey,manual_x,manual_y,output_x,output_y,target_id,authority,source_y,manual_exit,pass_x,pass_y,mode,ads_active,ads_reason\n";
        }
        for(int tick=0;tick<duration;++tick) {
            now=10+tick*.001;const int phase=tick%1625;
            if(phase==0){ex=float(int(rng()%181)-90);ey=float(int(rng()%61)-30);}
            const auto applied=motion.front();motion.pop_front();
            const float vx=120*std::sin(tick*.004f+scenario),vy=45*std::cos(tick*.003f+scenario);
            // Explicit synthetic linear camera: 1400 px/(stick*s), 9 ms delay.
            ex=std::clamp(ex+(vx-applied.right_x*1400)*.001f,-280.f,280.f);
            ey=std::clamp(ey+(vy+applied.right_y*1400)*.001f,-210.f,210.f);
            PhysicalGamepadState input=fixture::ads_input();input.left_trigger=phase<1500?1.f:0.f;
            input.right_x=(phase>350 && phase<1000)?(scenario%4==0?0:scenario%4==1?.2f:scenario%4==2?-.48f:.78f):.012f;
            input.right_y=(phase>650 && phase<1200)?(scenario%3==0?-.4f:.21f):-.012f;
            if(!exercise_manual)input.right_x=input.right_y=0;
            input.right_trigger=cfg.recoil.enabled && phase>250 && phase<1100?1.f:0.f;
            if(tick>=next_capture) {
                next_capture=tick+5+int(rng()%8);++frame;
                vision_native::DetectionBatch b;b.frame_width=640;b.frame_height=512;b.frame_id=frame;b.captured_at_ns=static_cast<std::uint64_t>(now*1e9);
                const int drop_duration=std::array<int,4>{{0,22,35,75}}[scenario/4];
                const bool gap=(phase>=1575)||(phase>=250 && phase<250+drop_duration)||(scenario==15 && phase>=500 && phase<700);
                if(!gap) {
                    const float width=100, height=boundary_scene ? 64.6f+float(rng()%81)*.01f : width*(.6f+float(rng()%81)*.01f);
                    const auto ratio=wide_low_body_shape(width,height)?.65f:.2f;
                    vision_native::Detection d;d.x1=320+ex-width*.5f;d.x2=d.x1+width;
                    // Boundary scene has a fixed physical chest-to-top offset.
                    // Detector bottom jitter cannot move ground truth or copy
                    // the production interpolation back into the synthetic plant.
                    d.y1=256+ey-(boundary_scene ? 27.625f : height*ratio);d.y2=d.y1+height;
                    d.conf=.92f;d.color_classified=true;d.is_friendly=scenario==14 && phase>=900 && phase<1000;
                    // Color evidence affects confidence/authority, never the
                    // physical chest location or the detector-owned aim point.
                    if(confirmed_enemy)d.color_bonus=.3f;
                    b.detections={d};
                }
                auto v=selector.select(b);selected+=v.has_target;
                const int result_delay_ms=2+int(rng()%11)+extra_delay_ms;
                v.frame_id=frame;v.captured_at_ns=b.captured_at_ns;
                v.result_at_ns=b.captured_at_ns+static_cast<std::uint64_t>(result_delay_ms)*1000000;v.frame_updated=true;
                pending.emplace_back(tick+result_delay_ms,runtime_app::adapt_vision_result(v));
            }
            while(!pending.empty() && pending.front().first<=tick) {controller.submit_vision_snapshot(pending.front().second);pending.pop_front();}
            auto out=controller.build_output(input);
            const auto& p=controller.last_target_plan();const auto& components=controller.last_output_components();
            check(valid(p),"random replay invalid target plan");
            check(std::isfinite(out.right_x)&&std::isfinite(out.right_y)&&std::abs(out.right_x)<=1&&std::abs(out.right_y)<=1,"random replay output outside physical envelope");
            if(p.target_id) {
                check(p.observation_age_ms<40.01f,"random replay retained expired source");
                check(std::abs(p.source_aim_px.x-p.aim_px.x)<.001f && std::abs(p.source_aim_px.y-p.aim_px.y)<.001f,"random replay manual moved source aim");
                if(p.observation_gap) {
                    check(!p.fire_authority,"gap acquired fire authority");
                    if(p.observation_age_ms>32.001f)check(p.aim_authority==0&&!p.gap_prediction_valid,"prediction outlived 32ms");
                }
            } else ++expired;
            if(input.left_trigger==0 && input.right_trigger==0)check(out.right_x==input.right_x && out.right_y==input.right_y,"raw release passthrough changed");
            check(!(p.mode==ControlMode::BodyLockFollow && p.ads_acquisition_active),"active ADS mixed with BodyLock");
            if(p.manual_exit_requested)check(components.manual_passthrough_x && components.manual_passthrough_y,"explicit manual exit lost authority");
            assist+=p.aim_authority>0;predicted+=p.gap_prediction_valid;identity_only+=p.observation_gap && p.aim_authority==0;
            const float error=std::hypot(ex,ey);error_sum+=error;maximum_error=std::max(maximum_error,error);
            // Compare the same commanded tracking windows, including lost targets;
            // conditioning on aim_authority would hide the cost of failed selection.
            if(phase>=100 && phase<1500){++tracking_ticks;tracking_error+=error;}
            if(phase>=100 && phase<1500) {
                const unsigned stage=phase<=350?0:phase<1200?1:2;
                stage_error[stage]+=error;++stage_ticks[stage];
            }
            ads_ticks+=p.mode==ControlMode::AdsAcquire;bodylock_ticks+=p.mode==ControlMode::BodyLockFollow;
            if(trace.is_open())trace<<tick<<','<<ex<<','<<ey<<','<<input.right_x<<','<<input.right_y<<','<<out.right_x<<','<<out.right_y<<','<<p.target_id<<','<<p.aim_authority<<','<<p.source_aim_px.y<<','<<p.manual_exit_requested<<','<<components.manual_passthrough_x<<','<<components.manual_passthrough_y<<','<<int(p.mode)<<','<<p.ads_acquisition_active<<','<<int(p.ads_decision_reason)<<'\n';
            crossings+=ex*previous_error<0 && phase!=0;previous_error=ex;
            maximum_delta=std::max(maximum_delta,std::abs(out.right_x-previous_x));previous_x=out.right_x;
            controller.observe_delivered_output(out,true,now);motion.push_back(out);
        }
        report<<seed<<','<<scenario<<','<<duration<<','<<duration<<','<<selected<<','<<assist<<','<<predicted<<','<<identity_only<<','<<expired<<','<<error_sum/duration<<','<<maximum_error<<','<<crossings<<','<<maximum_delta<<','<<extra_delay_ms<<','<<tracking_ticks<<','<<tracking_error/tracking_ticks<<','<<boundary_scene<<','<<exercise_manual;
        for(unsigned stage=0;stage<3;++stage)report<<','<<stage_error[stage]/stage_ticks[stage];
        report<<','<<confirmed_enemy<<','<<ads_ticks<<','<<bodylock_ticks<<'\n';
    }
    check(bool(report),"simulation report write failed");
}
void geometry_probe(const std::string& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory+"/geometry-sweep.csv");
    csv<<"tick,height,raw_aim_y,source_aim_y,output_y,target_id\n";
    auto cfg=fixture::base_config(40,190);cfg.ai_aim.normalized_assist_parameters=true;
    double now=10;NativeGamepadController controller(cfg,&now);
    vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);
    for(int tick=0;tick<1000;++tick) {
        now=10+tick*.001;
        const float height=(tick/100)%2 ? 65.2f:64.8f;
        const auto geometry=body_aim_geometry(270,220,370,220+height,.2f,.65f);
        if(tick%10==0) {
            vision_native::DetectionBatch b;b.frame_width=640;b.frame_height=512;
            b.frame_id=tick/10+1;b.captured_at_ns=static_cast<std::uint64_t>(now*1e9);
            vision_native::Detection d;d.x1=270;d.x2=370;d.y1=220;d.y2=220+height;
            d.conf=.92f;d.color_classified=true;b.detections={d};
            auto v=selector.select(b);v.frame_id=b.frame_id;v.captured_at_ns=b.captured_at_ns;
            v.result_at_ns=b.captured_at_ns;v.frame_updated=true;
            controller.submit_vision_snapshot(runtime_app::adapt_vision_result(v));
        }
        auto out=controller.build_output(fixture::ads_input());
        const auto& plan=controller.last_target_plan();
        csv<<tick<<','<<height<<','<<geometry.aim_y<<','<<plan.source_aim_px.y<<','<<out.right_y<<','<<plan.target_id<<'\n';
        controller.observe_delivered_output(out,true,now);
    }
    check(bool(csv),"geometry probe report write failed");
}
int main(int argc,char**argv) {
    if(argc==3 && std::string(argv[1])=="--geometry-probe") {
        try {geometry_probe(argv[2]);return 0;}
        catch(const std::exception&e){std::cout<<e.what()<<'\n';return 1;}
    }
    int failures=0;
    for(auto test:std::array<std::pair<const char*,std::function<void()>>,4>{{{"recorded_geometry",geometry},{"recorded_short_gap",short_gap},{"adjacent_boundaries",boundaries},{"continuous_geometry",continuous_geometry}}}) {
      try {test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception&e){++failures;std::cout<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    if(argc>=3) {
      try {const std::string scene=argc>=5?argv[4]:"mixed";
           simulations(static_cast<unsigned>(std::stoul(argv[1])),argv[2],argc>=4?std::stoi(argv[3]):0,
               scene.rfind("boundary",0)==0,scene.find("neutral")==std::string::npos,argc>=6&&std::string(argv[5])=="trace",scene.find("confirmed")!=std::string::npos);
           std::cout<<"PASS randomized seed "<<argv[1]<<" (32 short/long cases, 352000 ticks)\n";}
      catch(const std::exception&e){++failures;std::cout<<"FAIL randomized: "<<e.what()<<'\n';}
    }
    return failures?1:0;
}
