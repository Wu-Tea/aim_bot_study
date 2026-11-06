#include "vision_native/target_selector.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
template<class T>void read(std::ifstream& in,T& v){in.read(reinterpret_cast<char*>(&v),sizeof(v));if(!in)throw std::runtime_error("truncated fixture");}
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("fixture.bin required");std::ifstream in(argv[1],std::ios::binary);
 vision_native::VisionTargetSelector selector(640,512,190,true,.25f,.65f);selector.set_cue_identity_enabled(true);
 std::vector<unsigned char> pixels(640*512*4);unsigned index=0;
 std::cout<<"frame,has_target,target_x,target_y,cue_x,cue_y,offset_x,offset_y,source,reason,generation\n";
 while(in.peek()!=EOF){vision_native::DetectionBatch b;unsigned n;read(in,b.captured_at_ns);read(in,n);if(n>1000)throw std::runtime_error("count");
  for(unsigned i=0;i<n;++i){vision_native::Detection d;read(in,d.x1);read(in,d.y1);read(in,d.x2);read(in,d.y2);read(in,d.conf);b.detections.push_back(d);}
  in.read(reinterpret_cast<char*>(pixels.data()),pixels.size());if(!in)throw std::runtime_error("pixels");
  const auto r=selector.select_with_frame(b,{pixels.data(),640,512,2560,0,0,640,512,vision_native::PixelFormat::BGRA8});
  std::cout<<index++<<','<<r.has_target<<','<<r.target_x<<','<<r.target_y<<','<<r.motion_cue_x<<','<<r.motion_cue_y<<','<<r.target_x-r.motion_cue_x<<','<<r.target_y-r.motion_cue_y<<','<<r.target_source<<','<<r.selection_diagnostics.cue_reason<<','<<r.selector_target_generation<<'\n';
 }
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
