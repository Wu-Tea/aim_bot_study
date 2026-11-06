#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cstring>
namespace {
using namespace vision_native;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Scene {
    VisionTargetSelector selector{640,512,190,true,.25f,.8f};
    DetectionBatch batch;
    std::vector<unsigned char> pixels=std::vector<unsigned char>(640*512*4);
    Scene(){selector.set_cue_identity_enabled(true);batch.captured_at_ns=1'000'000'000;}
    void clear(){batch.captured_at_ns+=10'000'000;batch.detections.clear();std::fill(pixels.begin(),pixels.end(),0);}
    void box(float x,float y,float w,float h,float conf=.9f){Detection d;d.x1=x;d.y1=y;d.x2=x+w;d.y2=y+h;d.conf=conf;batch.detections.push_back(d);}
    void cue(int x,int y,bool friendly=false){for(int j=-5;j<=5;++j)for(int i=-5;i<=5;++i)if(i*i+j*j>=9&&i*i+j*j<=25){auto*p=&pixels[((y+j)*640+x+i)*4];p[0]=255;p[friendly?1:2]=255;}}
    VisionResult run(){return selector.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});}
};
void delayed_cue(){
 for(unsigned seed:{20261102u,846193u,73129u,407771u})for(int frames:{20,400})for(int gap:{0,4,10}){
    Scene s;std::mt19937 rng(seed);int x=270+int(rng()%60),y=160+int(rng()%35);
    s.box(float(x-25),float(y+12),50,120);s.run();s.clear();s.box(float(x-25),float(y+12),50,120);auto body=s.run();check(body.has_target,"person-only setup");
    for(int k=0;k<gap;++k){s.clear();check(!s.run().has_target,"empty frame must not actuate");}
    s.clear();s.cue(x,y);auto r=s.run();
    if(!r.has_target || r.fire_authority)throw std::runtime_error("delayed cue gap="+std::to_string(gap)+" target="+std::to_string(r.has_target)+" fire="+std::to_string(r.fire_authority)+" reason="+r.selection_diagnostics.cue_reason+" outcome="+r.selection_diagnostics.outcome);
    const float offset=r.target_y-y;
    for(int k=0;k<frames;++k){s.clear();s.cue(x+k%3,y+k%4);r=s.run();check(r.has_target&&!r.fire_authority&&std::abs(r.target_y-y-k%4-offset)<.001f,"continuous cue translates body without recursive drift");}
    s.selector.reset();s.clear();s.cue(x,y);check(!s.run().has_target,"reset must discard recent person");
 }
 for(bool expired:{false,true}){Scene s;s.box(295,192,50,120);s.run();s.clear();s.box(295,192,50,120);s.run();
    if(expired)for(int k=0;k<20;++k){s.clear();check(!s.run().has_target,"no stale body output");}
    s.clear();s.cue(310,180);if(!expired)s.cue(330,180);
    check(!s.run().has_target,"ambiguous/expired cue cannot invent identity");
 }
}
void friendly_background(){
 Scene s;s.box(137.917f,162.917f,69.416f,105.916f,.733398f);
 // A narrow visible part of a large cyan wall can occupy the old ROI ratio.
 for(int y=60;y<200;++y)for(int x=160;x<166;++x){auto*p=&s.pixels[(y*640+x)*4];p[0]=p[1]=120;}
 auto r=s.run();check(!r.detections[0].is_friendly,"large cyan background is not a compact friendly marker");
 s.clear();s.box(137.917f,162.917f,69.416f,105.916f);s.cue(172,143,true);r=s.run();
 check(r.detections[0].is_friendly&&!r.has_target,"real compact friendly marker must still reject");
 for(int radius:{2,3,5,7,8})for(int brightness:{80,160,255})for(bool diamond:{false,true}){
    Scene f;f.box(295,192,50,120);
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){
        const bool paint=diamond ? std::abs(x)+std::abs(y)<=radius :
            x*x+y*y<=radius*radius && x*x+y*y>=std::max(1,(radius-2)*(radius-2));
        if(paint){auto*p=&f.pixels[((177+y)*640+320+x)*4];p[0]=p[1]=static_cast<unsigned char>(brightness);}
    }
    const auto current=f.run();check(current.detections[0].is_friendly,"small/dim friendly circles and diamonds must keep hard rejection");
 }
}
void distant_body(){
 Scene s;s.box(308,230,22.5f,39.5f);s.run();s.clear();s.box(308,230,22.5f,39.5f);auto r=s.run();
 check(r.has_target&&r.has_selected_detection,"credible distant person must not be rejected by screen-area quota");
 check(std::abs(r.target_y-239.875f)<.001f,"small target retains configured 25 percent anatomy");
 s.selector.reset();s.clear();s.box(308,230,2,3);check(!s.run().has_target,"subpixel/noise geometry is not a person");
 s.clear();s.box(308,230,19,24);s.run();s.clear();s.box(308,230,19,24);
 check(!s.run().has_target,"head fragments must retain the existing tracking geometry floor");
 // November 6: a moderate-confidence body has the same admission as a
 // marked person. Later cue evidence still bridges the detector gap.
 Scene moving;moving.box(270,230,24,40,.50f);check(moving.run().has_target,"unmarked body has equal admission authority");
 moving.clear();moving.box(278,230,24,40,.50f);check(moving.run().has_target,"moving unmarked person retains authority");
 for(int i=0;i<3;++i){moving.clear();check(!moving.run().has_target,"prediction alone cannot actuate");}
 moving.clear();moving.cue(322,212);auto first=moving.run();
 moving.clear();moving.cue(322,212);auto paired=moving.run();
 check(paired.has_target&&!paired.fire_authority,"delayed unique marker corroborates moving small body");
 // The first real pair replaces provisional inferred motion/marker offset.
 moving.clear();moving.box(310,227,24,44,.9f);moving.cue(322,212);auto measured=moving.run();
 check(measured.has_selected_detection&&std::abs(measured.target_y-238)<.001f,"current body must replace provisional reference");
}
void body_reference_refresh(){
 Scene s;
 const auto step=[&](float x,float y,float w,float h,int cx,int cy){s.clear();s.box(x,y,w,h,.9f);s.cue(cx,cy);return s.run();};
 // Keep the initial anatomical point outside the 11 px marker. The former
 // cy=231/232 fixture aimed inside its marker and is now rejected explicitly.
 step(303,223.333f,27,48.334f,312,220);step(303,223.333f,27,48.334f,312,220);
 step(311.667f,224.333f,23.833f,40.334f,320,221);step(310,207.167f,28.167f,62,321,212);
 auto r=step(308.167f,216,26.833f,52.333f,318,198);
 check(r.has_selected_detection && std::abs(r.target_y-(216+.25f*52.333f))<.01f,"same-size current body must replace outdated cue offset");
 s.clear();s.cue(320,200);auto held=s.run();check(held.has_target&&!held.fire_authority&&std::abs(held.target_y-r.target_y-2)<.01f,"updated body owns following cue-only point");
}
}
void register_november_acquisition_tests(native_test::Registry& registry){
 registry.add_case("BaseVisionSelection","november_delayed_cue",delayed_cue);
 registry.add_case("BaseVisionSelection","november_friendly_background",friendly_background);
 registry.add_case("BaseVisionSelection","november_distant_body",distant_body);
 registry.add_case("BaseVisionSelection","november_body_reference_refresh",body_reference_refresh);
}
#ifdef NOVEMBER_STANDALONE
int main(){int failures=0;for(auto test:{delayed_cue,friendly_background,distant_body,body_reference_refresh})try{test();std::cout<<"PASS\n";}catch(const std::exception&e){++failures;std::cout<<"FAIL "<<e.what()<<'\n';}return failures?1:0;}
#endif
