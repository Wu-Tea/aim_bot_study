// Diagnostic reproduction, not a production fix or a passing quality gate.
#include "vision_native/target_selector.h"
#include <algorithm>
#include <iostream>
#include <vector>
using namespace vision_native;
int main() {
    VisionTargetSelector selector(640,512,190,true,.2f,.65f);
    selector.set_cue_identity_enabled(true);
    std::vector<unsigned char> pixels(640*512*4,0);
    for(int dy=-5;dy<=5;++dy)for(int dx=-5;dx<=5;++dx)
        if(dx*dx+dy*dy>=9 && dx*dx+dy*dy<=25) {
            auto* p=&pixels[((230+dy)*640+300+dx)*4];p[0]=p[2]=255;
        }
    DetectionBatch batch;batch.captured_at_ns=1000000000ull;
    auto run=[&](float top) {
        batch.captured_at_ns+=10000000;
        Detection d;d.x1=275;d.x2=325;d.y1=top;d.y2=362;
        d.conf=.9f;d.color_classified=true;batch.detections={d};
        return selector.select_with_frame(batch,{pixels.data(),640,512,640*4,0,0,640,512,PixelFormat::BGRA8});
    };
    const auto initial=run(242);
    std::cout<<"stage,box_top,aim_y,source,generation,cue_reason\n";
    auto write=[&](const char* stage,float top,const VisionResult& r) {
        std::cout<<stage<<','<<top<<','<<r.target_y<<','<<r.target_source<<','
            <<r.selector_target_generation<<','<<r.selection_diagnostics.cue_reason<<'\n';
    };
    write("initial",242,initial);
    for(int top=241;top>=205;--top)write("gradual_top_growth",float(top),run(float(top)));
    for(int i=0;i<20;++i)write("original_body_restored",242,run(242));
}
