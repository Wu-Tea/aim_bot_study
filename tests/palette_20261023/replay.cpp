// No hardware capture, GPU inference, or controller output. Recorded detections
// isolate color selection from the neural model; remapping is outside timings.
#include "vision_native/target_selector.h"
#include "reference_selector.h"
#include "runtime_app/runtime_timing.h"
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace vision_native;
namespace vision_native { int reference_enemy_color(const unsigned char*); bool reference_friendly_color(const unsigned char*); }
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
template<class T>void read(T& x){std::cin.read(reinterpret_cast<char*>(&x),sizeof(x));if(!std::cin)throw std::runtime_error("truncated header");}
void remap(std::vector<unsigned char>& dst,const std::vector<unsigned char>& src) {
 dst=src;
 for(size_t i=0;i<src.size();i+=4){
  // Global mask remap is an explicit synthetic equivalence stress test.
  // It recolors background too: do not interpret it as a new gameplay capture.
  if(reference_friendly_color(&src[i])){dst[i]=255;dst[i+1]=255;dst[i+2]=0;}
  else if(reference_enemy_color(&src[i]))
   {dst[i]=255;dst[i+1]=0;dst[i+2]=255;}
 }
}
int main(int argc,char**argv){try{
 if(argc<2||argc>3)throw std::runtime_error("output.csv [yellow-only]; stdin frame(time_ns,u32 count,5 floats/detection,BGRA)");
 const bool yellow_only=argc==3 && std::string(argv[2])=="yellow-only";
 if(!runtime_app::pin_process_to_efficiency_cores(6))throw std::runtime_error("E-core affinity failed");
 _setmode(_fileno(stdin),_O_BINARY);
 ReferenceVisionTargetSelector old(640,512,190,true,.2f,.65f);old.set_cue_identity_enabled(true);
 VisionTargetSelector mapped(640,512,190,true,.2f,.65f),original(640,512,190,true,.2f,.65f);
 mapped.set_cue_identity_enabled(true);original.set_cue_identity_enabled(true);
 ReferenceCueDetector old_detector;CueDetector detector,raw_detector;
 std::ofstream out(argv[1]);out<<std::setprecision(9);
 out<<"frame,old_ms,mapped_ms,raw_ms,old_cue_ms,mapped_cue_ms,raw_cue_ms,old_cues,mapped_cues,raw_cues,old_target,mapped_target,raw_target,old_x,old_y,mapped_x,mapped_y,old_generation,mapped_generation,old_source,mapped_source,old_friends,mapped_friends,raw_friends,cue_geometry_diff\n";
 std::vector<unsigned char> pixels(640*512*4),changed(pixels.size());size_t f=0;
 while(std::cin.peek()!=EOF){
  DetectionBatch batch;read(batch.captured_at_ns);std::uint32_t n;read(n);if(n>10000)throw std::runtime_error("count");
  batch.frame_id=f+1;batch.frame_width=640;batch.frame_height=512;
  for(unsigned i=0;i<n;++i){Detection d;read(d.x1);read(d.y1);read(d.x2);read(d.y2);read(d.conf);batch.detections.push_back(d);}
  std::cin.read((char*)pixels.data(),pixels.size());if(!std::cin)throw std::runtime_error("truncated pixels");
  // Optional one-enemy-color control: remove legacy red UI from BOTH sides.
  // Otherwise merging two old enemy hue classes into one changes identity semantics.
  if(yellow_only)for(size_t i=0;i<pixels.size();i+=4)if(reference_enemy_color(&pixels[i])==2)
      pixels[i]=pixels[i+1]=pixels[i+2]=0;
  remap(changed,pixels);
  ReferenceVisionTargetSelector::ColorFrameView of{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  VisionTargetSelector::ColorFrameView mf{changed.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8},rf{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  VisionResult a,b,c;double t[6]{};std::vector<CueObservation> ca,cb,cc;
  for(int k=0;k<3;++k){const int i=(f+k)%3;auto start=Clock::now();
   if(i==0)a=old.select_with_frame(batch,of);
   if(i==1)b=mapped.select_with_frame(batch,mf);
   if(i==2)c=original.select_with_frame(batch,rf);
   t[i]=ms(start);
  }
  for(int k=0;k<3;++k){const int i=(f+k)%3;auto start=Clock::now();
   if(i==0){const auto& v=old_detector.detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);t[3]=ms(start);ca=v;}
   if(i==1){const auto& v=detector.detect(changed.data(),640,512,2560,PixelFormat::BGRA8,0,0);t[4]=ms(start);cb=v;}
   if(i==2){const auto& v=raw_detector.detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);t[5]=ms(start);cc=v;}
  }
  auto friends=[](const VisionResult&r){int n=0;for(const auto&d:r.detections)n+=d.is_friendly;return n;};
  bool diff=ca.size()!=cb.size();if(!diff)for(size_t i=0;i<ca.size();++i)diff|=ca[i].x!=cb[i].x||ca[i].y!=cb[i].y||ca[i].size!=cb[i].size;
  out<<f;for(double x:t)out<<','<<x;
  out<<','<<ca.size()<<','<<cb.size()<<','<<cc.size()<<','<<a.has_target<<','<<b.has_target<<','<<c.has_target<<','<<a.target_x<<','<<a.target_y<<','<<b.target_x<<','<<b.target_y<<','<<a.selector_target_generation<<','<<b.selector_target_generation<<','<<a.target_source<<','<<b.target_source<<','<<friends(a)<<','<<friends(b)<<','<<friends(c)<<','<<diff<<'\n';++f;
 }
 std::cout<<"frames="<<f<<"\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
