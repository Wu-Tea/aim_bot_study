#include "controller_native/incident_fixture_support.h"
#include "controller_native/assist_control_state_machine.h"
#include "controller_native/aim_response_curve_plugin.h"
#include "controller_native/response_model_aim_solver.h"
#include <deque>
#include <random>
#include <iomanip>
#include <iostream>
using namespace controller_native;
using namespace pipeline_contract;
namespace f=controller_native::incident_fixture;
int main(int argc,char**argv){try{
 if(argc!=4)throw std::runtime_error("config output-dir seed");
 auto base=load_runtime_config(argv[1]).gamepad;
 std::filesystem::create_directories(argv[2]);
 std::ofstream solver(std::string(argv[2])+"/solver.csv");solver<<"error,y,rate,output\n";
 for(float e:{2.f,10.f,20.f,60.f})for(float y:{0.f,-150.f})for(float rate:{-500.f,0.f,500.f}){
  ResponseModelAimRequest r;r.error_px={e,y};r.relative_velocity_px_per_sec={rate,0};r.response_px_per_stick_second=900;r.arrival_horizon_seconds=.06f;r.motion_weight=.003f/.06f;r.motion_is_error_rate_lookahead=true;r.authority=1;r.range_position_response=true;r.position_range_px=190;r.minimum_position_stick=.5f;r.arrival_radius_px=2;r.response_curve=base.aim_response_curve;
  solver<<e<<','<<y<<','<<rate<<','<<solve_response_model_aim(r).stick.x<<'\n';
 }
 std::ofstream arb(std::string(argv[2])+"/arbiter.csv");arb<<"mode,manual,ai,correction,output\n";
 for(auto mode:{ControlMode::AdsAcquire,ControlMode::BodyLockFollow})for(float m:{.18f,.3f,.6f})for(float ai:{-.1f,0.f,.1f,.2f})for(bool correction:{false,true}){
  AssistControlStateMachineConfig cfg;cfg.use_gamepad_intent_for_arbitration=true;
  AssistControlStateMachine a(cfg);AssistControlStateMachineInput in;
  in.activation=AssistActivation::Engaged;in.target_authoritative=in.fresh_observation=true;in.target_id=in.selector_target_generation=1;in.mode=mode;in.ads_acquisition_state=AdsAcquisitionState::AcquiringNominal;in.now_seconds=10;in.visual_authority=1;in.target_error_px={20,0};in.manual_stick={m,0};in.filtered_manual_stick={m,0};in.ai_stick={ai,0};
  float t=std::clamp((m-.15f)/.15f,0.f,1.f);in.manual_axis_activity={t*t*(3-2*t),0};in.manual_correction_x=correction;
  arb<<int(mode)<<','<<m<<','<<ai<<','<<correction<<','<<a.update(in).stick.x<<'\n';
 }
 std::ofstream out(std::string(argv[2])+"/cases.csv"),trace(std::string(argv[2])+"/trace.csv");
 out<<"case,duration,speed,manual_kind,mean_error,late_error,mean_output,ads_ticks,body_ticks,manual_toward_ticks,reduced_ticks,zero_ticks\n";
 trace<<"case,duration,tick,error,manual,requested,shaped,output,mode,correction,authority,exit\n";
 for(int duration:{2000,20000})for(int sc=0;sc<24;++sc){
  std::mt19937 rng(std::stoul(argv[3])^unsigned(sc*971+duration));
  auto cfg=base;cfg.recoil.enabled=sc%4==3;cfg.auto_fire.enabled=false;
  double now=10;NativeGamepadController c(cfg,&now);const int delay=8+rng()%18;float response=900+rng()%1601;
  std::deque<Vec2f> camera(delay);float error=20,y=0;double sum=0,late=0,work=0;unsigned n=0,ln=0,ads=0,body=0,toward=0,reduced=0,zero=0;
  const float speed=std::array<float,6>{0,60,180,360,600,900}[sc/4];const int kind=sc%4;
  for(int tick=0;tick<duration;++tick){
   int phase=tick%1625;now=10+tick*.001;if(!phase){error=20;y=kind==1?-80.f:0.f;}
   auto input=f::ads_input();input.left_trigger=phase>=25&&phase<1500?1:0;
   input.right_x=kind==0?.3f:kind==1?(phase>=150?.3f:0):kind==2?(phase>=150?std::copysign(.3f,error):0):0;input.rb=kind==3&&phase>=300&&phase<1400;
   if(tick%5==0){f::TargetSpec s;s.observation_id=tick+1;s.selector_generation=tick/1625+1;s.body_width=56;s.body_height=140;s.aim_height_ratio=.2f;s.color_classified=s.has_enemy_cue=s.enemy_identity_confirmed=true;s.confidence=.92f;
    auto snap=phase<1575?f::observed_snapshot(s,tick+1,now,error+(int(rng()%101)-50)*.004f,y):ControllerVisionSnapshot{};
    if(phase>=1575){snap.frame_updated=true;snap.frame_id=tick+1;snap.capture_time_seconds=snap.ready_time_seconds=now;snap.selector_identity_protocol=true;snap.selector_target_generation=s.selector_generation;}
    c.submit_vision_snapshot(snap);
   }
   auto result=c.build_output(input);auto&p=c.last_target_plan();auto&d=c.last_output_components();
   if(!std::isfinite(result.right_x)||!std::isfinite(error))throw std::runtime_error("nonfinite");
   if(phase>=100&&phase<1400){sum+=std::fabs(error);work+=result.right_x;++n;ads+=p.mode==ControlMode::AdsAcquire;body+=p.mode==ControlMode::BodyLockFollow;zero+=p.aim_authority==0;
    if(p.aim_authority>0&&input.right_x*error>0&&std::fabs(error)>5&&std::fabs(input.right_x)>=.15f){++toward;if(result.right_x*std::copysign(1.f,input.right_x)<.7f*std::fabs(input.right_x))++reduced;}}
   if(phase>=700&&phase<1400){late+=std::fabs(error);++ln;}
   if(duration==2000&&tick%5==0)trace<<sc<<','<<duration<<','<<tick<<','<<error<<','<<input.right_x<<','<<d.requested_assist_stick.x<<','<<d.shaped_assist_stick.x<<','<<result.right_x<<','<<int(p.mode)<<','<<p.manual_correction_x<<','<<p.aim_authority<<','<<p.manual_exit_requested<<'\n';
   c.observe_delivered_output(result,true,now);auto movement=camera.front();camera.pop_front();camera.push_back(forward_aim_response_curve({result.right_x,result.right_y},cfg.aim_response_curve));
   error+=(speed-response*movement.x)*.001f;y+=(response*movement.y+(input.rb?.2f*response:0))*.001f;
  }
  out<<sc<<','<<duration<<','<<speed<<','<<kind<<','<<sum/n<<','<<late/ln<<','<<work/n<<','<<ads<<','<<body<<','<<toward<<','<<reduced<<','<<zero<<'\n';
 }
 std::cout<<"48 cases complete\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
