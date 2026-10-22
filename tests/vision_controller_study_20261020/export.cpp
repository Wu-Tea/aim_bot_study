// Diagnostic-only image replay. No capture device or physical output backend.
#include "record.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/target_selector.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <io.h>
#include <cmath>
#include <iostream>
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
struct Array{cudaArray_t data=nullptr;~Array(){if(data)cudaFreeArray(data);}};
int main(int argc,char** argv){try{
    if(argc<4||argc>6)throw std::runtime_error("model output.records fps [height-ratio] [pickup-radius]; stdin 640x512 BGRA");
    const double fps=std::stod(argv[3]);if(fps<1||fps>1000)throw std::runtime_error("fps");
    _setmode(_fileno(stdin),_O_BINARY);
    vision_native::TensorRTEngine engine(argv[1]);
    if(engine.input_width()!=480||engine.input_height()!=384)throw std::runtime_error("model shape");
    const float height_ratio=argc>4?std::stof(argv[4]):.2f;
    const float pickup_radius=argc>5?std::stof(argv[5]):190.f;
    vision_native::VisionTargetSelector selector(640,512,pickup_radius,true,height_ratio,.65f);selector.set_cue_identity_enabled(true);
    Array input;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&input.data,&desc,640,512));
    std::vector<unsigned char> pixels(640*512*4);
    std::ofstream out(argv[2]);out.exceptions(std::ios::failbit|std::ios::badbit);
    std::uint64_t frame=0,targets=0,cues=0;
    vision_native::CueDetector diagnostic_cues;
    std::ofstream cue_log(std::string(argv[2])+".cues.csv");cue_log<<"frame,x,y,size,score\n";
    while(true){
        std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size());
        if(!std::cin.gcount()&&std::cin.eof())break;
        if(std::size_t(std::cin.gcount())!=pixels.size())throw std::runtime_error("partial frame");
        check(cudaMemcpy2DToArray(input.data,0,0,pixels.data(),2560,2560,512,cudaMemcpyHostToDevice));
        auto batch=engine.infer_bgra_array(input.data,640,512,.20f);
        batch.frame_id=frame+1;batch.captured_at_ns=1'000'000'000ull+std::uint64_t(std::llround(frame*1e9/fps));
        auto r=selector.select_with_frame(batch,{pixels.data(),640,512,2560,0,0,640,512,vision_native::PixelFormat::BGRA8});
        for(const auto& cue:diagnostic_cues.detect(pixels.data(),640,512,2560,vision_native::PixelFormat::BGRA8,0,0))
            cue_log<<frame+1<<','<<cue.x<<','<<cue.y<<','<<cue.size<<','<<cue.score<<'\n';
        r.frame_id=batch.frame_id;r.captured_at_ns=batch.captured_at_ns;
        study::write(out,r);targets+=r.has_target;cues+=study::source_id(r.target_source)==2;++frame;
    }
    std::cerr<<"frames="<<frame<<" targets="<<targets<<" cues="<<cues<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
