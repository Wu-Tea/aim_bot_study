#include "vision_native/target_selector.h"
#include <fstream>
#include <iostream>
using namespace vision_native;
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("fixture binary path");
 std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char> pixels(640*512*4);
 for(int mode=0;mode<3;++mode){
  VisionTargetSelector s(640,512,190,true,.2f,.65f);s.set_cue_identity_enabled(true);
  Detection d;float values[5];in.read((char*)values,sizeof(values));
  d.x1=values[0];d.y1=values[1];d.x2=values[2];d.y2=values[3];d.conf=values[4];
  in.read((char*)pixels.data(),pixels.size());if(!in)throw std::runtime_error("truncated fixture");
  DetectionBatch batch;batch.detections={d};batch.captured_at_ns=1'000'000'000;
  auto r=s.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});
  batch.captured_at_ns+=10'000'000;
  r=s.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});
  std::cout<<mode<<" target="<<r.has_target<<" friend="<<r.detections[0].is_friendly<<" cue="<<r.detections[0].has_cue_point<<" source="<<r.target_source<<'\n';
  if(mode==1 && (!r.has_target||r.detections[0].is_friendly||!r.detections[0].has_cue_point))throw std::runtime_error("recolored real enemy failed");
  if(mode==2 && (r.has_target||!r.detections[0].is_friendly))throw std::runtime_error("recolored real friendly was not excluded");
  if(mode==1){batch.detections.clear();batch.captured_at_ns+=10'000'000;
   const auto held=s.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8});
   if(!held.has_target||held.fire_authority||held.selector_target_generation!=r.selector_target_generation)throw std::runtime_error("real marker cue continuation failed");
  }
 }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
