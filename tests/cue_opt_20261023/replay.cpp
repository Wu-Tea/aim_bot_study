#include "vision_native/target_selector.h"
#include "reference_selector.h"
#include "vision_native/tensorrt_engine.h"
#include "runtime_app/runtime_timing.h"
#include "record.h"
#include <cuda_runtime.h>
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
#include <sstream>
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
int main(int argc,char**argv){try {
 if(argc!=4)throw std::runtime_error("model timings.csv fps; stdin BGRA 640x512");
 if(!runtime_app::pin_process_to_efficiency_cores(6))throw std::runtime_error("E-core affinity failed");
 _setmode(_fileno(stdin),_O_BINARY);
 using namespace vision_native;
 TensorRTEngine engine(argv[1]);cudaArray_t array=nullptr;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&array,&desc,640,512));
 VisionTargetSelector current(640,512,160,true,.35f,.65f);current.set_cue_identity_enabled(true);
 ReferenceVisionTargetSelector baseline(640,512,160,true,.35f,.65f);baseline.set_cue_identity_enabled(true);
 CueDetector cue;ReferenceCueDetector old_cue;
 std::ofstream out(argv[2]);out<<"frame,before_cue_ms,after_cue_ms,before_selector_ms,after_selector_ms,cues,target,generation,source\n";
 std::vector<unsigned char> pixels(640*512*4);size_t f=0,targets=0,cues_total=0;
 while(std::cin.read((char*)pixels.data(),pixels.size())) {
  check(cudaMemcpy2DToArray(array,0,0,pixels.data(),2560,2560,512,cudaMemcpyHostToDevice));
  auto batch=engine.infer_bgra_array(array,640,512,.40f);batch.frame_id=f+1;batch.captured_at_ns=1'000'000'000ull+std::uint64_t(f*1e9/std::stod(argv[3]));
  VisionTargetSelector::ColorFrameView frame{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  ReferenceVisionTargetSelector::ColorFrameView old_frame{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  VisionResult a,b;double old_ms,new_ms,old_cue_ms,new_cue_ms;const std::vector<CueObservation>* ca=nullptr,*cb=nullptr;
  // Alternate measurement order to avoid always assigning the warm cache to one side.
  for(int k=0;k<2;++k) {
   auto start=Clock::now();
   if((f+k)%2==0){a=current.select_with_frame(batch,frame);new_ms=ms(start);}else{b=baseline.select_with_frame(batch,old_frame);old_ms=ms(start);}
  }
  for(int k=0;k<2;++k) {
   auto start=Clock::now();
   if((f+k)%2==0){ca=&cue.detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);new_cue_ms=ms(start);}else{cb=&old_cue.detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);old_cue_ms=ms(start);}
  }
  if(ca->size()!=cb->size())throw std::runtime_error("cue count frame "+std::to_string(f));
  for(size_t i=0;i<ca->size();++i){const auto& x=(*ca)[i];const auto& y=(*cb)[i];if(x.x!=y.x||x.y!=y.y||x.size!=y.size||x.score!=y.score||x.color!=y.color)throw std::runtime_error("cue value/order frame "+std::to_string(f));}
  std::ostringstream sa,sb;study::write(sa,a);study::write(sb,b);
  if(sa.str()!=sb.str())throw std::runtime_error("selector result frame "+std::to_string(f));
  out<<f<<','<<old_cue_ms<<','<<new_cue_ms<<','<<old_ms<<','<<new_ms<<','<<ca->size()<<','<<a.has_target<<','<<a.selector_target_generation<<','<<a.target_source<<'\n';
  ++f;targets+=a.has_target;cues_total+=ca->size();
 }
 if(std::cin.gcount())throw std::runtime_error("partial input frame");
 check(cudaFreeArray(array));std::cout<<"frames="<<f<<" targets="<<targets<<" cues="<<cues_total<<" all cue and selector results identical\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
