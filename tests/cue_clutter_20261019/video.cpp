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
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"

void check(cudaError_t e) { if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e)); }
struct Array {
    cudaArray_t data=nullptr;
    ~Array(){if(data)cudaFreeArray(data);}
};

void bridge_probe(vision_native::VisionResult result, unsigned frame, std::ostream& csv) {
 using namespace controller_native;
 namespace f=controller_native::incident_fixture;
 if(!result.has_target || std::string(result.target_source)!="cue_hold")return;
 double now=10.;auto cfg=f::base_config(40,190);cfg.ai_aim.normalized_assist_parameters=true;
 NativeGamepadController reference(cfg,&now),actual(cfg,&now);
 f::TargetSpec spec;spec.observation_id=1;spec.selector_generation=result.selector_target_generation;
 spec.body_width=result.body_x2-result.body_x1;spec.body_height=result.body_y2-result.body_y1;
 spec.has_enemy_cue=true;spec.color_classified=true;
 const float dx=result.target_x-320,dy=result.target_y-256;
 const auto manual=f::ads_input(.30f,0);
 for(int i=0;i<20;++i){now=10+i*.001;
  if(i%5==0){auto s=f::observed_snapshot(spec,i+1,now,dx,dy);reference.submit_vision_snapshot(s);actual.submit_vision_snapshot(s);}
  auto a=reference.build_output(manual),b=actual.build_output(manual);reference.observe_delivered_output(a,true,now);actual.observe_delivered_output(b,true,now);
 }
 now=10.020;result.frame_updated=true;result.frame_id=21;result.captured_at_ns=result.result_at_ns=uint64_t(now*1e9);
 auto s=runtime_app::adapt_vision_result(result);const auto count=s.candidates.size();actual.submit_vision_snapshot(s);s.candidates.clear();reference.submit_vision_snapshot(s);
 auto a=reference.build_output(manual),b=actual.build_output(manual);const auto&p=actual.last_target_plan();const auto&r=reference.last_target_plan();
 csv<<frame<<','<<double(frame)/60<<','<<count<<','<<r.cue_continuation<<','<<p.cue_continuation<<','<<p.observation_gap<<','<<r.aim_authority<<','<<p.aim_authority<<','<<a.right_x<<','<<b.right_x<<','<<p.source_aim_px.x<<','<<p.source_aim_px.y<<'\n';
 if(p.cue_continuation && (p.fire_authority||p.fire_requested))throw std::runtime_error("cue gained fire");
}

int main(int argc,char**argv){
    try{
        if(argc!=8) throw std::runtime_error("model output.jsonl fps pickup-radius target-height-ratio wide-height-ratio friendly(0|1); stdin 640x512 BGRA");
        const double fps=std::stod(argv[3]);
        if(!std::isfinite(fps)||fps<1||fps>1000)throw std::runtime_error("invalid fps");
        _setmode(_fileno(stdin),_O_BINARY);
        constexpr int w=640,h=512;
        vision_native::TensorRTEngine engine(argv[1]);
        if(engine.input_width()!=480||engine.input_height()!=384)throw std::runtime_error("Expected 480x384 model");
        vision_native::VisionTargetSelector selector(w,h,std::stof(argv[4]),std::stoi(argv[7])!=0,std::stof(argv[5]),std::stof(argv[6]));
        std::ofstream out(argv[2]);out.exceptions(std::ios::badbit|std::ios::failbit);
        out<<std::setprecision(9)<<std::boolalpha;
        std::ofstream bridge(std::string(argv[2])+".bridge.csv");bridge<<"frame,t,residual_count,reference_cue,actual_cue,gap,reference_authority,actual_authority,reference_x,actual_x,chest_x,chest_y\n";
        std::vector<uint8_t> pixels(size_t(w)*h*4),color(pixels.size());
        Array input;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&input.data,&desc,w,h));
        uint64_t frame=0;
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
            bridge_probe(result,unsigned(frame),bridge);
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
            out<<"]}\n";
            ++frame;
        }
        out.close();std::cerr<<"DONE frames="<<frame<<'\n';
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
