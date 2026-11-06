#include "vision_native/target_selector.h"
#include <algorithm>
#include <iostream>
int main(){
 using namespace vision_native;
 for(int scenario=0;scenario<3;++scenario){
  VisionTargetSelector s(640,512,190,true,.25f,.65f);s.set_cue_identity_enabled(true);
  DetectionBatch b;b.captured_at_ns=1000000000;
  std::vector<unsigned char> pixels(640*512*4);
  for(int frame=0;frame<35;++frame){
   b.captured_at_ns+=10000000;b.detections.clear();std::fill(pixels.begin(),pixels.end(),0);
   const int cx=320,cy=scenario==2?200:185;
   for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)if(x*x+y*y>=9&&x*x+y*y<=25){auto*p=&pixels[((cy+y)*640+cx+x)*4];p[0]=p[2]=255;}
   if(frame<25){
    Detection d;d.conf=.95f;
    const float scale=frame<5?1.f:std::max(.2f,1.f-(frame-4)*.04f);
    d.x1=320-25*scale;d.x2=320+25*scale;
    d.y1=185+15*scale;d.y2=d.y1+100*scale;
    if(scenario==1){d.x1=295;d.x2=345;d.y1=200;d.y2=200+100*scale;}
    if(scenario==2){d.x1=240;d.x2=400;d.y1=96;d.y2=512;}
    b.detections.push_back(d);
   }
   const auto r=s.select_with_frame(b,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});
   std::cout<<scenario<<','<<frame<<','<<r.has_target<<','<<r.target_y<<','<<cy<<','<<r.target_y-cy<<','<<r.target_source<<','<<r.selection_diagnostics.cue_reason<<'\n';
  }
 }
}
