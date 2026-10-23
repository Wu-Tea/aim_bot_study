#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
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
    for (int cy=8;cy<20;++cy) for (bool clutter:{false,true}) {
        std::vector<unsigned char> pixels(64*48*4,0);
        const auto paint=[&](int x,int y) {
            auto* p=&pixels[(y*64+x)*4];p[2]=255;p[1]=220;
        };
        if (clutter) for(int y=1;y<47;++y)for(int x=1;x<23;++x)paint(x,y);
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
            if(dx*dx+dy*dy>=1 && dx*dx+dy*dy<=4)paint(40+dx,cy+dy);
        const auto& cues=detector.detect(pixels.data(),64,48,64*4,PixelFormat::BGRA8,0,0);
        require(cues.size()==1,"sparse seed rows must preserve every small-marker phase without fragmenting large clutter");
        require(cues[0].x==40 && cues[0].y==cy && cues[0].size==5,
            "seed search must reconstruct the complete marker at original resolution");
    }
}
struct Scene {
    std::vector<unsigned char> pixels=std::vector<unsigned char>(640*512*4,0);
    DetectionBatch batch;
    Scene() { batch.captured_at_ns=1'000'000'000ull; }
    void clear() { std::fill(pixels.begin(),pixels.end(),0); batch.detections.clear(); batch.captured_at_ns+=10'000'000; }
    void cue(int cx,int cy,bool red=false) {
        for (int y=-5;y<=5;++y) for (int x=-5;x<=5;++x) {
            const int rr=x*x+y*y;
            if (rr<9 || rr>25) continue;
            auto* p=&pixels[((cy+y)*640+cx+x)*4]; p[2]=255; p[1]=red ? 0 : 220;
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
    require(cues.size()==2 && cues[0].color==1 && cues[1].color==2,"yellow/red circle candidates");
    std::vector<unsigned char> rgb(640*512*3);
    for (int i=0;i<640*512;++i) { rgb[i*3]=f.pixels[i*4+2]; rgb[i*3+1]=f.pixels[i*4+1]; rgb[i*3+2]=f.pixels[i*4]; }
    cues=detector.detect(rgb.data(),640,512,1920,PixelFormat::RGB8,10,20);
    require(cues.size()==2 && cues[0].x==290 && cues[0].y==250,"RGB and origin mapping");
    f.clear();
    for (int y=220;y<225;++y) for (int x=280;x<330;++x) { f.pixels[(y*640+x)*4+1]=220; f.pixels[(y*640+x)*4+2]=255; }
    require(detector.detect(f.pixels.data(),640,512,2560,PixelFormat::BGRA8,0,0).empty(),"blood bar excluded");
    require(detector.detect(f.pixels.data(),640,512,100,PixelFormat::BGRA8,0,0).empty(),"invalid pitch rejected");
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
    registry.add_case("BaseVisionSelection","person_or_cue_independent_acquisition",person_or_cue_independent_acquisition);
    registry.add_case("BaseVisionSelection","randomized_cue_body_reference",randomized_cue_body_reference);
    registry.add_case("BaseVisionSelection","randomized_cue_identity",randomized_cue_identity);
    registry.add_case("BaseEndToEnd","cue_identity_controller_manual_exit",cue_controller_integration);
}
