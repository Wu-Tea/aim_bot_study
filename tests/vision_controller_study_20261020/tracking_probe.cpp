#include "controller_native/incident_fixture_support.h"
#include "controller_native/bodylock_target_motion_observer.h"
#include "vision_native/target_selector.h"
#include "record.h"
#include <iostream>
#include <random>
using namespace controller_native;
namespace f=incident_fixture;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 require(argc==3,"profile output-dir");std::filesystem::create_directories(argv[2]);
 std::ofstream motion(std::string(argv[2])+"/motion.csv"),completion(std::string(argv[2])+"/completion.csv"),cross(std::string(argv[2])+"/crossing.csv");
 motion<<"seed,duration,amplitude,dt_a,response,truth,estimated,unclipped_diagnostic,expected_clipped_bias\n";
 for(unsigned seed:{71020401u,71020411u,71020429u,71020439u})for(int duration:{2000,20000})for(int sc=0;sc<32;++sc){
  std::mt19937 rng(seed+sc*104729);const float amplitude=sc<4?(sc==0?5:sc==1?20:sc==2?-20:20):(12+float(rng()%21))*(sc%2?1:-1);
  const float a=sc==3?.0125f:sc<4?.008f:(4+float(rng()%7))*.001f,b=.025f-a,response=sc<4?1400:900+float(rng()%1601);
  BodylockTargetMotionObserver normal;BodylockTargetMotionObserverConfig diag;diag.maximum_target_motion_stick=1000;BodylockTargetMotionObserver unclipped(diag);
  double now=10,normal_sum=0,raw_sum=0,weight=0;int index=0;
  while(now<10+duration*.001){const float dt=index%2?b:a,dx=index%2?-amplitude:amplitude;now+=dt;
   BodylockTargetMotionObservation o{1,now,dt,{dx/dt,0},{},response,.95f,true,false};require(normal.update(o)&&unclipped.update(o),"valid measurement rejected");
   const auto n=normal.estimate(1,now),u=unclipped.estimate(1,now);if(now>=10.5&&n.valid&&u.valid){normal_sum+=n.target_motion_stick.x*response*dt;raw_sum+=u.target_motion_stick.x*response*dt;weight+=dt;}++index;
  }
  const float cap=response*1.25f,expected=(std::clamp(amplitude/a,-cap,cap)*a+std::clamp(-amplitude/b,-cap,cap)*b)/.025f;
  motion<<seed<<','<<duration<<','<<amplitude<<','<<a<<','<<response<<",0,"<<normal_sum/weight<<','<<raw_sum/weight<<','<<expected<<'\n';
  require(std::abs(raw_sum/weight)<.02,"unclipped zero-sum diagnostic lost cancellation");
  require(std::abs(normal_sum/weight)<.02,"bounded observer lost zero-sum cancellation");
 }
 auto cfg=load_runtime_config(argv[1]).gamepad;cfg.auto_fire.enabled=false;cfg.recoil.enabled=false;cfg.tracker.max_observation_age_ms=40;
 // Actual cue triplet extracted from dev 170.9417–170.9667 s. Compare the
 // observer's two-sample displacement integration with the measured net;
 // neither is asserted to be the actor's world velocity.
 const auto records=study::read("tests/vision_controller_study_20261020/trajectories/dev_cue_jitter.records");
 std::vector<vision_native::VisionResult> triplet;for(const auto& r:records)if(r.frame_id==20514||r.frame_id==20516||r.frame_id==20517)triplet.push_back(r);
 require(triplet.size()==3,"missing recorded cue triplet");BodylockTargetMotionObserver real;
 for(int i=1;i<3;++i){const auto&a=triplet[i-1];const auto&b=triplet[i];require(a.has_target&&b.has_target&&a.selector_target_generation==b.selector_target_generation&&study::source_id(a.target_source)==2&&study::source_id(b.target_source)==2,"triplet identity/source changed");const float dt=(b.captured_at_ns-a.captured_at_ns)*1e-9f;
  require(real.update({1,b.captured_at_ns*1e-9,dt,{0,(b.target_y-a.target_y)/dt},{},1400,.95f,false,true}),"triplet rejected");
 }
 const auto estimate=real.estimate(1,triplet.back().captured_at_ns*1e-9);
 const double observed=(triplet.back().target_y-triplet.front().target_y)/((triplet.back().captured_at_ns-triplet.front().captured_at_ns)*1e-9);
 const double bounded_rate=-estimate.target_motion_stick.y*1400;
 require(bounded_rate*observed>0 && std::abs(bounded_rate)<=std::abs(observed)+.02 &&
     std::abs(bounded_rate)>.5*std::abs(observed),"recorded net displacement reversed or erased by saturation");
 std::ofstream recorded(std::string(argv[2])+"/recorded-motion.csv");recorded<<"first_frame,last_frame,net_measured_rate_y,estimated_rate_y,assumed_response\n20514,20517,"<<observed<<','<<-estimate.target_motion_stick.y*1400<<",1400\n";
 completion<<"cue,tick,frame,target,mode,state,source_x,source_y,authority,complete_ns\n";
 for(bool cue:{false,true}){double now=10;NativeGamepadController c(cfg,&now);f::TargetSpec s;s.selector_generation=1;s.confidence=.95f;s.body_height=100;s.has_enemy_cue=s.enemy_identity_confirmed=true;
  for(int tick=0;tick<2000;++tick){now=10+tick*.001;bool fresh=tick%8==0;const float dx=tick<200?30.f:0;
   if(fresh){s.observation_id=tick/8+1;c.submit_vision_snapshot(cue&&tick>=200&&tick<1600?f::cue_snapshot(s,s.observation_id,now,dx,0):f::observed_snapshot(s,s.observation_id,now,dx,0));}
   const auto out=c.build_output(f::ads_input());const auto&p=c.last_target_plan();
   if(fresh)completion<<cue<<','<<tick<<','<<p.source_frame_id<<','<<p.target_id<<','<<int(p.mode)<<','<<int(p.ads_acquisition_state)<<','<<p.source_aim_px.x<<','<<p.source_aim_px.y<<','<<p.aim_authority<<','<<p.ads_acquisition_complete_ns<<'\n';
   require(!p.cue_continuation||!p.fire_authority,"cue fire");
   if(tick==240) require(p.ads_acquisition_complete_ns!=0,"current settled cue must complete ADS");
  }
 }
 cross<<"seed,fps,offset,frame,a_x,b_x,selected_actor,generation,changed,has_target\n";
 for(unsigned seed:{71020401u,71020411u,71020429u,71020439u})for(int fps:{60,120})for(float separation:{0.f,2.f,8.f,20.f}){
  std::mt19937 rng(seed);const float speed=100+rng()%101;vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);selector.set_cue_identity_enabled(true);
  // Exact labelled boxes: A is first acquired alone, B later crosses A. No
  // appearance/cue identity is invented; this isolates person association.
  for(int frame=0;frame<fps*2;++frame){const float t=frame/float(fps),ax=280+speed*t,bx=400-speed*t;
   vision_native::DetectionBatch batch;batch.frame_id=frame+1;batch.captured_at_ns=1000000000ull+std::uint64_t(std::llround(t*1e9));
   auto detection=[](float x,float y){vision_native::Detection d;d.x1=x-20;d.x2=x+20;d.y1=y;d.y2=y+100;d.conf=.95f;d.class_id=0;return d;};
   batch.detections.push_back(detection(ax,236));if(frame>=fps/10)batch.detections.push_back(detection(bx,236+separation));
   const auto r=selector.select(batch);cross<<seed<<','<<fps<<','<<separation<<','<<frame<<','<<ax<<','<<bx<<','<<(r.has_selected_detection?int(r.selected_detection_index):-1)<<','<<r.selector_target_generation<<','<<r.selector_target_changed<<','<<r.has_target<<'\n';
   require(!r.has_selected_detection || r.selected_detection_index==0,"crossing switched actors under the same identity");
  }
 }
 std::cout<<"motion: 256 short/long cases; completion: 2 source variants; crossing: 32 labelled scenarios; diagnostic invariants passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
