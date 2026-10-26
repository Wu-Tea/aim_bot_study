#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
#include "cue_diamond_video_fixture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>

namespace {
using namespace vision_native;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void sparse_cue_seed_rows_preserve_small_markers() {
    CueDetector detector;
    // Move a five-pixel marker through every phase of the four-row seed grid,
    // then place it beside a rejected large same-color component.
    for (int cx:{40,41}) for (int cy=8;cy<20;++cy) for (bool clutter:{false,true}) {
        std::vector<unsigned char> pixels(64*48*4,0);
        const auto paint=[&](int x,int y) {
            auto* p=&pixels[(y*64+x)*4];p[2]=255;p[0]=255;
        };
        if (clutter) for(int y=1;y<47;++y)for(int x=1;x<23;++x)paint(x,y);
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
            if(dx*dx+dy*dy>=1 && dx*dx+dy*dy<=4)paint(cx+dx,cy+dy);
        const auto& cues=detector.detect(pixels.data(),64,48,64*4,PixelFormat::BGRA8,0,0);
        require(cues.size()==1,"sparse seed rows must preserve every small-marker phase without fragmenting large clutter");
        require(cues[0].x==cx && cues[0].y==cy && cues[0].size==5,
            "seed search must reconstruct the complete marker at original resolution");
    }
}
struct Scene {
    std::vector<unsigned char> pixels=std::vector<unsigned char>(640*512*4,0);
    DetectionBatch batch;
    Scene() { batch.captured_at_ns=1'000'000'000ull; }
    void clear() { std::fill(pixels.begin(),pixels.end(),0); batch.detections.clear(); batch.captured_at_ns+=10'000'000; }
    void cue(int cx,int cy,bool dim=false) {
        for (int y=-5;y<=5;++y) for (int x=-5;x<=5;++x) {
            const int rr=x*x+y*y;
            if (rr<9 || rr>25) continue;
            auto* p=&pixels[((cy+y)*640+cx+x)*4]; p[2]=p[0]=dim ? 160 : 255;
        }
    }
    void person(float x,float y,float conf=.8f) {
        Detection d; d.x1=x-25; d.x2=x+25; d.y1=y+12; d.y2=y+132; d.conf=conf;
        // Color decoding is tested separately; avoid friendly classification
        // changing these controlled body association fixtures.
        d.color_classified=true;
        batch.detections.push_back(d);
    }
    VisionResult run(VisionTargetSelector& selector, pipeline_contract::UserAimIntent intent={}) {
        return selector.select_with_frame(batch,{pixels.data(),640,512,640*4,0,0,640,512,PixelFormat::BGRA8},intent);
    }
};
VisionTargetSelector selector() {
    VisionTargetSelector s(640,512,190,true,.2f,.65f); s.set_cue_identity_enabled(true); return s;
}
void cue_owns_person_churn() {
    auto s=selector(); Scene f;
    f.cue(300,230); f.cue(400,230); f.person(300,230); f.person(400,230);
    require(!f.run(s).has_target,"multiple bodies retain original acquisition confirmation");
    f.batch.captured_at_ns+=10'000'000;
    const auto first=f.run(s);
    require(first.has_target && first.target_x==300,"initial cue/body acquisition");
    const auto generation=first.selector_target_generation;
    for (int i=0;i<80;++i) {
        f.clear(); f.cue(270,230); f.cue(340,230); f.person(340,230,.99f);
        if (i%5==0) { f.person(270,230,.5f); f.person(270,230,.95f); }
        if (i%2) std::reverse(f.batch.detections.begin(),f.batch.detections.end());
        const auto r=f.run(s);
        require(r.selector_target_generation==generation && !r.selector_target_changed,"person churn must not create identity");
        require(r.has_target && r.target_x==270,"closer, stronger neighbour must not steal cue");
        if (i%5) require(!r.has_selected_detection && !r.auto_fire && r.fire_authority==0,"cue continuation cannot fire");
    }
}
void cue_requires_body_geometry() {
    auto s=selector(); Scene f; f.cue(300,230);
    auto r=f.run(s);
    require(!r.has_target && !r.has_body_box && r.aim_authority==0 && r.selector_target_generation==0,"unpaired cue must not invent a controlled body identity");
    f.clear(); f.cue(300,230); f.person(300,230); r=f.run(s);
    require(r.has_target && r.selector_target_generation==1,"first person attaches to cue generation");
    for (int i=0;i<25;++i) {
        f.clear(); f.cue(300,230); r=f.run(s);
        require(r.selector_target_generation==1,"fresh cue retains identity beyond anatomy age");
        if (i>=18) require(r.has_target && r.aim_authority && !r.fire_authority,"continuous fresh cue renews motion beyond 180 ms without fire");
    }
    f.clear(); f.cue(300,230); f.person(300,230); r=f.run(s);
    require(r.has_target && r.selector_target_generation==1,"returning body cannot replace cue identity");
}
void cue_loss_and_expiry() {
    auto s=selector(); Scene f; f.cue(280,230); f.person(280,230); auto r=f.run(s);
    require(r.has_target,"seed");
    for (int i=0;i<5;++i) {
        f.clear(); f.cue(360,230); f.person(360,230); r=f.run(s);
        require(!r.has_target && r.selector_target_generation==1,"missing cue has no fresh output or neighbour switch");
    }
    for (int i=0;i<3;++i) { f.clear(); f.cue(360,230); f.person(360,230); r=f.run(s); }
    require(r.has_target && r.selector_target_generation==2,"expired cue permits new acquisition");
    f.batch.captured_at_ns-=100'000'000; r=f.run(s);
    require(!r.has_target,"backward timestamps cannot refresh geometry");
}
void cue_frame_and_reset() {
    auto s=selector(); Scene f; f.cue(300,230); f.person(300,230); auto r=f.run(s);
    auto region=s.required_color_region({});
    require(region && region->left==0 && region->right==640 && region->bottom==512,"independent acquisition reads capture without persons");
    f.batch.captured_at_ns+=10'000'000;
    r=s.select(f.batch);
    require(r.has_target && r.has_selected_detection,"credible person remains independent of color-frame availability");
    r=s.select_with_frame(f.batch,{f.pixels.data(),320,512,640*4,0,0,640,512,PixelFormat::BGRA8});
    require(!r.has_target,"partial frame cannot establish independent cue");
    s.reset(); f.clear(); f.person(300,230); r=f.run(s);
    require(!r.has_target && r.selector_target_generation==0,"request reset releases old cue and body");
}
void cue_handover_and_correction() {
    auto s=selector(); Scene f; f.cue(280,230); f.cue(400,230); f.person(280,230); f.person(400,230);
    f.run(s); f.batch.captured_at_ns+=10'000'000;
    auto r=f.run(s); require(r.has_target && r.target_x==280,"seed left cue");
    pipeline_contract::UserAimIntent intent; intent.valid=intent.aiming=intent.has_direction=true;
    intent.strength=1; intent.direction={1,0}; intent.intent_id=9;
    intent.purpose=pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    f.batch.captured_at_ns+=10'000'000; r=f.run(s,intent);
    require(r.has_target && r.target_x==280 && r.selector_target_generation==1,"correction does not change anatomical reference or cue");
    intent.purpose=pipeline_contract::UserAimIntentPurpose::HandoverTarget;
    f.batch.captured_at_ns+=10'000'000; r=f.run(s,intent);
    require(r.has_target && r.target_x==400 && r.selector_target_generation==2,"explicit handover remains available");
    f.batch.captured_at_ns+=10'000'000; r=f.run(s,intent);
    require(r.target_x==400 && r.selector_target_generation==2,"held handover direction cannot oscillate between cues");
}
void cue_colors_and_formats() {
    Scene f; f.cue(280,230); f.cue(380,230,true);
    CueDetector detector;
    auto cues=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);
    require(cues.size()==2 && cues[0].color==1 && cues[1].color==1,"bright/dim magenta circle candidates");
    std::vector<unsigned char> rgb(640*512*3);
    for (int i=0;i<640*512;++i) { rgb[i*3]=f.pixels[i*4+2]; rgb[i*3+1]=f.pixels[i*4+1]; rgb[i*3+2]=f.pixels[i*4]; }
    cues=detector.detect(rgb.data(),640,512,1920,PixelFormat::RGB8,10,20);
    require(cues.size()==2 && cues[0].x==290 && cues[0].y==250,"RGB and origin mapping");
    f.clear();
    for (int y=220;y<225;++y) for (int x=280;x<330;++x) { f.pixels[(y*640+x)*4]=255; f.pixels[(y*640+x)*4+2]=255; }
    require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).empty(),"blood bar excluded");
    require(detector.detect(f.pixels.data(),640,512,100,PixelFormat::BGRA8,0,0).empty(),"invalid pitch rejected");
}
void recorded_small_diamond_cue() {
    // Recorded diamond geometry, explicitly recolored from yellow to magenta.
    // Preserve saturation/value and all non-marker pixels; not a new live sample.
    for (const auto& original : cue_diamond_fixture::rgb) for (int phase=0;phase<4;++phase) {
        std::vector<unsigned char> patch(original, original+24*24*3);
        for (int i=0;i<24*24;++i) {
            auto* p=&patch[i*3];
            const float hi=std::max({p[0],p[1],p[2]}),lo=std::min({p[0],p[1],p[2]}),d=hi-lo;
            if (d==0 || hi<120 || 255*d<120*hi) continue;
            float h=30*(hi==p[0] ? (p[1]-p[2])/d : hi==p[1] ? 2+(p[2]-p[0])/d : 4+(p[0]-p[1])/d);
            if (h<0) h+=180;
            if(h>=20 && h<=35){p[0]=p[2]=static_cast<unsigned char>(hi);p[1]=static_cast<unsigned char>(lo);}
        }
        Scene f; const int left=288,top=212+phase;
        for(int y=0;y<24;++y)for(int x=0;x<24;++x) {
            auto* dst=&f.pixels[((top+y)*640+left+x)*4];
            const auto* src=&patch[(y*24+x)*3];
            dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];
        }
        CueDetector detector;
        const auto cues=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);
        require(cues.size()==1 && cues[0].color==1,"recolored recorded small diamond must survive shape/color checks");
        const auto cue=cues[0];
        std::vector<CueSearchRegion> region{{cue.x, cue.y, cue.x, cue.y}};
        const auto restricted=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0,&region);
        require(restricted.size()==1 && restricted[0].x==cue.x && restricted[0].y==cue.y,
            "region pruning must retain the complete diamond");
        const auto rgb=detector.detect(patch.data(),24,24,72,PixelFormat::RGB8,left,top);
        require(rgb.size()==1 && rgb[0].x==cue.x && rgb[0].y==cue.y,"recorded RGB diamond maps to the same capture position");
        // Isolate cue/body geometry from the separate friendly-background
        // heuristic (the recorded first patch also contains green leaves).
        // Both detector layouts above consume the explicitly recolored RGB pixels.
        f.person(cue.x,cue.y);
        auto s=selector();s.set_detection_policy(false,.2f,.65f);
        const auto paired=f.run(s);
        require(paired.has_target && paired.detections[0].has_cue_point,
            "recorded diamond must associate with the direct person");
        f.batch.detections.clear();f.batch.captured_at_ns+=10'000'000;
        const auto held=f.run(s);
        require(held.has_target && held.selector_target_generation==paired.selector_target_generation &&
            !held.has_selected_detection && !held.fire_authority,"recorded diamond sustains same-identity cue hold without firing");
    }
}
void person_or_cue_independent_acquisition() {
    auto fused=selector();
    VisionTargetSelector original(640,512,190,true,.2f,.65f);
    Scene f;
    for (int i=0;i<40;++i) {
        f.clear(); f.person(350,220);
        // An unrelated colored marker cannot veto or reposition a person.
        if (i%2) f.cue(200,150);
        const auto a=f.run(fused), b=f.run(original);
        require(a.has_target==b.has_target,"OR must preserve standalone person admission");
        if (a.has_target) {
            require(a.has_selected_detection && a.target_x==b.target_x && a.target_y==b.target_y,
                "OR must preserve direct person chest coordinates");
            require(a.selector_target_generation==1,"unrelated cue must not change person identity");
        }
    }
    // A cue can be paired later, after a person-only acquisition.
    f.clear(); f.cue(350,220); f.person(350,220); const auto paired=f.run(fused);
    f.clear(); f.cue(345,225); const auto held=f.run(fused);
    require(held.has_target && held.target_x==paired.target_x-5 && held.target_y==paired.target_y+5,
        "late same-frame pair must record the actual person-to-cue offset");
}
void randomized_cue_body_reference() {
    std::uint64_t ticks=0;
    for (unsigned seed:{2026102001u,32453011u,49980211u,104396183u}) {
        std::mt19937 rng(seed);
        for (int duration:{100,1000}) {
            auto s=selector(); Scene f;
            const int origin=280+int(rng()%30),cy=210+int(rng()%20);
            f.cue(origin,cy); f.person(origin,cy); auto reference=f.run(s);
            require(reference.has_target,"reference initialization");
            float offset_x=reference.target_x-origin,offset_y=reference.target_y-cy;
            for (int i=0;i<duration;++i) {
                f.clear(); f.batch.captured_at_ns+=10'000'000; // 50 Hz: 2 / 20 seconds
                const int x=origin+int(12*std::sin(i*.07)),y=cy+int(8*std::cos(i*.03));
                f.cue(x,y);
                if (i==duration/2) {
                    f.person(float(x+3),float(y+8));
                } else if (i%7==0) {
                    f.person(float(x+8),float(y+15),.25f); // weak fragment must not recalibrate
                }
                const auto r=f.run(s);
                require(r.has_target && r.selector_target_generation==reference.selector_target_generation,
                    "long current cue must retain its target generation");
                if (i==duration/2) {
                    require(r.has_selected_detection,"returning strong body recalibrates reference");
                    offset_x=r.target_x-x; offset_y=r.target_y-y;
                } else {
                    require(std::abs(r.target_x-x-offset_x)<.001f && std::abs(r.target_y-y-offset_y)<.001f,
                        "cue translation must not accumulate drift or learn weak body geometry");
                    require(!r.fire_authority && !r.has_selected_detection,"cue evidence alone is aim-only");
                }
                ++ticks;
            }
            f.clear(); auto r=f.run(s);
            require(!r.has_target && !r.aim_authority,"a missing current cue must stop output immediately");
            s.reset(); f.clear(); f.cue(origin,cy); r=f.run(s);
            require(!r.has_target,"reset must discard old body reference");
        }
    }
    require(ticks==4400,"short and long reference validation across independent seeds");
}
void randomized_cue_identity() {
    std::uint64_t ticks=0;
    for (unsigned seed:{2026101902u,32452937u,49979807u,104395489u}) {
        std::mt19937 rng(seed);
        for (int test=0;test<32;++test) for (int duration:{120,1200}) {
            CueIdentity owner; const float phase=float(rng()%1000)/100;
            CueObservation cue{280,210,11,.9f,1}; std::uint64_t ns=1'000'000'000;
            owner.seed(cue,ns);
            for (int i=0;i<duration;++i) {
                ns+=16'666'667;
                cue.x=280+15*std::sin(phase+i*.02f); cue.y=210+10*std::cos(phase+i*.01f);
                CueObservation neighbour{cue.x+70, cue.y,11,1,1};
                std::vector<CueObservation> observations{cue,neighbour};
                if (rng()%2) std::reverse(observations.begin(),observations.end());
                owner.update(observations,ns);
                require(owner.active() && owner.fresh() && owner.active()->x==cue.x,"randomized cue identity switched"); ++ticks;
            }
        }
    }
    require(ticks==168960,"explicit randomized coverage");
    CueIdentity owner; owner.seed({300,230,11,1,1},1'000'000'000);
    owner.update({{298,230,11,1,1},{302,230,11,1,1}},1'010'000'000);
    require(owner.active() && !owner.fresh(),"ambiguous cues retain identity without fresh output");
}
void cue_controller_integration() {
    double now=1;
    controller_native::NativeGamepadController controller(
        controller_native::incident_fixture::base_config(40,180),&now);
    auto s=selector(); Scene f;
    auto physical=controller_native::incident_fixture::ads_input();
    const auto publish=[&]() {
        auto result=f.run(s);
        now=double(f.batch.captured_at_ns)*1e-9;
        result.frame_id=++f.batch.frame_id; result.captured_at_ns=f.batch.captured_at_ns;
        result.result_at_ns=f.batch.captured_at_ns; result.frame_updated=true;
        controller.submit_vision_snapshot(runtime_app::adapt_vision_result(result));
        return controller.build_output(physical);
    };
    f.cue(380,220); f.person(320,320); // unrelated person cannot give this cue a body
    auto output=publish();
    require(controller.last_target_plan().aim_authority==0 && output.right_x==0,"cue-only identity must not become downstream acquisition");
    f.clear(); f.cue(380,220); f.person(380,220); output=publish();
    const auto id=controller.last_target_plan().target_id;
    require(id!=0 && output.right_x>0,"cue/body pair must reach actual ADS output");
    for (int i=0;i<80;++i) {
        f.clear(); f.cue(380,220); f.cue(320,220); f.person(320,220,.99f);
        publish();
        const auto& plan=controller.last_target_plan();
        require(plan.target_id==id,"downstream target identity must remain cue-owned");
        require(plan.aim_authority>0 && !plan.fire_authority,"continuous cue must remain aim-only downstream beyond 180 ms");
    }
    physical=controller_native::incident_fixture::ads_input(-.95f,0);
    bool escaped=false;
    for (int i=0;i<40;++i) {
        f.clear(); f.cue(380,220); f.person(380,220); output=publish();
        if (output.right_x<=-.94f) { escaped=true; break; }
    }
    require(escaped,"cue lock must preserve sustained explicit manual exit");
    physical.left_trigger=0; physical.right_x=.012f; physical.right_y=-.012f;
    now+=.001; output=controller.build_output(physical);
    require(output.right_x==physical.right_x && output.right_y==physical.right_y,"release preserves zero-deadzone raw passthrough");
}
}
void register_cue_identity_tests(native_test::Registry& registry) {
    registry.add_case("BaseVisionSelection","cue_returns_after_person_occlusion",[] {
        auto s=selector();Scene f;f.cue(300,200);f.person(300,200);const auto ref=f.run(s);
        // Marker hidden first, real body still observed; then both disappear.
        for(int i=0;i<25;++i){f.clear();f.person(300,200);require(f.run(s).has_target,"person OR admission during cue gap");}
        for(int i=0;i<9;++i){f.clear();require(!f.run(s).has_target,"missing evidence cannot produce aim");}
        f.clear();f.cue(306,203);const auto r=f.run(s);
        require(r.has_target && !r.has_selected_detection && r.selector_target_generation>=ref.selector_target_generation &&
            std::abs(r.target_x-ref.target_x-6)<.001f && std::abs(r.target_y-ref.target_y-3)<.001f,
            "current unique cue must recover recent body calibration without a new full person");
        require(!r.fire_authority,"recovered cue remains aim-only");
    });
    registry.add_case("BaseVisionSelection","background_growth_cannot_disprove_visible_cue",[] {
        auto s=selector();Scene f;f.cue(310,229);f.person(310,229);auto& d=f.batch.detections.back();
        d.x1=292;d.x2=328;d.y1=244;d.y2=294;const auto ref=f.run(s);
        f.clear();f.cue(310,229);f.person(310,229);auto& b=f.batch.detections.back();
        b.x1=281;b.x2=327;b.y1=183;b.y2=292;auto r=f.run(s);
        require(r.has_target && !r.has_selected_detection && std::abs(r.target_y-ref.target_y)<.001f,
            "a box growing above a reliable head cue is not independent recovery evidence");
        f.clear();f.cue(308,230);r=f.run(s);
        require(r.has_target && std::abs(r.target_y-ref.target_y-1)<.001f,"background must not discard cue reference");
    });
    registry.add_case("BaseVisionSelection","cue_recovery_requires_current_unique_recent_evidence",[] {
        for(unsigned seed:{20261025u,73129u,880301u,407771u})for(int cycles:{4,40}) {
            std::mt19937 rng(seed);auto s=selector();Scene f;
            for(int cycle=0;cycle<cycles;++cycle) {
                s.reset();const int x=270+int(rng()%81),y=180+int(rng()%41);
                f.clear();f.cue(x,y);f.person(float(x),float(y));const auto ref=f.run(s);require(ref.has_target,"seed");
                for(int i=0;i<15;++i){f.clear();f.person(float(x),float(y));require(f.run(s).has_target,"live person during cue loss");}
                for(int i=0;i<8;++i){f.clear();require(!f.run(s).has_target,"no output from retained calibration alone");}
                f.clear();f.cue(x-10,y);f.cue(x+10,y);require(!f.run(s).has_target,"ambiguous current cues cannot recover");
                if(cycle%2) {
                    for(int i=0;i<12;++i){f.clear();require(!f.run(s).has_target,"expiry produces no output");}
                    f.clear();f.cue(x,y);require(!f.run(s).has_target,"expired calibration cannot be revived");
                } else {
                    f.clear();f.cue(x+3,y+2);auto r=f.run(s);
                    require(r.has_target && !r.fire_authority && std::abs(r.target_x-ref.target_x-3)<.001f,
                        "recent unique reacquisition is aim-only at the calibrated anatomy");
                    s.reset();f.clear();f.cue(x+3,y+2);require(!f.run(s).has_target,"explicit reset discards recovery evidence");
                }
            }
        }
    });
    registry.add_case("BaseVisionSelection","side_pose_cue_keeps_body_during_occlusion",[] {
        auto s=selector();Scene f;f.cue(330,213);
        Detection d;d.x1=296;d.x2=332;d.y1=243;d.y2=314;d.conf=.85f;
        f.batch.detections={d};f.run(s);f.batch.captured_at_ns+=10'000'000;const auto ref=f.run(s);
        require(ref.has_selected_detection,"side-facing body acquires independently");
        for(int i=0;i<25;++i) {
            f.clear();f.cue(330+i%3,213+i%5);const auto r=f.run(s);
            require(r.has_target && !r.has_selected_detection &&
                std::abs(r.target_x-ref.target_x-i%3)<.001f &&
                std::abs(r.target_y-ref.target_y-i%5)<.001f,
                "a head near the shoulder is valid anatomy, not an inflated side-box cue");
        }
    });
    registry.add_case("BaseVisionSelection","cue_calibration_cannot_ratchet_top_gap",[] {
        for (unsigned seed:{73129u,880301u,407771u,20261025u}) for (int duration:{120,1200}) {
            auto s=selector();Scene f;std::mt19937 rng(seed);
            f.cue(300,230);f.person(300,230);const auto initial=f.run(s);
            for(int i=0;i<duration;++i) {
                const int x=300+int(12*std::sin(i*.03)),y=230+int(8*std::cos(i*.04));
                const float drift=float(std::min(37,i/3));
                f.clear();f.cue(x,y);f.person(float(x),float(y),.8f+float(rng()%10)*.01f);
                f.batch.detections.back().y1-=drift;
                const auto r=f.run(s);
                require(r.has_target && std::abs((r.target_y-y)-(initial.target_y-230))<=12,
                    "bounded top-edge noise must not accumulate into cue offset drift");
            }
            f.clear();f.cue(300,230);f.person(300,230);auto r=f.run(s);
            require(r.has_selected_detection && std::abs(r.target_y-initial.target_y)<.001f,
                "original body must recover immediately against fixed calibration");
        }
    });
    registry.add_case("BaseVisionSelection","cue_has_one_body_owner",[] {
        auto s=selector();Scene f;f.cue(340,203);
        Detection near;near.x1=64;near.y1=197;near.x2=354;near.y2=511;near.conf=.69f;
        Detection far;far.x1=322;far.y1=215;far.x2=348;far.y2=273;far.conf=.61f;
        f.batch.detections={near,far};auto r=f.run(s);
        require(!r.selection_diagnostics.boxes[0].has_cue && r.selection_diagnostics.boxes[1].has_cue,
            "foreground body cannot borrow the better-matched background cue");
        f.batch.captured_at_ns+=10'000'000;std::reverse(f.batch.detections.begin(),f.batch.detections.end());r=f.run(s);
        require(r.selection_diagnostics.boxes[0].has_cue && !r.selection_diagnostics.boxes[1].has_cue,
            "mutual cue ownership must not depend on detection ordering");
    });
    registry.add_case("BaseVisionSelection","unpaired_fragment_or_background_cannot_replace_fresh_cue_body",[] {
        auto s=selector();Scene f;f.cue(300,180);f.person(300,180);const auto initial=f.run(s);
        for(const bool background:{false,true}) {
            f.clear();f.cue(300,180);f.person(300,180,.95f);auto& d=f.batch.detections.back();
            if(background){d.x1=230;d.x2=578;d.y1=0;d.y2=246;}
            else {d.x1=295;d.x2=325;d.y1=171;d.y2=200;}
            const auto r=f.run(s);
            require(r.has_target && !r.has_selected_detection && r.selector_target_generation==initial.selector_target_generation &&
                std::abs(r.target_y-initial.target_y)<.001f,
                "independent false/partial box cannot overwrite same-identity anatomy");
        }
    });
    registry.add_case("BaseVisionSelection","edge_cue_cannot_calibrate_inflated_body",[] {
        auto s=selector();Scene f;f.cue(15,14);
        Detection d;d.x1=0;d.y1=0;d.x2=624;d.y2=512;d.conf=.9f;f.batch.detections={d};
        f.run(s);f.batch.captured_at_ns+=10'000'000;auto r=f.run(s);
        require(r.has_selected_detection,"person-only acquisition stays available at the edge");
        f.clear();f.cue(25,14);r=f.run(s);
        require(!r.has_target,"side marker must not retain a 300px invented body offset");
    });
    registry.add_case("BaseVisionSelection","friendly_marker_cut_by_roi_boundary",[] {
        for(int top=196;top<=200;++top) {
            auto s=selector();Scene f;f.person(209,185);auto& d=f.batch.detections.back();
            d.x1=64;d.x2=354;d.y1=float(top);d.y2=511;
            for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)if(x*x+y*y>=9 && x*x+y*y<=25){
                auto* p=&f.pixels[((156+y)*640+183+x)*4];p[0]=p[1]=255;
            }
            const auto r=f.run(s);
            if(!r.detections[0].is_friendly)throw std::runtime_error("compact cyan marker lost at ROI body top="+std::to_string(top));
        }
    });
    registry.add_case("BaseVisionSelection","close_body_head_fragment_keeps_anatomy",[] {
        auto s=selector();Scene f;f.cue(310,215);
        Detection d;d.x1=72;d.y1=127;d.x2=567;d.y2=512;d.conf=.8f;f.batch.detections={d};
        const auto ref=f.run(s);require(ref.has_selected_detection,"close cropped actor acquires");
        f.clear();f.cue(310,217);d.x1=310;d.x2=473;d.y1=132;d.y2=295;d.conf=.9f;f.batch.detections={d};
        const auto r=f.run(s);
        require(r.has_target && !r.has_selected_detection && std::abs(r.target_y-ref.target_y-2)<.001f,
            "uncued head fragment cannot bypass current close-body reference");
    });
    registry.add_case("BaseVisionSelection","cue_owned_identity_expires_without_evidence",[] {
        auto s=selector();Scene f;f.cue(300,180);f.person(300,180);require(f.run(s).has_target,"seed");
        for(int i=0;i<12;++i){f.clear();require(!f.run(s).has_target,"no synthetic output on missing evidence");}
        f.clear();f.person(300,180,.44f);
        require(!f.run(s).has_target,"expired enemy evidence cannot lend tracking confidence to a new object");
        f.clear();f.person(300,180,.9f);f.run(s);
        f.clear();f.person(300,180,.9f);
        require(f.run(s).has_target,"independent person acquisition survives cue expiry");
    });
    registry.add_case("BaseVisionSelection","short_person_gap_retains_tracking_admission",[] {
        auto s=selector();Scene f;f.cue(300,180);f.person(300,180);const auto ref=f.run(s);
        for(int i=0;i<3;++i){f.clear();f.run(s);}
        f.clear();f.person(300,180,.45f);const auto r=f.run(s);
        require(r.has_target && r.has_selected_detection && r.selector_target_generation==ref.selector_target_generation,
            "brief body gap preserves tracking confidence without requiring reacquisition");
    });
    registry.add_case("BaseVisionSelection","unpaired_same_body_cannot_discard_calibration",[] {
        auto s=selector();Scene f;f.cue(320,220);f.person(320,220,.9f);const auto ref=f.run(s);
        for(int h=120;h>=30;--h) {
            f.clear();f.cue(320,220);f.person(320,220,.9f);f.batch.detections.back().y2=232+float(h);
            Detection fragment;fragment.x1=310;fragment.x2=330;fragment.y1=225;fragment.y2=250;fragment.conf=.2f;
            f.batch.detections.push_back(fragment);const auto r=f.run(s);
            require(r.has_target && std::abs(r.target_y-ref.target_y)<=4,
                "temporary lack of unique pairing is not contrary evidence against the reference");
        }
    });
    registry.add_case("BaseVisionSelection","small_side_object_is_not_close_actor_identity",[] {
        auto s=selector();Scene f;
        Detection d;d.x1=149;d.y1=98;d.x2=581;d.y2=512;d.conf=.9f;f.batch.detections={d};
        f.run(s);f.batch.captured_at_ns+=10'000'000;require(f.run(s).has_target,"close person");
        f.clear();d.x1=546;d.y1=142;d.x2=639;d.y2=356;f.batch.detections={d};
        require(!f.run(s).has_target,"side object cannot inherit large body's identity via center tolerance");
    });
    registry.add_case("BaseVisionSelection","independent_body_retires_contradicted_extent",[] {
        auto s=selector();Scene f;
        auto body=[&](float top) {f.person(320,220,.9f);auto& d=f.batch.detections.back();d.x1=305;d.x2=335;d.y1=top;d.y2=312;};
        f.cue(320,220);body(252);require(f.run(s).has_target,"partial initial pair");
        // Two plausible person pairings, but the already tracked cue is unique
        // in motion space. Recovery must still respect head-above-body anatomy.
        f.clear();f.cue(320,220);f.cue(320,208);body(232);auto full=f.run(s);
        require(full.has_selected_detection,"complete body below the head cue remains authoritative");
        f.clear();f.cue(320,220);body(234);full=f.run(s);
        require(full.has_selected_detection && std::abs(full.target_y-249.6f)<.001f,
            "old partial geometry must not veto anatomically supported recovery");
        s.reset();f.clear();f.cue(320,220);f.person(320,220);const auto ref=f.run(s);
        f.clear();f.cue(320,220);f.person(320,220,.95f);f.batch.detections.back().y1=310;
        require(!f.run(s).has_selected_detection,"same-identity contracting fragment cannot override fresh cue anatomy");
        f.clear();f.cue(320,220);auto held=f.run(s);
        require(held.has_target && std::abs(held.target_y-ref.target_y)<.001f,
            "an unpaired contracting fragment must not discard the calibrated body extent");
    });
    registry.add_case("BaseEndToEnd","ironsight_extent_does_not_create_vertical_command",[] {
        double now=1;
        controller_native::NativeGamepadController controller(
            controller_native::incident_fixture::base_config(40,180),&now);
        auto s=selector();Scene f;const auto physical=controller_native::incident_fixture::ads_input();
        for(int i=0;i<400;++i) {
            f.clear();f.cue(320,220);f.person(320,220,.9f);
            auto& d=f.batch.detections.back();d.x1=297.5f;d.x2=342.5f;d.y1=238;d.y2=328;
            if(i>=40 && i<350) {d.x1=306;d.x2=334;d.y2=278;}
            auto r=f.run(s);now=double(f.batch.captured_at_ns)*1e-9;
            r.frame_id=++f.batch.frame_id;r.captured_at_ns=f.batch.captured_at_ns;
            r.result_at_ns=r.captured_at_ns;r.frame_updated=true;
            controller.submit_vision_snapshot(runtime_app::adapt_vision_result(r));
            const auto output=controller.build_output(physical);
            require(r.has_target && std::abs(r.target_y-256)<.001f,"stationary chest stays centered during occlusion");
            require(std::abs(output.right_y)<.001f,"box truncation must not manufacture an upward stick command");
        }
    });
    registry.add_case("BaseVisionSelection","cue_extent_scale_and_gradual_truncation",[] {
        for(unsigned seed:{25102501u,32453011u,49980211u,104396183u})
        for(int duration:{120,1200}) {
            std::mt19937 rng(seed);auto s=selector();Scene f;
            const float phase=(rng()%1000)*.01f;
            // Independent, explicit trajectory: translation + whole-body scale,
            // UI gap either fixed or proportional. No visibility inference oracle.
            for(int i=0;i<duration;++i) {
                f.clear();const int x=300+int(18*std::sin(i*.05+phase)),y=210+int(12*std::cos(i*.03));
                const float scale=1+.3f*std::sin(i*.025+phase);
                const float gap=seed%2 ? 12.f : 12*scale;
                f.cue(x,y);f.person(x,y,.95f);auto& d=f.batch.detections.back();
                d.x1=x-25*scale;d.x2=x+25*scale;d.y1=y+gap;d.y2=d.y1+120*scale;
                auto r=f.run(s);
                require(r.has_selected_detection && std::abs(r.target_y-(d.y1+24*scale))<.001f,
                    "real uniform scale/translation must update anatomy immediately");
            }
            s.reset();f.clear();f.cue(320,210);f.person(320,210);const auto ref=f.run(s);
            // One-pixel contractions must not repeatedly retrain the shape model.
            // Edge tolerance allows <= 4 px anatomy variation, not a collapse.
            for(int h=119;h>=24;--h) {
                f.clear();f.cue(320,210);f.person(320,210,.95f);
                f.batch.detections.back().y2=222+float(h);auto r=f.run(s);
                require(r.has_target && r.selector_target_generation==ref.selector_target_generation &&
                    std::abs(r.target_y-ref.target_y)<=4,
                    "gradual high-confidence truncation must have bounded anatomy drift");
            }
            f.clear();f.cue(320,210);f.person(320,210);auto r=f.run(s);
            require(r.has_selected_detection && std::abs(r.target_y-ref.target_y)<.001f,"recovery without delay");
            f.clear();f.cue(320,210);r=f.run(s);
            require(r.has_target && std::abs(r.target_y-ref.target_y)<.001f,"recovered body owns cue-only geometry");
        }
    });
    registry.add_case("BaseVisionSelection","ironsight_fragment_cannot_recalibrate_cue",[] {
        auto s=selector();Scene f;
        auto body=[&](float w,float h,float gap,float conf) {
            f.person(320,220,conf);auto& d=f.batch.detections.back();
            d.x1=320-w*.5f;d.x2=320+w*.5f;d.y1=220+gap;d.y2=d.y1+h;
        };
        // Rounded recorded dimensions: full person -> iron-sight upper fragment
        // -> background above the cue. Confidence alone does not encode extent.
        f.cue(320,220);body(45.17f,87.33f,27,.65f);const auto ref=f.run(s);
        require(ref.has_target,"complete seed");
        for(auto dims:{std::array<float,4>{30.33f,44.33f,27.67f,.69f},
                       {29.17f,39,28.67f,.81f},{22,33.33f,23,.52f},
                       {28.83f,62.67f,-7.5f,.57f}}) {
            f.clear();f.cue(320,220);body(dims[0],dims[1],dims[2],dims[3]);
            const auto r=f.run(s);
            require(r.has_target && std::abs(r.target_y-ref.target_y)<.001f,
                "high-confidence partial boxes must not pull the cue-relative chest upward");
            require(!r.fire_authority && !r.has_selected_detection && r.selector_target_generation==ref.selector_target_generation,
                "untrusted geometry uses existing aim-only cue continuation");
        }
        f.clear();f.cue(320,220);body(45,87,28,.9f);const auto recovered=f.run(s);
        require(recovered.has_selected_detection && recovered.fire_authority,"complete person recovers immediately");
    });
    registry.add_case("BaseVisionSelection","configurable_marker_palette_rgb_domain",[] {
        using pipeline_contract::MarkerPalette;
        const auto palette=MarkerPalette::from_hex("ff00ff","#00FFFF");
        // Independent floating HSV oracle over every 8-bit RGB value.
        for(int r=0;r<256;++r)for(int g=0;g<256;++g)for(int b=0;b<256;++b) {
            const double hi=std::max({r,g,b}),lo=std::min({r,g,b}),d=hi-lo;
            double h=0,s=hi==0 ? 0 : 255*d/hi;
            if(d>0){h=30*(hi==r ? (g-b)/d : hi==g ? 2+(b-r)/d : 4+(r-g)/d);if(h<0)h+=180;}
            const bool enemy=d>0 && h>=142-1e-10 && h<=158+1e-10 && s>=120-1e-10 && hi>=120;
            const bool friendly=d>0 && h>=82-1e-10 && h<=98+1e-10 && s>=80-1e-10 && hi>=50;
            require(palette.enemy.matches(r,g,b)==enemy,"enemy integer band differs from independent HSV oracle");
            require(palette.friendly.matches(r,g,b)==friendly,"friendly integer band differs from independent HSV oracle");
        }
        const auto custom=MarkerPalette::from_hex("FF0000","00FF00");
        require(custom.enemy.matches(255,0,24) && custom.enemy.matches(255,24,0),"red hue wrap must support both sides");
        require(custom.friendly.matches(0,255,0) && !custom.enemy.matches(255,0,255),"custom colors replace defaults");
    });
    registry.add_case("BaseVisionSelection","palette_rejects_old_colors_and_configures_cue",[] {
        Scene f;f.cue(300,220);CueDetector detector;
        require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).size()==1,"magenta default");
        for(auto color: {std::array<int,3>{255,255,0},{255,0,0},{0,255,0},{0,255,255}}) {
            for(int i=0;i<640*512;++i)if(f.pixels[i*4+2] || f.pixels[i*4]) {
                f.pixels[i*4+2]=color[0];f.pixels[i*4+1]=color[1];f.pixels[i*4]=color[2];
            }
            require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).empty(),"old enemy colors / friend cyan must not become cues");
            f.clear();f.cue(300,220);
        }
        detector.set_enemy_color(pipeline_contract::MarkerPalette::from_hex("FF0000","00FF00").enemy);
        for(int i=0;i<640*512;++i)f.pixels[i*4]=0;
        require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).size()==1,"configured red must reach independent detector");
        auto configured=selector();
        configured.set_marker_palette(pipeline_contract::MarkerPalette::from_hex("FF0000","00FF00"));
        f.person(300,220);
        const auto paired=f.run(configured);
        require(paired.has_target && paired.motion_cue_id,"configured palette must pair cue with person");
        f.batch.detections.clear();f.batch.captured_at_ns+=10'000'000;
        const auto held=f.run(configured);
        require(held.has_target && !held.has_selected_detection && !held.fire_authority,"configured cue must sustain same body reference");
        configured.set_marker_palette(pipeline_contract::MarkerPalette{});
        require(!f.run(configured).has_target,"palette change must invalidate prior color identity/template");
    });
    registry.add_case("BaseVisionSelection","cue_regions_preserve_boundaries_and_empty_frame",[] {
        Scene f;CueDetector detector;f.cue(80,401);f.cue(560,100);
        std::vector<CueSearchRegion> regions{{80,401,80,401}};
        const auto& found=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0,&regions);
        require(found.size()==1 && found[0].x==80 && found[0].y==401,"tiny lower-frame search region must preserve full marker shape and seed phases");
        regions.clear();
        require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0,&regions).empty(),"empty regions must clear previous observations");
        require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).size()==2,"full-frame detector remains available after restricted/empty calls");
    });
    registry.add_case("BaseVisionSelection","cue_regions_preserve_hit_marker_composition",[] {
        Scene f;CueDetector detector;
        for(int x:{308,332})for(int y:{244,268})f.cue(x,y);
        f.cue(80,401);
        const std::vector<CueSearchRegion> regions{{308,244,308,244},{80,401,80,401}};
        const auto& cues=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0,&regions);
        require(cues.size()==1 && cues[0].x==80,"region around a single hit-marker tip must retain all four partners for rejection");
    });
    registry.add_case("BaseVisionSelection","cue_regions_follow_active_target_into_lower_frame",[] {
        auto s=selector();Scene f;f.cue(320,220);f.person(320,220);auto r=f.run(s);
        require(r.has_target && r.motion_cue_id,"seed cue/body reference");
        const auto generation=r.selector_target_generation;
        for(int y=225;y<=420;y+=5){f.clear();f.cue(320,y);r=f.run(s);
            require(r.has_target && r.enemy_cue_current && r.selector_target_generation==generation,
                "active cue must remain searched without a person, including the lower capture area");
        }
    });
    registry.add_case("BaseVisionSelection","sparse_cue_seed_rows_preserve_small_markers",sparse_cue_seed_rows_preserve_small_markers);
    registry.add_case("BaseVisionSelection","motion_cue_identity_survives_person_dropout",[] {
        auto s=selector();Scene f;f.cue(320,180);f.person(320,180);
        auto r=f.run(s);const auto id=r.motion_cue_id;
        require(r.has_target && id,"paired selector must publish a motion cue");
        for(int i=1;i<=30;++i) {
            f.clear();f.cue(320+i,180-i/2);
            if(i%3)f.person(320+i,180-i/2);
            r=f.run(s);
            require(r.has_target && r.motion_cue_id==id && std::abs(r.motion_cue_x-(320+i))<.01f,
                "person availability cannot replace current marker correspondence");
            r.frame_updated=true;
            const auto snapshot=runtime_app::adapt_vision_result(r);
            require(snapshot.motion_cue_id==id && snapshot.motion_cue_px.x==r.motion_cue_x,
                "adapter must preserve selector marker identity and coordinate");
        }
        f.clear();r=f.run(s);require(!r.motion_cue_id,"missing marker cannot publish stale motion evidence");
        s.reset();f.clear();f.cue(340,180);f.person(340,180);r=f.run(s);
        require(r.motion_cue_id && r.motion_cue_id!=id,"reset cannot join different marker tracks");
    });
    registry.add_case("BaseBodyLock","cue_motion_is_independent_of_person_geometry",[] {
        namespace f=controller_native::incident_fixture;
        auto cfg=f::base_config(40,180);
        cfg.ai_aim.ads_snap_max_ai_force=cfg.ai_aim.ads_snap_max_ai_force_y=0;
        cfg.ai_aim.body_lock_max_ai_force=cfg.ai_aim.body_lock_max_ai_force_y=0;
        cfg.ai_aim.aim_response_learning_enabled=false;
        double now=10;controller_native::NativeGamepadController controller(cfg,&now);
        f::TargetSpec spec;spec.selector_generation=1;spec.has_enemy_cue=true;
        for(int i=0;i<80;++i) {
            now=10+i*.01;spec.observation_id=i+1;
            auto snapshot=f::observed_snapshot(spec,i+1,now,0,float((i%4)*10));
            snapshot.motion_cue_id=7;snapshot.motion_cue_px={320,180};
            controller.submit_vision_snapshot(snapshot);controller.build_output(f::ads_input());
            const auto&p=controller.last_target_plan();
            if(i>5)require(p.bodylock_target_motion_valid && std::abs(p.bodylock_target_motion_px_per_sec.y)<.1f,
                "stationary same cue cannot acquire person box reconstruction velocity");
            require(std::abs(p.source_aim_px.y-(256+(i%4)*10))<.01f,
                "cue motion must not move the person-selected chest");
        }
        for(int i=0;i<8;++i) {
            now+=.01;spec.observation_id=81+i;
            auto snapshot=i%2 ? f::observed_snapshot(spec,81+i,now,0,12) :
                f::cue_snapshot(spec,81+i,now,0,0);
            snapshot.motion_cue_id=7;snapshot.motion_cue_px={320,180};
            controller.submit_vision_snapshot(snapshot);controller.build_output(f::ads_input());
            const auto&p=controller.last_target_plan();
            require(p.bodylock_target_motion_valid && std::abs(p.bodylock_target_motion_px_per_sec.y)<.1f,
                "same cue remains a motion correspondence across person/hold transitions");
        }
        now+=.01;spec.observation_id=90;
        auto replacement=f::observed_snapshot(spec,90,now,0,0);
        replacement.motion_cue_id=8;replacement.motion_cue_px={340,240};
        controller.submit_vision_snapshot(replacement);controller.build_output(f::ads_input());
        require(!controller.last_target_plan().bodylock_target_motion_valid,
            "different cue id cannot inherit velocity or difference unrelated marker coordinates");
    });
    registry.add_case("BaseBodyLock","cue_preserves_validated_body_range",[] {
        namespace f=controller_native::incident_fixture;
        auto config=f::base_config(40,180); config.ai_aim.body_lock_activation_box_px=120;
        double now=10; controller_native::NativeGamepadController controller(config,&now);
        f::TargetSpec spec; spec.body_height=220; spec.body_width=154;
        spec.selector_generation=1; spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        float radius=0; std::uint64_t identity=0;
        for(int tick=0;tick<600;++tick) {
            now=10+tick*.001;
            if(tick%8==0) { spec.observation_id=tick/8+1;
                controller.submit_vision_snapshot(tick>=400 ? f::cue_snapshot(spec,spec.observation_id,now,145,0) :
                    f::observed_snapshot(spec,spec.observation_id,now,tick<200 ? 0.f : 145.f,0)); }
            controller.build_output(f::ads_input()); const auto& plan=controller.last_target_plan();
            if(tick==399) { radius=plan.position_response_radius_px; identity=plan.target_id;
                require(radius>145 && plan.aim_authority>0,"body-sized range must admit the established target"); }
            if(tick>=400) require(plan.target_id==identity && plan.aim_authority>0 && !plan.fire_authority &&
                std::abs(plan.position_response_radius_px-radius)<.001f,
                "same-identity cue keeps body range and aim-only continuation");
        }
    });
    registry.add_case("BaseAds","cue_arrival_requires_fresh_same_source_evidence",[] {
        namespace f=controller_native::incident_fixture;
        auto config=f::base_config(40,180);
        config.ai_aim.ads_completion_fresh_frames=3;
        config.ai_aim.ads_completion_radius_px=2;
        config.recoil.enabled=false;
        double now=10; controller_native::NativeGamepadController controller(config,&now);
        f::TargetSpec spec; spec.selector_generation=1; spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        std::uint64_t frame=0;
        const auto publish=[&](bool cue,float x) {
            now+=.008; ++frame;
            spec.observation_id=frame;
            controller.submit_vision_snapshot(cue ? f::cue_snapshot(spec,frame,now,x,0) : f::observed_snapshot(spec,frame,now,x,0));
            controller.build_output(f::ads_input());
            require(!controller.last_target_plan().cue_continuation || !controller.last_target_plan().fire_authority,
                "cue arrival cannot grant fire authority");
        };
        for(int i=0;i<12;++i) publish(false,30); // Admit ADS through its physical onset lifecycle.
        require(controller.last_target_plan().target_id!=0 &&
            controller.last_target_plan().mode==pipeline_contract::ControlMode::AdsAcquire,
            "fixture must establish an admitted person acquisition");
        publish(true,0);
        require(!controller.last_target_plan().ads_acquisition_complete_ns,"source switch cannot prove arrival");
        for(int i=0;i<5;++i) { now+=.001; controller.build_output(f::ads_input()); }
        require(!controller.last_target_plan().ads_acquisition_complete_ns,"controller ticks cannot manufacture cue arrivals");
        publish(true,20); publish(true,0);
        require(!controller.last_target_plan().ads_acquisition_complete_ns,"fast cue pass cannot settle on one center sample");
        publish(false,6); publish(true,-2);
        require(!controller.last_target_plan().ads_acquisition_complete_ns,"cross-domain sign change cannot complete ADS");
        for(int i=0;i<5;++i) publish(true,0);
        require(controller.last_target_plan().ads_acquisition_complete_ns!=0,"settled fresh cue must hand off to BodyLock");
    });
    registry.add_case("BaseVisionSelection","person_motion_reversal_gap_and_ambiguity",[] {
        auto s=selector(); Scene f;
        for(int i=0;i<5;++i) { f.clear(); f.person(280+i*2,224,.95f); f.run(s); }
        f.clear(); f.person(289,224,.95f); f.person(291,224,.95f);
        require(!f.run(s).has_target,"equally plausible different bodies must not use detection order as identity");
        f.clear(); f.person(292,224,.95f);
        auto r=f.run(s); require(r.has_target && r.selector_target_generation==1,"one ambiguous capture does not replace identity");
        f.clear(); f.person(290,224,.95f); r=f.run(s);
        require(r.has_target && r.target_x==290,"a single reversing actor is not rejected by prediction");
        f.clear(); f.run(s);
        f.clear(); f.person(286,224,.95f); r=f.run(s);
        require(r.has_target && r.target_x==286 && r.selector_target_generation==1,"short gap preserves actor identity");
    });
    registry.add_case("BaseVisionSelection","hit_marker_composition_preserves_individual_cues",[] {
        Scene f; CueDetector detector;
        for(bool red:{false,true}) {
            f.clear();
            for(int x:{308,332}) for(int y:{244,268}) f.cue(x,y,red);
            f.cue(320,256,red); f.cue(400,180,red);
            const auto& cues=detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0);
            require(cues.size()==2,"four hit-marker tips excluded, center/remote real cues retained");
            f.clear(); f.cue(308,244,red);
            require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).size()==1,
                "an individual cue near the crosshair must remain detectable");
        }
    });
    registry.add_case("BaseVisionSelection","strong_person_disproves_stale_cue_pair",[] {
        auto s=selector(); Scene f; f.cue(300,180); f.person(300,180); auto r=f.run(s);
        require(r.has_target,"paired body");
        for(int i=1;i<=10;++i) { f.clear(); f.cue(300,180); f.person(300-i*12.f,180); r=f.run(s);
            require(r.has_target && r.has_selected_detection,"independent person stays authoritative"); }
        f.clear(); f.cue(300,180); r=f.run(s);
        require(!r.has_target,"a contradicted cue cannot revive old body geometry");
    });
    registry.add_case("BaseVisionSelection","person_crossing_motion_and_order",[] {
        for(unsigned seed:{71020601u,71020613u,71020627u,71020643u})
        for(int fps:{60,120}) for(float separation:{0.f,2.f,8.f,20.f}) for(bool reorder:{false,true}) {
            std::mt19937 rng(seed); const float speed=100+rng()%101;
            auto s=selector();
            for(int frame=0;frame<fps;++frame) {
                const float t=frame/float(fps),ax=280+speed*t,bx=400-speed*t;
                Scene f; f.batch.captured_at_ns=1'000'000'000ull+std::uint64_t(std::llround(t*1e9));
                f.person(ax,224,.95f); if(frame>=fps/10) f.person(bx,224+separation,.95f);
                const bool reverse=reorder && frame%2 && f.batch.detections.size()==2;
                if(reverse) std::reverse(f.batch.detections.begin(),f.batch.detections.end());
                const auto r=s.select(f.batch);
                if(frame<2 && !r.has_target) continue; // Existing unmarked-person confirmation.
                if(!r.has_target || !r.has_selected_detection) throw std::runtime_error(
                    "continuous actor missing seed="+std::to_string(seed)+" fps="+std::to_string(fps)+
                    " separation="+std::to_string(separation)+" frame="+std::to_string(frame)+
                    " reason="+r.selection_diagnostics.outcome);
                require(std::abs(r.target_x-ax)<.001f && r.selector_target_generation==1,
                    "crossing and detection order cannot transfer the current identity to B");
            }
        }
    });
    registry.add_case("BaseVisionSelection","cue_owns_person_churn",cue_owns_person_churn);
    registry.add_case("BaseVisionSelection","cue_requires_body_geometry",cue_requires_body_geometry);
    registry.add_case("BaseVisionSelection","cue_loss_and_expiry",cue_loss_and_expiry);
    registry.add_case("BaseVisionSelection","cue_frame_and_reset",cue_frame_and_reset);
    registry.add_case("BaseVisionSelection","cue_handover_and_correction",cue_handover_and_correction);
    registry.add_case("BaseVisionSelection","cue_colors_and_formats",cue_colors_and_formats);
    registry.add_case("BaseVisionSelection","recorded_small_diamond_cue",recorded_small_diamond_cue);
    registry.add_case("BaseVisionSelection","person_or_cue_independent_acquisition",person_or_cue_independent_acquisition);
    registry.add_case("BaseVisionSelection","randomized_cue_body_reference",randomized_cue_body_reference);
    registry.add_case("BaseVisionSelection","randomized_cue_identity",randomized_cue_identity);
    registry.add_case("BaseEndToEnd","cue_identity_controller_manual_exit",cue_controller_integration);
}
