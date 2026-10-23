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
using namespace vision_native;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
std::string record(const VisionResult&r){std::ostringstream s;study::write(s,r);return s.str();}
int main(int argc,char**argv){try {
 if(argc!=4)throw std::runtime_error("model timings.csv fps; stdin BGRA 640x512");
 if(!runtime_app::pin_process_to_efficiency_cores(6))throw std::runtime_error("E-core affinity failed");
 _setmode(_fileno(stdin),_O_BINARY);
 TensorRTEngine engine(argv[1]);cudaArray_t array=nullptr;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&array,&desc,640,512));
 VisionTargetSelector native(640,512,160,true,.35f,.65f);native.set_cue_identity_enabled(true);
 ReferenceVisionTargetSelector full(640,512,160,true,.35f,.65f),greedy(640,512,160,true,.35f,.65f);
 full.set_cue_identity_enabled(true);greedy.set_cue_identity_enabled(true);
 std::ofstream out(argv[2]),diffs(std::string(argv[2])+".diffs.txt");
 out<<"frame,full_cue_ms,greedy_cue_ms,full_selector_ms,greedy_selector_ms,full_cues,greedy_cues,record_diff,target,generation,native_selector_ms\n";
 std::vector<unsigned char> pixels(640*512*4);size_t f=0,different=0;const double fps=std::stod(argv[3]);
 while(std::cin.read((char*)pixels.data(),pixels.size())) {
  check(cudaMemcpy2DToArray(array,0,0,pixels.data(),2560,2560,512,cudaMemcpyHostToDevice));
  auto batch=engine.infer_bgra_array(array,640,512,.40f);batch.frame_id=f+1;batch.captured_at_ns=1'000'000'000ull+std::uint64_t(f*1e9/fps);
  VisionTargetSelector::ColorFrameView frame{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  ReferenceVisionTargetSelector::ColorFrameView rf{pixels.data(),640,512,2560,0,0,640,512,PixelFormat::BGRA8};
  VisionResult a,b,c;double full_ms=0,greedy_ms=0,native_ms=0,full_cue=0,greedy_cue=0;std::size_t full_count=0,greedy_count=0;
  for(int k=0;k<3;++k){int i=int((f+k)%3);region_relevance_enabled=i==1;auto start=Clock::now();
   if(i==0){a=full.select_with_frame(batch,rf);full_ms=ms(start);full_cue=region_cue_ms;full_count=region_cue_count;}
   if(i==1){b=greedy.select_with_frame(batch,rf);greedy_ms=ms(start);greedy_cue=region_cue_ms;greedy_count=region_cue_count;}
   if(i==2){c=native.select_with_frame(batch,frame);native_ms=ms(start);}
  }
  if(record(a)!=record(c))throw std::runtime_error("full search control differs at "+std::to_string(f));
  const bool changed=record(a)!=record(b);different+=changed;
  if(changed)diffs<<"frame="<<f<<"\nfull "<<record(a)<<"greedy "<<record(b);
  out<<f<<','<<full_cue<<','<<greedy_cue<<','<<full_ms<<','<<greedy_ms<<','<<full_count<<','<<greedy_count<<','<<changed<<','<<a.has_target<<','<<a.selector_target_generation<<','<<native_ms<<'\n';
  ++f;
 }
 if(std::cin.gcount())throw std::runtime_error("partial input frame");
 check(cudaFreeArray(array));std::cout<<"frames="<<f<<" selector_differences="<<different<<" full search control identical\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
