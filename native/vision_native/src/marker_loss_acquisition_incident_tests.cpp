#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"
#include <cmath>
#include <fstream>
#include <random>
#include <string>

namespace {
struct Counts { int triggers=0, misses=0, identity_changes=0, stale_points=0, controls_failed=0; };

// Synthetic reduction of Replay 15-40-01, not an exact capture replay: the
// live person remains directly observed while its optional yellow cue stops.
Counts run_cases(std::ofstream& out) {
    Counts total;
    out << "{\"cases\":[";
    bool first=true;
    for (unsigned seed : {20260928u, 846193u}) {
        std::mt19937 rng(seed);
        for (int c=0;c<128;++c) {
            vision_native::VisionTargetSelector selector(640,512,150,true,.35f,.65f);
            const auto step = c%2 ? 5'000'000ull : 8'333'333ull;
            const int frames = c%4<2 ? 24 : 256;
            const float phase=float(rng()%628)/100.0f;
            const float amplitude=10.0f+float(rng()%30);
            vision_native::DetectionBatch batch;
            batch.frame_width=640;batch.frame_height=512;
            batch.captured_at_ns=1'000'000'000ull;batch.frame_id=1;
            vision_native::Detection d;
            const float start_x=320+amplitude*std::sin(phase);
            const float start_y=256+amplitude*.5f*std::cos(phase);
            d.x1=start_x-20;d.x2=start_x+20;d.y1=start_y-28;d.y2=start_y+52;d.conf=.8f;
            d.color_classified=true;d.color_bonus=.3f;d.has_cue_point=true;
            d.cue_x=start_x;d.cue_y=start_y-38;d.cue_score=.95f;
            batch.detections={d};
            const auto locked=selector.select(batch);
            if(!locked.has_target || !locked.enemy_cue_current) ++total.controls_failed;
            const auto generation=locked.selector_target_generation;
            int triggers=0,misses=0,changes=0,stale=0;
            for(int f=1;f<=frames;++f){
                batch.frame_id++;batch.captured_at_ns+=step;
                const float x=320+amplitude*std::sin(phase+f*.025f);
                const float y=256+amplitude*.5f*std::cos(phase+f*.025f);
                d.x1=x-20;d.x2=x+20;d.y1=y-28;d.y2=y+52;
                d.conf=.7f+float(rng()%200)/1000.0f;
                d.color_bonus=0;d.has_cue_point=false;d.cue_score=0;
                batch.detections={d};
                // A no-observation control must never output old coordinates.
                if(f==12){auto gap=batch;gap.detections.clear();if(selector.select(gap).has_target)++total.controls_failed;}
                const auto result=selector.select(batch);
                if(step*f>50'000'000ull){
                    ++triggers;
                    if(!result.has_target || !result.aim_authority)++misses;
                    if(result.selector_target_generation!=generation)++changes;
                    if(result.has_target && (std::fabs(result.target_x-x)>.001f || std::fabs(result.target_y-y)>.001f))++stale;
                    if(result.enemy_cue_current)++total.controls_failed;
                }
            }
            // Superseding user contract: flat/corpse-like people are aimable;
            // uncertain pose evidence still cannot grant synthetic fire.
            const float flat_x=(d.x1+d.x2)*.5f;
            const float flat_y=d.y1+(d.y2-d.y1)*.35f;
            d.x1=flat_x-50;d.x2=flat_x+50;
            d.y1=flat_y-26.f;d.y2=d.y1+40;d.conf=.92f;
            batch.detections={d};batch.captured_at_ns+=step;
            const auto flat=selector.select(batch);
            if(!flat.has_target || !flat.aim_authority || flat.fire_authority)++total.controls_failed;
            // Removing all history is a counterfactual, never the proposed fix.
            selector.reset();d.x1=300;d.x2=340;d.y1=228;d.y2=308;
            batch.detections={d};selector.select(batch);batch.captured_at_ns+=step;
            if(!selector.select(batch).has_target)++total.controls_failed;
            d.is_friendly=true;batch.detections={d};batch.captured_at_ns+=step;
            if(selector.select(batch).has_target)++total.controls_failed;
            if(!first)out<<',';first=false;
            out<<"{\"seed\":"<<seed<<",\"case\":"<<c<<",\"frames\":"<<frames
               <<",\"triggers\":"<<triggers<<",\"misses\":"<<misses<<",\"identity_changes\":"<<changes<<",\"stale_points\":"<<stale<<'}';
            total.triggers+=triggers;total.misses+=misses;total.identity_changes+=changes;total.stale_points+=stale;
        }
    }
    out<<"],\"totals\":{\"cases\":256,\"triggers\":"<<total.triggers<<",\"misses\":"<<total.misses
       <<",\"identity_changes\":"<<total.identity_changes<<",\"stale_points\":"<<total.stale_points
       <<",\"controls_failed\":"<<total.controls_failed<<"}}\n";
    return total;
}
int run(const char* path){
    std::ofstream out(path);if(!out)return 3;
    const auto c=run_cases(out);
    if(c.triggers==0 || c.controls_failed)return 3;
    return c.misses || c.identity_changes || c.stale_points ? 1 : 0;
}
}
void register_marker_loss_acquisition_incident_tests(native_test::Registry& registry){
    registry.add_context_case("BaseVisionSelection","marker_loss_live_person_incident",[](const native_test::TestContext& context){
        std::filesystem::create_directories(context.artifact_directory);
        const int status=run(context.artifact_path("marker_loss_acquisition.json").string().c_str());
        if(status==3)native_test::invalid_fixture("marker-loss incident trigger/control failed");
        if(status)throw std::runtime_error("marker loss suppressed a directly observed live-person fixture");
    });
}
#ifdef MARKER_LOSS_INCIDENT_STANDALONE
int main(int argc,char** argv){return argc==2 ? run(argv[1]) : 3;}
#endif
