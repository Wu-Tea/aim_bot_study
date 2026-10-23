// Offline stage timing; never opens capture or output devices.
#include "vision_native/target_selector.h"
#include "vision_native/cue_identity.h"
#include "vision_native/tensorrt_engine.h"
#include "color_readback.h"
#include "runtime_app/runtime_timing.h"
#include <cuda_runtime.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
int main(int argc,char**argv) {try {
 if(argc!=6)throw std::runtime_error("model raw-bgra output.csv ecores rounds");
 if(std::stoi(argv[4]) && !runtime_app::pin_process_to_efficiency_cores(6))throw std::runtime_error("E-core affinity failed");
 constexpr int w=640,h=512;constexpr size_t bytes=w*h*4;
 std::ifstream input(argv[2],std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("raw missing");
 const size_t length=size_t(input.tellg());if(length%bytes)throw std::runtime_error("partial frame");
 std::vector<unsigned char> frames(length);input.seekg(0);input.read((char*)frames.data(),length);
 vision_native::TensorRTEngine engine(argv[1]);cudaArray_t array=nullptr;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&array,&desc,w,h));
 vision_native::ColorReadbackBuffer host(true);if(!host.ensure(bytes))throw std::runtime_error("host buffer");
 std::vector<vision_native::DetectionBatch> batches(length/bytes);
 std::vector<double> inference(batches.size()),gpu(batches.size()),copy(batches.size());
 // Inference and readback measured once, then CPU variants use identical raw detections.
 for(size_t f=0;f<batches.size();++f) {
  check(cudaMemcpy2DToArray(array,0,0,frames.data()+f*bytes,w*4,w*4,h,cudaMemcpyHostToDevice));
  auto start=Clock::now();batches[f]=engine.infer_bgra_array(array,w,h,.40f);inference[f]=ms(start);gpu[f]=batches[f].infer_ms;
  start=Clock::now();check(cudaMemcpy2DFromArrayAsync(host.data(),w*4,array,0,0,w*4,h,cudaMemcpyDeviceToHost,engine.cuda_stream()));check(cudaStreamSynchronize(engine.cuda_stream()));copy[f]=ms(start);
  batches[f].frame_width=w;batches[f].frame_height=h;batches[f].captured_at_ns=1'000'000'000ull+f*8'333'333ull;
 }
 check(cudaFreeArray(array));
 std::ofstream out(argv[3]);out<<"round,mode,frame,inference_ms,gpu_infer_ms,readback_ms,selector_ms,cue_ms,roi_ratio,target,source,x,y,cues\n";
 for(int round=0;round<std::stoi(argv[5]);++round)for(int order=0;order<2;++order) {
  const bool enabled=(order+round)%2==0;
  vision_native::VisionTargetSelector selector(w,h,160,true,.35f,.65f);selector.set_cue_identity_enabled(enabled);
  vision_native::CueDetector detector;
  for(size_t f=0;f<batches.size();++f) {
   auto* pixels=frames.data()+f*bytes;auto region=selector.required_color_region(batches[f]);
   auto start=Clock::now();auto r=selector.select_with_frame(batches[f],{pixels,w,h,w*4,0,0,w,h,vision_native::PixelFormat::BGRA8});double select_ms=ms(start);
   start=Clock::now();size_t cues=detector.detect(pixels,w,h,w*4,vision_native::PixelFormat::BGRA8,0,0).size();double cue_ms=ms(start);
   double ratio=region?double((region->right-region->left)*(region->bottom-region->top))/(w*h):0;
   out<<round<<','<<(enabled?"cue_on":"legacy")<<','<<f<<','<<inference[f]<<','<<gpu[f]<<','<<copy[f]<<','<<select_ms<<','<<cue_ms<<','<<ratio<<','<<r.has_target<<','<<r.target_source<<','<<r.target_x<<','<<r.target_y<<','<<cues<<'\n';
  }
 }
 std::cout<<"frames="<<batches.size()<<" rounds="<<argv[5]<<" readback="<<vision_native::color_readback_mode_name(host.mode())<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
