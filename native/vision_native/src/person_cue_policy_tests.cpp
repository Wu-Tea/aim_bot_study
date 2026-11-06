#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#ifndef PERSON_CUE_STANDALONE
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
#include "tracking_native/tracker_authority.h"
#endif
namespace {
using namespace vision_native;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Scene {
 VisionTargetSelector selector{640,512,190,true,.25f,.65f};
 DetectionBatch batch;std::vector<unsigned char> pixels=std::vector<unsigned char>(640*512*4);
 Scene(){selector.set_cue_identity_enabled(true);batch.captured_at_ns=1'000'000'000;}
 void clear(){batch.captured_at_ns+=10'000'000;batch.detections.clear();std::fill(pixels.begin(),pixels.end(),0);}
 void body(float x,float y,float w,float h,float confidence=.9f){Detection d;d.x1=x;d.y1=y;d.x2=x+w;d.y2=y+h;d.conf=confidence;batch.detections.push_back(d);}
 void cue(int x,int y,bool friendly=false){for(int j=-5;j<=5;++j)for(int i=-5;i<=5;++i)if(i*i+j*j>=9&&i*i+j*j<=25){auto*p=&pixels[((y+j)*640+x+i)*4];p[0]=255;p[friendly?1:2]=255;}}
 VisionResult run(){return selector.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});}
};
void equal_person_admission(){
 for(float confidence:{.42f,.5f,.64f,.9f})for(bool flat:{false,true}){
  Scene plain,marked;const float w=flat?100.f:50.f,h=flat?50.f:100.f;
  for(int tick=0;tick<3;++tick){plain.clear();marked.clear();
   plain.body(320-w/2,200,w,h,confidence);marked.body(320-w/2,200,w,h,confidence);marked.cue(320,185);
   const auto a=plain.run(),b=marked.run();
   check(a.has_target==b.has_target,"marker presence must not change person admission timing");
   if(tick==2){check(a.has_target&&a.aim_authority&&b.aim_authority,"both persons have full aim authority");
    check(std::strcmp(a.target_source,"observed")==0&&std::strcmp(b.target_source,"observed")==0,"unmarked posture must not downgrade person tier");
    check(a.fire_authority==b.fire_authority,"equal observed bodies must share authority");}
  }
 }
}
void equal_person_ranking(){
 Scene s;s.body(235,200,50,100);s.body(355,200,50,100);s.cue(380,185);const auto r=s.run();
 const auto& d=r.selection_diagnostics;
 check(d.count==2&&std::abs(d.boxes[0].rank_score-d.boxes[1].rank_score)<.01f,"enemy marker must not outvote an equally credible unmarked person");
 Scene priority;priority.body(400,220,50,100);priority.cue(320,185);
 const auto person=priority.run();
 check(person.has_selected_detection&&person.target_x==425&&person.target_y==245,
     "a closer orphan cue must not displace an available person");
}
void friendly_remains_rejected(){
 Scene s;for(int i=0;i<3;++i){s.clear();s.body(295,200,50,100);s.cue(320,185,true);check(!s.run().has_target,"friendly body must never acquire");}
}
void cue_without_person_can_locate(){
 for(unsigned seed:{260611u,1949u,731u,4867u})for(int length:{20,400}){
  Scene s;std::mt19937 rng(seed);int x=280+int(rng()%60),y=170+int(rng()%30);
  for(int i=0;i<length;++i){s.clear();s.cue(x+i%3,y+i%4);auto r=s.run();
   if(i>=1){check(r.has_target&&r.aim_authority&&!r.fire_authority,"unpaired enemy cue may acquire aim only");
    check(!r.has_body_box&&!r.has_selected_detection,"cue offset must not fabricate a detected body");
    check(std::abs(r.target_y-(y+i%4+40.f))<.001f,"configured downwards offset owns cold cue point");}}
  s.clear();check(!s.run().has_target,"empty frame cannot continue cue authority");
 }
}
#ifndef PERSON_CUE_BASELINE
void cue_offset_and_body_transition(){
 Scene s;s.selector.set_cue_pickup_offset_y(80);
 s.cue(320,185);s.run();s.clear();s.cue(320,185);auto cold=s.run();
 check(cold.has_target&&cold.target_y==265,"configured offset must reach selection");
 const auto generation=cold.selector_target_generation;
 s.clear();s.body(295,200,50,100);s.cue(320,185);auto body=s.run();
 check(body.has_selected_detection&&body.has_body_box&&body.target_y==225,"real body must replace temporary offset immediately");
 check(body.selector_target_generation==generation,"body calibration must retain the same cue identity");
 s.clear();s.cue(323,190);auto held=s.run();
 check(held.has_target&&held.has_body_box&&held.target_y==230&&!held.fire_authority,"paired cue must use measured offset instead of configured prior");
 s.selector.reset();s.clear();s.cue(320,185);check(!s.run().has_target,"reset requires new confirmation");
 s.clear();s.cue(320,185);check(s.run().target_y==265,"reset must discard learned geometry but preserve configuration");
 for(float invalid:{-1.f,513.f}){bool threw=false;try{s.selector.set_cue_pickup_offset_y(invalid);}catch(const std::invalid_argument&){threw=true;}check(threw,"invalid offset rejected");}
}
void cue_pickup_boundaries(){
 Scene s;s.cue(290,185);s.cue(350,185);s.run();s.clear();s.cue(290,185);s.cue(350,185);
 check(!s.run().has_target,"ambiguous unpaired cues cannot choose an arbitrary identity");
 s.selector.reset();s.clear();s.cue(100,185);s.run();s.clear();s.cue(100,185);check(!s.run().has_target,"cue offset point obeys pickup range");
 s.selector.reset();s.clear();s.cue(320,185);s.run();check(!s.run().has_target,"duplicate capture cannot satisfy confirmation");
 s.clear();s.cue(400,185);check(!s.run().has_target,"different point cannot inherit unpaired cue confirmation");
 s.clear();s.cue(400,185);check(s.run().has_target,"stable unique point can confirm");
 s.selector.reset();s.selector.set_cue_pickup_offset_y(500);s.clear();s.cue(320,185);s.run();s.clear();s.cue(320,185);
 check(!s.run().has_target,"offscreen offset cannot be clamped into an invented target");
}
void unrelated_person_cannot_hide_current_weak_person(){
 for(float challenger_x:{55.f,175.f}){
  Scene s;s.body(395,110,50,100);check(s.run().has_target,"establish person identity");
  s.clear();s.body(400,115,50,100,.3f);s.body(challenger_x,200,50,100,.5f);
  const auto r=s.run();
  check(r.has_target&&r.has_selected_detection&&r.selected_detection_index==0,
      "unrelated candidate must not hide a currently associated weak person");
  check(!r.fire_authority&&std::strcmp(r.target_source,"associated_weak")==0,
      "weak person continuity retains weak authority");
 }
}
#endif
#ifndef PERSON_CUE_STANDALONE
void cue_selector_adapter_controller_chain(){
 check(tracking_native::classify_target_tier("cue_unpaired")==tracking_native::TargetTierClass::CueHold,
     "unpaired cue must remain cue authority in shared consumers");
 for(unsigned seed:{1867u,5911u,8347u,12037u})for(int length:{20,400}){
  Scene s;s.selector.set_cue_pickup_offset_y(80);
  std::mt19937 rng(seed);const int x=280+int(rng()%80),y=170+int(rng()%30);
  double now=1.0;controller_native::NativeGamepadController controller(
      controller_native::incident_fixture::base_config(50,180),&now);
  std::uint64_t identity=0;
  for(int i=0;i<length+1;++i){
   s.clear();s.cue(x,y);if(i==length)s.body(x-25,y+15,50,100);
   auto r=s.run();r.frame_id=i+1;r.frame_updated=true;r.captured_at_ns=s.batch.captured_at_ns;
   r.result_at_ns=r.captured_at_ns;now=double(r.captured_at_ns)*1e-9;
   controller.submit_vision_snapshot(runtime_app::adapt_vision_result(r));
   const auto out=controller.build_output(controller_native::incident_fixture::ads_input());
   const auto& plan=controller.last_target_plan();
   if(i==0){check(!r.has_target&&plan.aim_authority==0,"pending cue cannot grant control");continue;}
   check(plan.target_id!=0&&plan.aim_authority>0,"confirmed selector cue must survive adapter and controller");
   if(i==1)identity=plan.target_id;
   check(plan.target_id==identity,"person transition must preserve controller identity");
   check(std::abs(plan.source_aim_px.y-r.target_y)<.001f,"source point survives full chain");
   check(std::isfinite(out.right_x)&&std::isfinite(out.right_y),"full chain output finite");
   if(i<length)check(plan.cue_continuation&&!plan.fire_authority&&plan.normalized_size==0,
       "unpaired cue must have aim-only authority and no inferred body");
   else check(!plan.cue_continuation&&plan.normalized_size>0,"observed body replaces provisional geometry");
  }
 }
}
#endif
}
void register_person_cue_policy_tests(native_test::Registry& registry){
 registry.add_case("BaseVisionSelection","person_marker_equal_admission",equal_person_admission);
 registry.add_case("BaseVisionSelection","person_marker_equal_ranking",equal_person_ranking);
 registry.add_case("BaseVisionSelection","person_marker_friendly_reject",friendly_remains_rejected);
 registry.add_case("BaseVisionSelection","cue_without_person_offset_acquisition",cue_without_person_can_locate);
#ifndef PERSON_CUE_BASELINE
 registry.add_case("BaseVisionSelection","cue_offset_body_transition",cue_offset_and_body_transition);
 registry.add_case("BaseVisionSelection","cue_offset_pickup_boundaries",cue_pickup_boundaries);
 registry.add_case("BaseVisionSelection","unrelated_person_preserves_weak_identity",unrelated_person_cannot_hide_current_weak_person);
#endif
#ifndef PERSON_CUE_STANDALONE
 registry.add_case("BaseEndToEnd","cue_selector_adapter_controller_chain",cue_selector_adapter_controller_chain);
#endif
}
#ifdef PERSON_CUE_STANDALONE
int main(){int failures=0;for(auto test:{equal_person_admission,equal_person_ranking,friendly_remains_rejected,cue_without_person_can_locate
#ifndef PERSON_CUE_BASELINE
,cue_offset_and_body_transition,cue_pickup_boundaries,unrelated_person_cannot_hide_current_weak_person
#endif
})try{test();std::cout<<"PASS\n";}catch(const std::exception&e){++failures;std::cout<<"FAIL "<<e.what()<<'\n';}return failures?1:0;}
#endif
