// Recorded model detections + original pixels; no GPU, controller or hardware output.
#include "vision_native/target_selector.h"
#include "runtime_app/runtime_timing.h"
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iomanip>
#include <iostream>
template<class T> void read(T& x) {
    std::cin.read(reinterpret_cast<char*>(&x),sizeof(x));
    if(!std::cin) throw std::runtime_error("truncated input");
}
int main(int argc,char** argv) { try {
    if(argc!=2) throw std::runtime_error("output.csv; stdin ns,u32 count,5 floats/box,640x512 BGRA");
    _setmode(_fileno(stdin),_O_BINARY);
    if(!runtime_app::pin_process_to_efficiency_cores(6)) throw std::runtime_error("affinity");
    vision_native::VisionTargetSelector selector(640,512,190,true,.2f,.65f);
    selector.set_cue_identity_enabled(true);
    std::vector<unsigned char> pixels(640*512*4);
    std::ofstream out(argv[1]);out<<std::setprecision(9);
    out<<"frame,ms,target,x,y,generation,source,selected,fire,body_top,body_bottom,reason\n";
    unsigned frame=0;
    while(std::cin.peek()!=EOF) {
        vision_native::DetectionBatch b;read(b.captured_at_ns);unsigned n;read(n);
        if(n>1000)throw std::runtime_error("count");
        b.frame_id=frame+1;b.frame_width=640;b.frame_height=512;
        for(unsigned i=0;i<n;++i) {vision_native::Detection d;
            read(d.x1);read(d.y1);read(d.x2);read(d.y2);read(d.conf);b.detections.push_back(d);}
        std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size());
        if(!std::cin)throw std::runtime_error("pixels");
        auto start=std::chrono::steady_clock::now();
        auto r=selector.select_with_frame(b,{pixels.data(),640,512,2560,0,0,640,512,vision_native::PixelFormat::BGRA8});
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        out<<frame++<<','<<ms<<','<<r.has_target<<','<<r.target_x<<','<<r.target_y<<','<<r.selector_target_generation
            <<','<<r.target_source<<','<<(r.has_selected_detection?int(r.selected_detection_index):-1)<<','<<r.fire_authority
            <<','<<r.body_y1<<','<<r.body_y2<<','<<r.selection_diagnostics.cue_reason<<'\n';
    }
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
