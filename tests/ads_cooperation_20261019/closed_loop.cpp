#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_response_curve_plugin.h"
#include <deque>
#include <random>
#include <iostream>
#include <array>
using namespace controller_native;
using namespace pipeline_contract;
namespace f=controller_native::incident_fixture;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 require(argc==4,"profile output-dir seed");
 auto base=load_runtime_config(argv[1]).gamepad;
 const unsigned seed=std::stoul(argv[3]);std::filesystem::create_directories(argv[2]);
 std::ofstream csv(std::string(argv[2])+"/cases.csv"),trace(std::string(argv[2])+"/trace.csv");
 csv<<"seed,case,duration,motion,manual,mean_error,late_error,p95_error,max_error,ads,body,handoffs,exits,stale_ticks,mean_delta,peak_delta\n";
 trace<<"case,tick,error_x,error_y,manual_x,manual_y,out_x,out_y,request_x,request_y,mode,authority,source_age,motion_valid,motion_x,motion_y,correction_x,exit,duration,response,plant_response,plant_delay\n";
 for(int duration:{2000,20000})for(int sc=0;sc<48;++sc){
  std::mt19937 rng(seed^(unsigned(sc)*104729u+unsigned(duration)));
  const int motion=sc/8,manual=sc%8,delay=6+rng()%25;
  const float response=900+float(rng()%1601);
  auto cfg=base;cfg.auto_fire.enabled=false;cfg.recoil.enabled=manual==5;
  // Explicit user-required evidence boundary, independent of the profile
  // loader's legacy 50 ms fallback when no tracker section is present.
  cfg.tracker.max_observation_age_ms=40;
  double now=10;NativeGamepadController c(cfg,&now);
  std::deque<Vec2f> plant(delay);std::deque<std::pair<int,ControllerVisionSnapshot>> pending;
  float x=0,y=0;unsigned next_capture=0,frame=0;
  double sum=0,late=0,delta_sum=0;unsigned n=0,ln=0,ads=0,body=0,handoffs=0,exits=0,stale=0;
  float peak=0;Vec2f previous{};ControlMode previous_mode=ControlMode::Manual;std::vector<float> errors;
  for(int tick=0;tick<duration;++tick){
   const int phase=tick%1625;now=10+tick*.001;
   if(phase==0){x=20;y=sc%2?-18.f:0.f;}
   // A fixed world target with lateral player translation is represented by
   // screen drift independently of right-stick camera rotation. Paired motion
   // classes 1/4 cover constant translation and direction-changing strafe.
   const float ramp=std::min(1.f,phase/80.f);
   float vx=motion==0?0:motion==1?60:motion==2?360:motion==3?600:
      motion==4?180*std::sin(phase*.008f):360*std::cos(phase*.004f);
   float vy=motion==5?240*std::sin(phase*.004f):0;vx*=ramp;vy*=ramp;
   auto input=f::ads_input();input.left_trigger=phase>=25&&phase<1500?1:0;
   if(motion==4)input.left_x=std::sin(phase*.008f)*.8f;
   auto sustain=inverse_aim_response_curve({vx/response,-vy/response},cfg.aim_response_curve);
   if(manual==1){input.right_x=.9f*sustain.x;input.right_y=.9f*sustain.y;}
   if(manual==2){input.right_x=phase<1200?.30f:0;} // carried bias stress, not ideal tracking
   if(manual==3&&phase>=150){input.right_x=.8f*sustain.x;input.right_y=.8f*sustain.y;}
   if(manual==4){input.right_x=phase>=250&&phase<750?.3f:phase>=750&&phase<1200?-.3f:0;input.right_y=phase>=250&&phase<1200?.21f:0;}
   input.rb=manual==5&&phase>=300&&phase<1400;
   if(manual==6&&phase>=700&&phase<1000)input.right_x=-.95f;
   if(manual==7){input.right_x=.3f;input.right_y=-.18f;}
   const bool gap=manual==7&&((phase>=650&&phase<675)||(phase>=1120&&phase<1190));
   const bool cue=manual==7&&phase>=850&&phase<920;
   if(tick>=int(next_capture)){
    next_capture=tick+5+rng()%8;const int latency=2+rng()%9;
    const float nx=(int(rng()%1001)-500)*.0005f,ny=(int(rng()%1001)-500)*.0005f;
    f::TargetSpec s;s.observation_id=++frame;s.selector_generation=tick/1625+1;
    s.aim_height_ratio=.2f;s.has_enemy_cue=s.enemy_identity_confirmed=s.color_classified=true;s.confidence=.92f;
    ControllerVisionSnapshot snap;
    if(phase<1575&&!gap)snap=cue?f::cue_snapshot(s,frame,now,x+nx,y+ny):f::observed_snapshot(s,frame,now,x+nx,y+ny);
    else {snap.frame_updated=true;snap.frame_id=frame;snap.capture_time_seconds=now;snap.selector_identity_protocol=true;snap.selector_target_generation=s.selector_generation;}
    snap.ready_time_seconds=now+latency*.001;
    pending.emplace_back(tick+latency,std::move(snap));
   }
   while(!pending.empty()&&pending.front().first<=tick){c.submit_vision_snapshot(pending.front().second);pending.pop_front();}
   auto out=c.build_output(input);auto&p=c.last_target_plan();auto&d=c.last_output_components();
   require(std::isfinite(out.right_x)&&std::isfinite(out.right_y)&&std::fabs(out.right_x)<=1&&std::fabs(out.right_y)<=1,"invalid physical output");
   if(p.target_id){if(p.source_capture_age_ms>40.01f)std::cerr<<"stale case="<<sc<<" tick="<<tick<<" age="<<p.source_capture_age_ms<<" gap="<<p.observation_gap<<" lifecycle="<<int(p.lifecycle)<<" authority="<<p.aim_authority<<'\n';require(p.source_capture_age_ms<=40.01f,"identity exceeded 40ms");require(std::hypot(p.aim_px.x-p.source_aim_px.x,p.aim_px.y-p.source_aim_px.y)<.001f,"manual altered chest");}
   if(p.observation_gap&&p.source_capture_age_ms>32.001f){require(p.aim_authority==0&&!p.fire_authority,"stale authority exceeded 32ms");++stale;}
   if(p.manual_exit_requested){require(d.manual_passthrough_x&&d.manual_passthrough_y,"exit lost manual authority");++exits;}
   if(!input.left_trigger&&!input.rb)require(out.right_x==input.right_x&&out.right_y==input.right_y,"raw passthrough changed");
   if(p.cue_continuation)require(!p.fire_authority,"cue gained fire authority");
   require(!(p.ads_acquisition_active&&p.mode==ControlMode::BodyLockFollow),"dual positioning owners");
   ads+=p.mode==ControlMode::AdsAcquire;body+=p.mode==ControlMode::BodyLockFollow;
   handoffs+=previous_mode==ControlMode::AdsAcquire&&p.mode==ControlMode::BodyLockFollow;previous_mode=p.mode;
   const float err=std::hypot(x,y),delta=std::hypot(out.right_x-previous.x,out.right_y-previous.y);
   if(phase>=100&&phase<1400){sum+=err;++n;errors.push_back(err);delta_sum+=delta;peak=std::max(peak,delta);}
   if(phase>=700&&phase<1400){late+=err;++ln;}
   if((duration==2000||sc==25||sc==42)&&tick%5==0)trace<<sc<<','<<tick<<','<<x<<','<<y<<','<<input.right_x<<','<<input.right_y<<','<<out.right_x<<','<<out.right_y<<','<<d.requested_assist_stick.x<<','<<d.requested_assist_stick.y<<','<<int(p.mode)<<','<<p.aim_authority<<','<<p.source_capture_age_ms<<','<<p.bodylock_target_motion_valid<<','<<p.bodylock_target_motion_px_per_sec.x<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<p.manual_correction_x<<','<<p.manual_exit_requested<<','<<duration<<','<<p.response_scale<<','<<response<<','<<delay<<'\n';
   c.observe_delivered_output(out,true,now);previous={out.right_x,out.right_y};
   const auto camera=plant.front();plant.pop_front();plant.push_back(forward_aim_response_curve(previous,cfg.aim_response_curve));
   x+=(vx-response*camera.x)*.001f;y+=(vy+response*camera.y+(input.rb?.2f*response:0))*.001f;
  }
  std::sort(errors.begin(),errors.end());
  csv<<seed<<','<<sc<<','<<duration<<','<<motion<<','<<manual<<','<<sum/n<<','<<late/ln<<','<<errors[errors.size()*95/100]<<','<<errors.back()<<','<<ads<<','<<body<<','<<handoffs<<','<<exits<<','<<stale<<','<<delta_sum/n<<','<<peak<<'\n';
 }
 std::cout<<"96 short/long cases, 1056000 ticks, invariants passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
