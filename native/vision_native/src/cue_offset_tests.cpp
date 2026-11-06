#include "vision_native/target_selector.h"
#include "vision_native/cue_acquisition_policy.h"
#include "test_support/native_test_registry.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#ifndef CUE_OFFSET_STANDALONE
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
#endif
namespace {
using namespace vision_native;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Scene {
 VisionTargetSelector selector;
 DetectionBatch batch;std::vector<unsigned char> pixels=std::vector<unsigned char>(640*512*4);
 explicit Scene(float ratio=.25f):selector{640,512,190,true,ratio,.65f}{selector.set_cue_identity_enabled(true);batch.captured_at_ns=1000000000;}
 void next(int x,int y){batch.captured_at_ns+=10000000;++batch.frame_id;batch.detections.clear();std::fill(pixels.begin(),pixels.end(),0);
  for(int j=-5;j<=5;++j)for(int i=-5;i<=5;++i)if(i*i+j*j>=9&&i*i+j*j<=25){auto*p=&pixels[((y+j)*640+x+i)*4];p[0]=p[2]=255;}}
 void body(int x,int y,float scale,bool upper_body=false){Detection d;const float horizontal=upper_body?1.f:scale;
  d.x1=x-15*horizontal;d.x2=x+35*horizontal;d.y1=y+15*horizontal;d.y2=d.y1+100*scale;d.conf=.95f;batch.detections.push_back(d);}
 VisionResult run(){auto r=selector.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});
  r.frame_id=batch.frame_id;r.frame_updated=true;r.captured_at_ns=r.result_at_ns=batch.captured_at_ns;return r;}
};
void contracting_person_cannot_relearn_cue_offset(){
 for(float ratio:{.25f,.35f})for(bool upper_body:{false,true})
 for(unsigned seed:{61106u,731u,52837u,101141u})for(int duration:{40,600}){
  Scene s(ratio);std::mt19937 rng(seed);const int x=280+int(rng()%50),y=175+int(rng()%30);
  s.next(x,y);s.body(x,y,1);const auto initial=s.run();require(initial.has_selected_detection,"complete seed");
  const float dx=initial.target_x-x,dy=initial.target_y-y;
  for(int tick=0;tick<duration;++tick){const int cx=x+int(8*std::sin(tick*.04)),cy=y+int(6*std::sin(tick*.05));
   s.next(cx,cy);if(tick<25)s.body(cx,cy,std::max(.2f,1-(tick+1)*.04f),upper_body);
   auto r=s.run();require(r.has_target,"current unique cue must preserve target through occlusion");
   require(r.target_y>cy+5.5f,"aim point must remain outside the marker");
   require(std::abs(r.target_x-cx-dx)<.001f&&std::abs(r.target_y-cy-dy)<.001f,
       "partial person must use calibrated displacement immediately, before detector disappearance");
   require(!r.fire_authority,"partial geometry continued by cue is aim only");
  }
  s.next(x,y);s.body(x,y,1);require(s.run().has_selected_detection,"complete body returns immediately");
  // A complete body moving relative to its UI marker must refresh in either
  // direction; preserving an occlusion offset must not freeze real anatomy.
  s.next(x,y);s.body(x,y,1);s.batch.detections[0].y1-=5;s.batch.detections[0].y2-=5;
  require(s.run().has_selected_detection,"complete translated body remains primary");
  s.next(x,y);const auto updated=s.run();
  require(updated.has_target&&std::abs(updated.target_y-y-(dy-5))<.001f,
      "complete body refreshes a smaller valid cue offset");
  // ADS can first enlarge the body. The next contraction must compare with
  // that measured pair, not the smaller initial pre-ADS calibration.
  s.next(x,y);s.body(x,y,1.1f);require(s.run().has_selected_detection,"larger complete body refreshes");
  for(int i=0;i<12;++i){s.next(x,y);if(i<8)s.body(x,y,1.05f);const auto held=s.run();
   require(held.has_target&&std::abs(held.target_y-y-dy*1.1f)<.001f,
       "post-ADS fragments cannot replace the latest measured offset");}
 }
}
void clipped_body_cannot_aim_at_cue(){
 Scene s;
 for(int i=0;i<8;++i){s.next(320,200);if(i<4){Detection d;d.x1=240;d.x2=400;d.y1=96;d.y2=512;d.conf=.95f;s.batch.detections.push_back(d);}
  const auto r=s.run();if(r.has_target){require(r.target_y>205.5f,"clipped body must not establish a zero cue offset");require(!r.has_body_box&&!r.fire_authority,"invalid body anatomy cannot grant body/fire authority");}
  if(i>=1)require(r.has_target,"unique cue retains configured fallback acquisition");
 }
}
void cue_recovery_keeps_anatomy_and_search_coordinates_separate(){
 Scene s;s.next(320,185);s.body(320,185,1);const auto initial=s.run();
 for(int i=1;i<=20;++i){s.next(320+i/4,185);s.body(320+i/4,185,std::max(.3f,1-i*.04f));s.run();}
 for(int i=0;i<6;++i){s.batch.captured_at_ns+=10000000;s.batch.detections.clear();std::fill(s.pixels.begin(),s.pixels.end(),0);
  require(!s.run().has_target,"a retained offset cannot actuate an empty frame");}
 s.next(328,186);const auto recovered=s.run();
 require(recovered.has_target&&!recovered.fire_authority,"recent cue must recover after a short complete gap");
 require(std::abs(recovered.target_x-328-(initial.target_x-320))<.001f&&
     std::abs(recovered.target_y-186-(initial.target_y-185))<.001f,
     "cue search translation must not subtract an anatomical hold from a fragment point");
}
void marker_geometry_rejects_zero_cold_offset(){
 const std::vector<CueObservation> cues{{320,200,11,1,1}};
 for(float offset:{0.f,1.f,5.5f})require(!choose_cue_acquisition(cues,{}, {320,256,640,512,190,offset}),
     "cold cue point must be below the marker footprint");
 require(choose_cue_acquisition(cues,{}, {320,256,640,512,190,10}).has_value(),"minimum supported offset is outside marker");
 Scene s;for(float offset:{0.f,9.f}){bool threw=false;try{s.selector.set_cue_pickup_offset_y(offset);}catch(const std::invalid_argument&){threw=true;}
  require(threw,"configuration cannot aim directly into cue");}
}
#ifndef CUE_OFFSET_STANDALONE
void cue_offset_survives_complete_controller_chain(){
 for(float ratio:{.25f,.35f})for(bool upper_body:{false,true})
 for(unsigned seed:{9701u,10903u,21517u,30727u})for(int duration:{40,600}){
  Scene s(ratio);double now=1;std::mt19937 rng(seed);
  controller_native::NativeGamepadController controller(controller_native::incident_fixture::base_config(50,190),&now);
  const int x=280+int(rng()%70),y=180+int(rng()%30);std::uint64_t identity=0;
  for(int tick=0;tick<duration;++tick){s.next(x,y);if(tick<25)s.body(x,y,tick<5?1:std::max(.2f,1-(tick-4)*.04f),upper_body);
   auto r=s.run();now=r.captured_at_ns*1e-9;controller.submit_vision_snapshot(runtime_app::adapt_vision_result(r));
   const auto output=controller.build_output(controller_native::incident_fixture::ads_input());const auto& plan=controller.last_target_plan();
   if(tick==0)identity=plan.target_id;
   require(identity&&plan.target_id==identity,"occlusion must retain controller target identity");
   require(std::abs(plan.source_aim_px.y-r.target_y)<.001f,"controller must preserve anatomical source point");
   if(tick>=5)require(std::abs(plan.source_aim_px.y-y-(15+100*ratio))<.001f&&plan.cue_continuation&&!plan.fire_authority,
       "controller must use cue-relative anatomy from the first contracting person frame");
   require(std::isfinite(output.right_x)&&std::isfinite(output.right_y),"bounded finite control");
  }
 }
}
#endif
}
void register_cue_offset_tests(native_test::Registry& registry){
 registry.add_case("BaseVisionSelection","cue_offset_survives_contracting_body",contracting_person_cannot_relearn_cue_offset);
 registry.add_case("BaseVisionSelection","cue_offset_clipped_body_never_marker",clipped_body_cannot_aim_at_cue);
 registry.add_case("BaseVisionSelection","cue_offset_zero_and_marker_footprint",marker_geometry_rejects_zero_cold_offset);
 registry.add_case("BaseVisionSelection","cue_offset_gap_recovery_coordinates",cue_recovery_keeps_anatomy_and_search_coordinates_separate);
#ifndef CUE_OFFSET_STANDALONE
 registry.add_case("BaseEndToEnd","cue_offset_person_ads_occlusion_chain",cue_offset_survives_complete_controller_chain);
#endif
}
#ifdef CUE_OFFSET_STANDALONE
int main(int argc,char**argv){native_test::Registry registry;register_cue_offset_tests(registry);return native_test::run(registry,argc,argv,"CueOffsetTests");}
#endif
