
#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_response_curve_plugin.h"
#include "runtime_app/vision_controller_adapter.h"
#include "vision_native/target_selector.h"
#include <random>
#include <deque>
#include <array>
#include <iostream>
#include <fstream>
using namespace controller_native;
using namespace pipeline_contract;
namespace f=controller_native::incident_fixture;
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
GamepadRuntimeConfig config(bool nonlinear,bool fire){
 auto c=f::base_config(40,190);c.ai_aim.normalized_assist_parameters=true;
 c.ai_aim.minimum_position_stick=.5f;c.ai_aim.arrival_radius_px=2;
 c.ai_aim.ads_response_time_ms=60;c.ai_aim.bodylock_response_time_x_ms=c.ai_aim.bodylock_response_time_y_ms=10;
 c.ai_aim.ads_free_initial_scale=1400;c.ai_aim.ads_slow_initial_scale=900;
 c.ai_aim.body_free_initial_scale=2000;c.ai_aim.body_slow_initial_scale=1500;
 c.ai_aim.ads_output_limit_x=c.ai_aim.ads_output_limit_y=c.ai_aim.bodylock_output_limit_x=c.ai_aim.bodylock_output_limit_y=1;
 c.aim_response_curve.algorithm=nonlinear?AimResponseCurveAlgorithm::CodDynamicLegacyLut:AimResponseCurveAlgorithm::Linear;
 c.recoil.enabled=fire;c.recoil.output_amount=.2f;return c;
}
struct World {
 double now=10;NativeGamepadController controller;
 vision_native::VisionTargetSelector selector{640,512,190,true,.2f,.65f};
 float x=0,y=0;GamepadOutputState previous{};
 std::deque<Vec2f> motion;
 std::deque<std::pair<int,ControllerVisionSnapshot>> pending;
 unsigned frames=0,cues=0,accepted=0,lost=0,ads=0,body=0,exits=0,handoffs=0;
 ControlMode last_mode=ControlMode::Manual;
 double error=0,late=0;unsigned measured=0;float peak_delta=0;
 World(const GamepadRuntimeConfig& c,int delay):controller(c,&now),motion(delay){}
};
int main(int argc,char**argv){try{
 require(argc==3||argc==4,"seed output-directory [noise-fraction]");unsigned seed=std::stoul(argv[1]);std::filesystem::create_directories(argv[2]);
 std::ofstream csv(std::string(argv[2])+"/cases.csv");
 csv<<"seed,case,duration,variant,mean_error,late_mean,max_delta,cues,accepted_cues,zero_authority,ads,body,handoffs,manual_exits,pair_max_output_difference\n";
 std::ofstream trace(std::string(argv[2])+"/transitions.csv");trace<<"case,duration,tick,variant,phase,source,manual_x,manual_y,output_x,output_y,authority,mode,cue,gap,source_age,target,exit,error_x,error_y\n";
 for(int duration:{2000,20000})for(int sc=0;sc<32;++sc){
  std::mt19937 rng(seed^(sc*104729u+duration));const bool fire=sc%4==3;auto cfg=config(sc%2,fire);cfg.ai_aim.ai_input_deadzone=argc==4?std::stof(argv[3]):.03f;
  const int delay=6+rng()%25;const float response=900+rng()%1601;
  World clean(cfg,delay),clutter(cfg,delay);std::array<World*,2> worlds{&clean,&clutter};
  unsigned next_capture=0;float pair_max=0;
  for(int tick=0;tick<duration;++tick){
   const int phase=tick%1625;bool capture=tick>=int(next_capture);int latency=2+rng()%9;
   if(capture)next_capture=tick+5+rng()%8;
   const float noise_x=(int(rng()%1001)-500)*.0005f,noise_y=(int(rng()%1001)-500)*.0005f;
   const bool cue=phase>=400&&phase<490 || phase>=900&&phase<1020;
   const bool gap=(phase>=650&&phase<675)||(phase>=1150&&phase<1210)||phase>=1575;
   const bool weak=phase>=250&&phase<350 || phase>=1020&&phase<1100;
   const bool firing=fire&&phase>=350&&phase<1400;
   auto input=f::ads_input();input.left_trigger=phase<1500?1:0;
   // Fixed 30% input through strong/weak/cue/gap transitions. Other cases
   // cover neutral, diagonal, onset/reversal/release and explicit exit.
   if(sc%8==0)input.right_x=.30f;
   else if(sc%8==1)input.right_x=-.30f;
   else if(sc%8==2){input.right_x=.30f/std::sqrt(2.f);input.right_y=.30f/std::sqrt(2.f);}
   else if(sc%8==3){input.right_x=phase>200&&phase<800?.30f:phase<1300?-.30f:0;}
   else if(sc%8==4)input.right_x=phase>700&&phase<1000?.90f:0;
   else if(sc%8==5){input.right_x=.30f;input.right_y=-.20f;}
   input.right_trigger=firing?1:0;
   for(int variant=0;variant<2;++variant){auto&w=*worlds[variant];w.now=10+tick*.001;
    if(phase==0){w.x=sc<8?0:25+float(sc%4)*10;w.y=sc<8?0:12;w.selector.reset();}
    if(capture){
     vision_native::DetectionBatch b;b.frame_width=640;b.frame_height=512;b.frame_id=++w.frames;b.captured_at_ns=uint64_t(w.now*1e9);
     const float tx=320+w.x+noise_x,ty=256+w.y+noise_y;
     if(!gap&&!cue){vision_native::Detection d;d.x1=tx-45;d.x2=tx+45;d.y1=ty-28;d.y2=d.y1+140;d.conf=weak?.30f:.92f;d.color_classified=true;d.has_cue_point=true;d.cue_x=tx;d.cue_y=ty-40;d.cue_score=.95f;d.color_bonus=.3f;b.detections.push_back(d);}
     if(cue&&!gap){b.has_external_cue=true;b.external_cue_x=tx;b.external_cue_y=ty-40;b.external_cue_score=.95f;}
     // Raw diagnostic clutter survives the production adapter, while Vision
     // explicitly selects cue. It must not decide control ownership.
     auto v=w.selector.select(b);
     if(variant&&v.has_target&&std::string(v.target_source)=="cue_hold"){
      for(unsigned n=0;n<1+unsigned(sc%4);++n){vision_native::Detection d;d.x1=500;d.x2=610;d.y1=10;d.y2=60;d.conf=.34f;d.color_classified=true;v.detections.push_back(d);}
     }
     v.frame_updated=true;v.frame_id=b.frame_id;v.captured_at_ns=b.captured_at_ns;v.result_at_ns=b.captured_at_ns+uint64_t(latency)*1000000;
     w.pending.emplace_back(tick+latency,runtime_app::adapt_vision_result(v));
    }
    bool delivered_cue=false;
    while(!w.pending.empty()&&w.pending.front().first<=tick){delivered_cue=w.pending.front().second.state.target_tier=="cue_hold";w.controller.submit_vision_snapshot(w.pending.front().second);w.pending.pop_front();}
    auto out=w.controller.build_output(input);const auto&p=w.controller.last_target_plan();const auto&comp=w.controller.last_output_components();
    require(std::isfinite(out.right_x)&&std::isfinite(out.right_y)&&std::hypot(out.right_x,out.right_y)<=1.415f,"nonfinite/unbounded output");
    if(p.target_id){require(p.source_capture_age_ms<40.01f,"expired identity");require(std::hypot(p.aim_px.x-p.source_aim_px.x,p.aim_px.y-p.source_aim_px.y)<.001f,"manual moved chest");}
    if(p.observation_gap&&p.source_capture_age_ms>32.001f)require(p.aim_authority==0&&!p.fire_authority,"gap outlived 32ms");
    if(p.cue_continuation)require(!p.fire_authority&&!p.fire_requested,"cue gained fire");
    if(p.manual_exit_requested){require(comp.manual_passthrough_x&&comp.manual_passthrough_y,"manual exit suppressed");++w.exits;}
    if(!input.left_trigger&&!input.right_trigger)require(out.right_x==input.right_x&&out.right_y==input.right_y,"raw release changed");
    require(!(p.ads_acquisition_active&&p.mode==ControlMode::BodyLockFollow),"dual phase ownership");
    if(delivered_cue){++w.cues;w.accepted+=p.cue_continuation;}
    w.lost+=p.aim_authority==0&&phase<1500;w.ads+=p.mode==ControlMode::AdsAcquire;w.body+=p.mode==ControlMode::BodyLockFollow;
    w.handoffs+=w.last_mode==ControlMode::AdsAcquire&&p.mode==ControlMode::BodyLockFollow;w.last_mode=p.mode;
    w.peak_delta=std::max(w.peak_delta,std::hypot(out.right_x-w.previous.right_x,out.right_y-w.previous.right_y));
    if(phase>=100&&phase<1500){w.error+=std::hypot(w.x,w.y);++w.measured;}if(phase>=400&&phase<1400)w.late+=std::hypot(w.x,w.y);
    if(((sc<4&&duration==2000)||(sc==1&&duration==20000))&&tick%5==0)trace<<sc<<','<<duration<<','<<tick<<','<<variant<<','<<phase<<','<<(gap?"gap":cue?"cue":weak?"weak":"strong")<<','<<input.right_x<<','<<input.right_y<<','<<out.right_x<<','<<out.right_y<<','<<p.aim_authority<<','<<int(p.mode)<<','<<p.cue_continuation<<','<<p.observation_gap<<','<<p.source_capture_age_ms<<','<<p.target_id<<','<<p.manual_exit_requested<<','<<w.x<<','<<w.y<<'\n';
    w.controller.observe_delivered_output(out,true,w.now);w.previous=out;
    const auto movement=w.motion.front();w.motion.pop_front();w.motion.push_back(forward_aim_response_curve({out.right_x,out.right_y},cfg.aim_response_curve));
    const float vx=sc<8?0:float(30+sc%8*12)*std::sin(tick*.003+sc),vy=sc<16?0:float(15+sc%4*8)*std::cos(tick*.002+sc);
    w.x+=(vx-response*movement.x)*.001f;w.y+=(vy+response*movement.y+(firing?.2f*response:0))*.001f;
   }
   pair_max=std::max(pair_max,std::hypot(clean.previous.right_x-clutter.previous.right_x,clean.previous.right_y-clutter.previous.right_y));
  }
  for(int variant=0;variant<2;++variant){auto&w=*worlds[variant];unsigned late_count=0;for(int t=0;t<duration;++t)late_count+=t%1625>=400&&t%1625<1400;
   csv<<seed<<','<<sc<<','<<duration<<','<<variant<<','<<w.error/w.measured<<','<<w.late/late_count<<','<<w.peak_delta<<','<<w.cues<<','<<w.accepted<<','<<w.lost<<','<<w.ads<<','<<w.body<<','<<w.handoffs<<','<<w.exits<<','<<pair_max<<'\n';}
 }
 std::cout<<"DONE seed="<<seed<<" 64 paired short/long cases; 1408000 controller ticks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
