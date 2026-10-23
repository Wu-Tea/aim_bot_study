#include "vision_native/target_selector.h"
#include "reference_selector.h"
#include "vision_native/tensorrt_engine.h"
#include "runtime_app/runtime_timing.h"
#include "record.h"
#include <cuda_runtime.h>
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <memory>
#include <array>
using namespace vision_native;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
bool equal(const CueObservation&a,const CueObservation&b){return a.x==b.x&&a.y==b.y&&a.size==b.size&&a.score==b.score&&a.color==b.color;}
std::string record(VisionResult r,bool selected_only=false){if(selected_only)r.detections.clear();std::ostringstream s;study::write(s,r);return s.str();}
int main(int argc,char**argv){try {
 if(argc!=4)throw std::runtime_error("model timings.csv fps; stdin BGRA 640x512");
 if(!runtime_app::pin_process_to_efficiency_cores(6))throw std::runtime_error("E-core affinity failed");
 _setmode(_fileno(stdin),_O_BINARY);
 TensorRTEngine engine(argv[1]);cudaArray_t array=nullptr;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&array,&desc,640,512));
 constexpr std::array<int,5> limits{512,448,384,320,288};
 VisionTargetSelector full(640,512,160,true,.35f,.65f);full.set_cue_identity_enabled(true);
 std::array<std::unique_ptr<ReferenceVisionTargetSelector>,5> selectors;
 std::array<ReferenceCueDetector,5> detectors;
 for(auto&s:selectors){s=std::make_unique<ReferenceVisionTargetSelector>(640,512,160.f,true,.35f,.65f);s->set_cue_identity_enabled(true);}
 CueDetector detector;
 std::ofstream out(argv[2]),positions(std::string(argv[2])+".positions.csv"),diffs(std::string(argv[2])+".diffs.txt");
 out<<"frame,bottom,full_cue_ms,region_cue_ms,full_selector_ms,region_selector_ms,full_cues,region_cues,removed,added,record_diff,selected_diff,full_target,region_target,full_generation,region_generation,full_motion_y,region_motion_y\n";
 positions<<"frame,kind,x,y,size\n";
 std::vector<unsigned char> pixels(640*512*4);size_t f=0,cues=0;const double fps=std::stod(argv[3]);
 while(std::cin.read((char*)pixels.data(),pixels.size())) {
  check(cudaMemcpy2DToArray(array,0,0,pixels.data(),2560,2560,512,cudaMemcpyHostToDevice));
  auto batch=engine.infer_bgra_array(array,640,512,.40f);batch.frame_id=f+1;batch.captured_at_ns=1'000'000'000ull+std::uint64_t(f*1e9/fps);
  VisionTargetSelector::ColorFrameView frame{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  ReferenceVisionTargetSelector::ColorFrameView rf{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  VisionResult base;std::array<VisionResult,5> results;
  std::vector<CueObservation> base_cues;std::array<std::vector<CueObservation>,5> rc;
  double base_ms=0,base_cue_ms=0;std::array<double,5> times{},cue_times{};
  // Rotate all six variants through every measurement slot, independently
  // preserving each selector's state across all video frames.
  for(int k=0;k<6;++k){int i=int((f+k)%6);auto start=Clock::now();
   if(i==5){base=full.select_with_frame(batch,frame);base_ms=ms(start);}
   else{region_seed_bottom=limits[i];results[i]=selectors[i]->select_with_frame(batch,rf);times[i]=ms(start);}
  }
  for(int k=0;k<6;++k){int i=int((f+k)%6);region_seed_bottom=i<5?limits[i]:512;auto start=Clock::now();
   if(i==5){const auto& v=detector.detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);base_cue_ms=ms(start);base_cues=v;}
   else{const auto& v=detectors[i].detect(pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);cue_times[i]=ms(start);rc[i]=v;}
  }
  for(auto&c:base_cues)positions<<f<<",candidate,"<<c.x<<','<<c.y<<','<<c.size<<'\n';
  if(base.has_target&&base.motion_cue_id)positions<<f<<",selected_motion,"<<base.motion_cue_x<<','<<base.motion_cue_y<<",0\n";
  for(int i=0;i<5;++i){size_t removed=0,added=0;
   for(auto&c:base_cues){bool found=false;for(auto&d:rc[i])found|=equal(c,d);removed+=!found;}
   for(auto&c:rc[i]){bool found=false;for(auto&d:base_cues)found|=equal(c,d);added+=!found;}
   const bool changed=record(base)!=record(results[i]),selected=record(base,true)!=record(results[i],true);
   if(i==0&&(removed||added||changed))throw std::runtime_error("full-height frozen control differs at "+std::to_string(f));
   if(selected)diffs<<"frame="<<f<<" bottom="<<limits[i]<<"\nbase "<<record(base,true)<<"region "<<record(results[i],true);
   out<<f<<','<<limits[i]<<','<<base_cue_ms<<','<<cue_times[i]<<','<<base_ms<<','<<times[i]<<','<<base_cues.size()<<','<<rc[i].size()<<','<<removed<<','<<added<<','<<changed<<','<<selected<<','<<base.has_target<<','<<results[i].has_target<<','<<base.selector_target_generation<<','<<results[i].selector_target_generation<<','<<(base.motion_cue_id?base.motion_cue_y:-1)<<','<<(results[i].motion_cue_id?results[i].motion_cue_y:-1)<<'\n';
  }
  ++f;cues+=base_cues.size();
 }
 if(std::cin.gcount())throw std::runtime_error("partial input frame");
 check(cudaFreeArray(array));std::cout<<"frames="<<f<<" cues="<<cues<<" full-height control identical; region differences recorded\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
