#include "controller_native/incident_fixture_support.h"
#include <iostream>
using namespace controller_native;
namespace f=incident_fixture;
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("profile output.csv");
 auto cfg=load_runtime_config(argv[1]).gamepad;cfg.auto_fire.enabled=false;cfg.recoil.enabled=false;cfg.tracker.max_observation_age_ms=40;
 std::ofstream out(argv[2]);out<<"height,error,tick,source,target_id,mode,authority,radius,source_x,source_y,out_x,out_y,fire_authority\n";
 int failures=0,confirmed=0;
 // Frozen geometry isolates source classification from camera response or
 // target motion. This is a policy probe, not a tracking-accuracy simulation.
 for(float height:{50.f,220.f,360.f})for(float error:{80.f,125.f,145.f,175.f}){
  double now=10;NativeGamepadController c(cfg,&now);f::TargetSpec s;s.body_height=height;s.body_width=height*.7f;s.selector_generation=1;s.has_enemy_cue=true;s.enemy_identity_confirmed=true;s.confidence=.9f;s.aim_height_ratio=.2f;
  float before=-1,cue=-1,after=-1;std::uint64_t identity=0;
  for(int tick=0;tick<800;++tick){now=10+tick*.001;const bool is_cue=tick>=400&&tick<600;
   const float dx=tick<200?0:error; // establish completed ADS before the policy comparison
   if(tick%8==0){s.observation_id=tick/8+1;c.submit_vision_snapshot(is_cue?f::cue_snapshot(s,s.observation_id,now,dx,0):f::observed_snapshot(s,s.observation_id,now,dx,0));}
   const auto result=c.build_output(f::ads_input());const auto&p=c.last_target_plan();
   if(tick==399){before=p.aim_authority;identity=p.target_id;}if(tick==599)cue=p.aim_authority;if(tick==799)after=p.aim_authority;
   if(tick>=250&&((p.target_id!=identity&&tick>=400)||std::abs(p.source_aim_px.x-(320+error))>.001||std::abs(p.source_aim_px.y-256)>.001))++failures;
   if(is_cue&&p.fire_authority)++failures;
   if(tick%8==0||tick==399||tick==599||tick==799)out<<height<<','<<error<<','<<tick<<','<<(is_cue?"cue":"person")<<','<<p.target_id<<','<<int(p.mode)<<','<<p.aim_authority<<','<<p.position_response_radius_px<<','<<p.source_aim_px.x<<','<<p.source_aim_px.y<<','<<result.right_x<<','<<result.right_y<<','<<p.fire_authority<<'\n';
   // No feedback is synthesized for this frozen-measurement policy check.
  }
  if(before>0&&cue==0&&after>0)++confirmed;
  std::cout<<"height="<<height<<" error="<<error<<" authority="<<before<<" -> "<<cue<<" -> "<<after<<'\n';
 }
 std::cout<<"source-only cutoffs="<<confirmed<<" other invariant failures="<<failures<<'\n';return failures||confirmed?2:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
