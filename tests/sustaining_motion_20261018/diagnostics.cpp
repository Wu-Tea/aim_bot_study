// Read-only production probes. These are explanations, not alternate policies.
#include "controller_native/response_model_aim_solver.h"
#include "controller_native/target_coordinator.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace controller_native;
using namespace pipeline_contract;
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    std::filesystem::create_directories(argv[1]);
    std::ofstream positions(std::string(argv[1])+"/position.csv");
    positions<<"error,authority,position_response,pre_curve,output,effective_seconds\n";
    ResponseModelAimRequest r;
    r.range_position_response=r.delay_aware_velocity_response=true;
    r.position_range_px=150;r.minimum_position_stick=.5f;r.arrival_radius_px=2;
    r.arrival_horizon_seconds=.1f;r.response_px_per_stick_second=1000;
    r.feedback_delay_seconds=.01f;
    for(float e:{.01f,.1f,1.f,10.f})for(float a:{.0736f,.2208f,.92f}) {
        r.authority=a;r.error_px={e,0};const auto o=solve_response_model_aim(r);
        if(std::fabs(o.pre_curve_stick.x-o.position_stick.x*a)>1e-6f)return 1;
        positions<<e<<','<<a<<','<<o.position_stick.x<<','<<o.pre_curve_stick.x<<','<<o.stick.x<<','<<e/(o.stick.x*1000)<<'\n';
    }
    TargetCoordinatorConfig cfg;cfg.settle_frames=1;TargetCoordinator coordinator(cfg);
    coordinator.begin_ads_epoch(1,10);IntentState intent;intent.activation=AssistActivation::Engaged;
    VisionObservationBatch b;b.frame_width_px=480;b.frame_height_px=416;
    b.capture_fresh=b.selector_identity_protocol=b.selector_enemy_cue_checked=true;
    b.selector_target_generation=1;b.preferred_source_id=1;b.count=1;
    auto& c=b.candidates[0];c.source_id=1;c.has_aim_point=c.has_aim_region=c.has_body_box=c.body_cue=true;
    c.aim_region_source=AimRegionSource::VisionGeometry;c.reliability=c.confidence=.92f;
    c.normalized_size=.25f;c.box_size_px={48,80};
    std::ofstream boundary(std::string(argv[1])+"/boundary.csv");boundary<<"error,authority,target_id\n";
    for(float e:{0.f,69.9f,70.1f,69.9f}) {
        ++b.frame_id;b.source_time_seconds=b.publish_time_seconds=10+b.frame_id*.01;
        c.aim_px={240+e,208};c.aim_region_px=c.body_box_px={c.aim_px.x-24,168,48,80};
        const auto p=coordinator.update(b,intent,b.source_time_seconds);
        if(p.mode!=ControlMode::BodyLockFollow || std::fabs(p.aim_authority-(e<=70?.0736f:.2208f))>1e-6f)return 1;
        boundary<<e<<','<<p.aim_authority<<','<<p.target_id<<'\n';
    }
    std::ofstream composition(std::string(argv[1])+"/composition.csv");
    composition<<"curve,authority,kick_response,recoil_response,residual_response,ai_request,total_camera_response,uncompensated_response,required_ai_for_total\n";
    for(auto curve:{AimResponseCurveAlgorithm::Linear,AimResponseCurveAlgorithm::CodDynamicLegacyLut})
    for(float a:{.0736f,.3325f,.92f}) {
        r.response_curve.algorithm=curve;r.error_px={};r.authority=a;
        r.motion_is_sustaining_target_motion=true;
        const auto recoil=forward_aim_response_curve({0,-.2f},r.response_curve);
        const float residual=.2f+recoil.y;
        r.relative_velocity_px_per_sec={0,residual*1000};
        const auto output=solve_response_model_aim(r).stick;
        const auto combined=forward_aim_response_curve({output.x,output.y-.2f},r.response_curve);
        const auto required=inverse_aim_response_curve({0,-.2f},r.response_curve);
        composition<<int(curve)<<','<<a<<",0.2,"<<recoil.y<<','<<residual<<','<<output.y<<','<<combined.y<<','<<.2f+combined.y<<','<<required.y+.2f<<'\n';
    }
    std::cout<<"PASS unchanged position gain / 70px budget probes; wrote nonlinear composition diagnostic\n";
}
