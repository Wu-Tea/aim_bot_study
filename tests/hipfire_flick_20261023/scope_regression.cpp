// Same integration invariant as BaseEndToEnd, linked against each real core.
#include "controller_native/incident_fixture_support.h"
#include <iostream>
using namespace controller_native;
int main(){
 auto cfg=incident_fixture::base_config(40,200);
 cfg.ai_aim.normalized_assist_parameters=true;cfg.ai_aim.hipfire_ratio=.7f;
 cfg.ai_aim.aim_response_learning_enabled=false;cfg.auto_fire.manual_fire_activates_ai_aim=true;
 double now=10;NativeGamepadController controller(cfg,&now);
 incident_fixture::TargetSpec spec;spec.selector_generation=17;spec.body_height=300;
 spec.has_enemy_cue=spec.enemy_identity_confirmed=true;float previous=0,delta=0;
 for(int tick=0;tick<=100;++tick){
  now=10+tick*.001;spec.observation_id=tick+1;
  controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,tick+1,now,-100,0));
  auto input=incident_fixture::ads_input();input.left_trigger=tick>=100?1.f:0.f;input.rb=true;
  controller.build_output(input);const float current=controller.last_output_components().shaped_assist_stick.x;
  if(tick==100){if(std::fabs(previous)<.1f)return 3;delta=std::fabs(current-previous);}
  previous=current;
 }
 std::cout<<"actual native ADS entry step="<<delta<<" bound=0.0641\n";
 return delta<=.0641f?0:2;
}
