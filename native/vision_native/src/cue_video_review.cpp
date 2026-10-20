// Offline diagnostic only: BGRA frames on stdin, existing GPU preprocessing and
// selector. No capture, controller, output device, configuration write or tuning.
#include "vision_native/tensorrt_engine.h"
#include "vision_native/target_selector.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <io.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(cudaError_t e) { if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e)); }
struct Array {
    cudaArray_t data=nullptr;
    ~Array(){if(data)cudaFreeArray(data);}
};
int main(int argc,char**argv){
    try{
        if(argc!=8 && argc!=9) throw std::runtime_error("model output.jsonl fps pickup-radius target-height-ratio wide-height-ratio friendly(0|1) [cue-owned]; stdin 640x512 BGRA");
        const double fps=std::stod(argv[3]);
        if(!std::isfinite(fps)||fps<1||fps>1000)throw std::runtime_error("invalid fps");
        _setmode(_fileno(stdin),_O_BINARY);
        constexpr int w=640,h=512;
        vision_native::TensorRTEngine engine(argv[1]);
        if(engine.input_width()!=480||engine.input_height()!=384)throw std::runtime_error("Expected 480x384 model");
        vision_native::VisionTargetSelector selector(w,h,std::stof(argv[4]),std::stoi(argv[7])!=0,std::stof(argv[5]),std::stof(argv[6]));
        if(argc==9) {
            if(std::string(argv[8])!="cue-owned") throw std::runtime_error("unknown selection policy");
            selector.set_cue_identity_enabled(true);
        }
        std::ofstream out(argv[2]);out.exceptions(std::ios::badbit|std::ios::failbit);
        out<<std::setprecision(9)<<std::boolalpha;
        std::vector<uint8_t> pixels(size_t(w)*h*4),color(pixels.size());
        Array input;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&input.data,&desc,w,h));
        uint64_t frame=0;
        vision_native::CueDetector independent_detector;
        while(true){
            std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size());
            if(std::cin.gcount()==0 && std::cin.eof())break;
            if(size_t(std::cin.gcount())!=pixels.size())throw std::runtime_error("Truncated input frame");
            check(cudaMemcpy2DToArray(input.data,0,0,pixels.data(),w*4,w*4,h,cudaMemcpyHostToDevice));
            auto batch=engine.infer_bgra_array(input.data,w,h,.20f);
            batch.frame_id=frame+1;
            batch.captured_at_ns=1'000'000'000ull+uint64_t(std::llround(frame*1e9/fps));
            const auto region=selector.required_color_region(batch);
            vision_native::VisionResult result;
            // Same cropped ColorFrameView boundary as VisionEngine. Decode/upload is
            // offline transport, not an end-to-end performance measurement.
            if(region){
                const int rw=region->right-region->left,rh=region->bottom-region->top;
                check(cudaMemcpy2DFromArrayAsync(color.data(),rw*4,input.data,region->left*4,region->top,rw*4,rh,cudaMemcpyDeviceToHost,engine.cuda_stream()));
                check(cudaStreamSynchronize(engine.cuda_stream()));
                result=selector.select_with_frame(batch,{color.data(),rw,rh,rw*4,region->left,region->top,w,h,vision_native::PixelFormat::BGRA8});
            }else result=selector.select(batch);
            const auto& diag=result.selection_diagnostics;
            out<<"{\"frame\":"<<frame<<",\"t\":"<<frame/fps
                <<",\"has_target\":"<<result.has_target<<",\"source\":\""<<result.target_source
                <<"\",\"aim_authority\":"<<result.aim_authority<<",\"fire_authority\":"<<result.fire_authority
                <<",\"target\":["<<result.target_x<<','<<result.target_y<<"],\"generation\":"<<result.selector_target_generation
                <<",\"selected\":"<<(result.has_selected_detection?int(result.selected_detection_index):-1)
                <<",\"cue_reason\":\""<<diag.cue_reason<<"\",\"cue_best\":"<<diag.cue_best_score
                <<",\"cue_direct_age_ms\":"<<diag.cue_direct_age_ms<<",\"cue_age_ms\":"<<diag.cue_age_ms
                <<",\"outcome\":\""<<diag.outcome<<"\",\"region\":";
            if(region)out<<'['<<region->left<<','<<region->top<<','<<region->right<<','<<region->bottom<<']';else out<<"null";
            out<<",\"detections\":[";
            for(size_t i=0;i<result.detections.size();++i){
                if(i)out<<',';
                const auto& d=result.detections[i];
                out<<"{\"box\":["<<d.x1<<','<<d.y1<<','<<d.x2<<','<<d.y2<<"],\"conf\":"<<d.conf
                    <<",\"class\":"<<d.class_id<<",\"color_checked\":"<<d.color_classified
                    <<",\"friendly\":"<<d.is_friendly<<",\"has_cue\":"<<d.has_cue_point
                    <<",\"cue\":["<<d.cue_x<<','<<d.cue_y<<"],\"cue_score\":"<<d.cue_score
                    <<",\"motion_anchor\":"<<d.has_motion_anchor<<",\"motion_score\":"<<d.motion_anchor_score;
                if(i<diag.count)out<<",\"gate\":\""<<diag.boxes[i].gate<<'"';
                out<<'}';
            }
            out<<"],\"independent_cues\":[";
            if(argc==9) {
                const auto& cues=independent_detector.detect(pixels.data(),w,h,w*4,vision_native::PixelFormat::BGRA8,0,0);
                for(size_t i=0;i<cues.size();++i) {
                    if(i)out<<',';
                    const auto& cue=cues[i];
                    out<<"{\"xy\":["<<cue.x<<','<<cue.y<<"],\"color\":"<<cue.color<<",\"score\":"<<cue.score<<'}';
                }
            }
            out<<"]}\n";
            ++frame;
        }
        out.close();std::cerr<<"DONE frames="<<frame<<'\n';
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
