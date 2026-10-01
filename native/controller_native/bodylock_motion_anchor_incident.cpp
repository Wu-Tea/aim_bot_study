#include "aim_response_curve_plugin.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <random>
#include <limits>
#include <stdexcept>

namespace {
struct Result {
    float maximum_motion_error = 0;
    float maximum_request = 0;
    float maximum_world_error = 0;
    int body_ticks = 0;
    int motion_ticks = 0;
    int geometry_changes = 0;
    bool identity = true;
    bool finite = true;
};

// A closed-loop, explicitly assumed plant with independently known rigid
// pixel displacement and semantic box-point noise. It does not replay Apex.
Result run(unsigned seed, int axis, int duration, int vision_ms, float noise,
           float speed, float manual, controller_native::AimResponseCurveAlgorithm curve,
           bool learning=false, int anchor_discontinuity=0) {
    using namespace controller_native;
    auto config=incident_fixture::base_config(100,180);
    config.ai_aim.aim_response_learning_enabled=learning;
    config.ai_aim.aim_response_effect_delay_ms=0;
    config.ai_aim.aim_response_initial_scale=1000;
    config.ai_aim.body_free_initial_scale=config.ai_aim.body_slow_initial_scale=1000;
    config.ai_aim.ads_free_initial_scale=config.ai_aim.ads_slow_initial_scale=1000;
    config.ai_aim.visual_authority_enabled=false;
    config.ai_aim.ads_completion_fresh_frames=2;
    config.aim_response_curve.algorithm=curve;
    double now=10;
    NativeGamepadController controller(config,&now);
    incident_fixture::TargetSpec target;
    target.selector_generation=99;
    target.has_enemy_cue=target.enemy_identity_confirmed=true;
    std::mt19937 rng(seed);
    float x=0,y=0,last_offset=0;
    std::uint64_t frame=1,identity=0;
    Result result;
    for (int tick=0;tick<duration;++tick) {
        const float velocity=tick>=200 ? speed : 0;
        if (tick%vision_ms==0 && !(tick>=500 && tick<510)) {
            const float offset=tick>=150 ? (static_cast<int>(rng()%3)-1)*noise : 0;
            result.geometry_changes+=offset!=last_offset;
            last_offset=offset;
            target.observation_id=frame;
            auto snapshot=incident_fixture::observed_snapshot(target,frame++,now,
                x+(axis ? 0:offset),y+(axis ? offset:0),tick==0);
            auto& candidate=snapshot.candidates[0];
            candidate.has_motion_anchor=true;
            candidate.motion_anchor_score=.9f;
            candidate.motion_anchor_px={target.center_x+x,target.center_y+y};
            if (tick>=150 && frame%7==0) {
                if (anchor_discontinuity==1) {
                    candidate.motion_anchor_score=.35f;
                    candidate.motion_anchor_px.x+=12;
                } else if (anchor_discontinuity==2) {
                    candidate.motion_anchor_px.x+=200;
                } else if (anchor_discontinuity==3) {
                    candidate.motion_anchor_px.y=std::numeric_limits<float>::quiet_NaN();
                } else if (anchor_discontinuity==4) {
                    candidate.has_motion_anchor=false;
                    candidate.motion_anchor_px.x+=12;
                }
            }
            controller.submit_vision_snapshot(snapshot);
        }
        const auto output=controller.build_output(incident_fixture::ads_input(
            axis ? 0:manual,axis ? manual:0));
        const auto& plan=controller.last_target_plan();
        if (!identity && plan.target_id) identity=plan.target_id;
        result.identity &= identity!=0 && plan.target_id==identity;
        result.finite &= incident_fixture::finite_unit(output.right_x) && incident_fixture::finite_unit(output.right_y);
        if (tick>=400 && plan.mode==pipeline_contract::ControlMode::BodyLockFollow) {
            ++result.body_ticks;
            if (plan.bodylock_target_motion_valid) {
                ++result.motion_ticks;
                const float motion=axis ? plan.bodylock_target_motion_px_per_sec.y : plan.bodylock_target_motion_px_per_sec.x;
                result.maximum_motion_error=std::max(result.maximum_motion_error,std::fabs(motion-velocity)/1000);
            }
            const auto& components=controller.last_output_components();
            result.maximum_request=std::max(result.maximum_request,std::hypot(components.requested_assist_stick.x,components.requested_assist_stick.y));
            result.maximum_world_error=std::max(result.maximum_world_error,std::hypot(x,y));
        }
        const auto response=forward_aim_response_curve({output.right_x,output.right_y},config.aim_response_curve);
        x += (axis ? 0:velocity)*.001f-response.x;
        y += (axis ? velocity:0)*.001f+response.y;
        now+=.001;
    }
    return result;
}

void incident(const native_test::TestContext& context) {
    using controller_native::AimResponseCurveAlgorithm;
    std::ofstream out(context.artifact_path("bodylock_motion_anchor.json"));
    out << "{\"fidelity\":\"native closed loop, known synthetic camera gain and rigid pixel motion, semantic point jitter; no real-game replay\",\"seeds\":[20261001,8675309],\"cases\":[";
    int count=0,failed=0,invalid=0,failed_controls=0;
    float worst=0;
    for (unsigned seed : {20261001u,8675309u}) for (int index=0;index<128;++index)
    for (int duration : {600,2400}) {
        std::mt19937 rng(seed+index*1009+duration);
        const int axis=index%2;
        const int vision_ms=std::array<int,3>{5,8,16}[rng()%3];
        const float speed=std::array<float,5>{0,40,-40,100,-100}[rng()%5];
        const float manual=std::array<float,3>{0,.03f,-.03f}[rng()%3];
        const float noise=1.5f+(rng()%251)*.01f;
        const auto curve=(index/2)%2 ? AimResponseCurveAlgorithm::CodDynamicLegacyLut : AimResponseCurveAlgorithm::Linear;
        for (int control : {0,1}) {
            const auto r=run(seed+index*17,axis,duration,vision_ms,control ? 0:noise,speed,manual,curve);
            const bool trigger=r.body_ticks>=duration-450 && r.motion_ticks>=duration-450 &&
                r.identity && r.finite && (control || r.geometry_changes>=10);
            const bool pass=trigger && r.maximum_motion_error<=.002f;
            invalid+=!trigger;
            failed+=!pass;
            failed_controls+=control && !pass;
            worst=std::max(worst,r.maximum_motion_error);
            out << (count++?",":"") << "{\"seed\":"<<seed<<",\"case\":"<<index<<",\"duration_ms\":"<<duration
                <<",\"axis\":"<<axis<<",\"vision_ms\":"<<vision_ms<<",\"speed\":"<<speed
                <<",\"manual\":"<<manual<<",\"noise\":"<<(control ? 0:noise)<<",\"control\":"<<control
                <<",\"curve\":"<<static_cast<int>(curve)<<",\"body_ticks\":"<<r.body_ticks
                <<",\"motion_ticks\":"<<r.motion_ticks<<",\"geometry_changes\":"<<r.geometry_changes
                <<",\"maximum_motion_error\":"<<r.maximum_motion_error<<",\"maximum_request\":"<<r.maximum_request
                <<",\"maximum_world_error\":"<<r.maximum_world_error<<",\"trigger\":"<<trigger<<",\"pass\":"<<pass<<"}";
        }
    }
    out << "],\"count\":"<<count<<",\"failed\":"<<failed<<",\"invalid\":"<<invalid
        <<",\"failed_controls\":"<<failed_controls<<",\"maximum_motion_error\":"<<worst<<"}";
    out.close();
    if (invalid) throw native_test::InvalidFixtureError("motion/source geometry fixture did not exercise the declared native observer and stable identity");
    if (failed) throw std::runtime_error("semantic box-point jitter must not become rigid target motion when independent supported pixel displacement is available");
}

// Independent validation covariates are frozen separately from the original
// RED fixture. Valid correspondence tests learning-on geometry noise; the
// negative controls have quiet semantic geometry but invalid/jumped features.
void learning_and_correspondence_validation(const native_test::TestContext& context) {
    using controller_native::AimResponseCurveAlgorithm;
    std::ofstream out(context.artifact_path("motion_anchor_learning_and_domains.json"));
    out << "{\"seeds\":[10012026,3141592653],\"learning_enabled\":true,\"cases\":[";
    int count=0,failed=0,invalid=0;
    for (unsigned seed : {10012026u,3141592653u}) for (int index=0;index<64;++index)
    for (int duration : {600,2400}) {
        std::mt19937 rng(seed+index*101+duration);
        const int axis=index%2;
        const int kind=(index/4)%5;
        const int vision_ms=std::array<int,3>{5,8,16}[rng()%3];
        const float speed=std::array<float,5>{0,40,-40,100,-100}[rng()%5];
        const float manual=std::array<float,3>{0,.03f,-.03f}[rng()%3];
        const float noise=kind ? 0 : 1.5f+(rng()%251)*.01f;
        const auto curve=(index/2)%2 ? AimResponseCurveAlgorithm::CodDynamicLegacyLut : AimResponseCurveAlgorithm::Linear;
        const auto r=run(seed+index*19,axis,duration,vision_ms,noise,speed,manual,curve,true,kind);
        const bool trigger=r.body_ticks>=duration-450 && r.motion_ticks>=duration-450 && r.identity && r.finite && (kind || r.geometry_changes>=10);
        const bool pass=trigger && r.maximum_motion_error<=.002f;
        invalid+=!trigger; failed+=!pass;
        out << (count++?",":"") << "{\"seed\":"<<seed<<",\"case\":"<<index<<",\"axis\":"<<axis
            <<",\"kind\":"<<kind<<",\"duration_ms\":"<<duration<<",\"vision_ms\":"<<vision_ms
            <<",\"curve\":"<<static_cast<int>(curve)<<",\"speed\":"<<speed<<",\"manual\":"<<manual<<",\"noise\":"<<noise
            <<",\"body_ticks\":"<<r.body_ticks<<",\"motion_ticks\":"<<r.motion_ticks
            <<",\"maximum_motion_error\":"<<r.maximum_motion_error<<",\"maximum_world_error\":"<<r.maximum_world_error
            <<",\"trigger\":"<<trigger<<",\"pass\":"<<pass<<"}";
    }
    out << "],\"count\":"<<count<<",\"failed\":"<<failed<<",\"invalid\":"<<invalid<<"}";
    out.close();
    if (invalid) throw native_test::InvalidFixtureError("learning/domain validation did not exercise stable native observer ownership");
    if (failed) throw std::runtime_error("plant learning must share the rigid measurement source; invalid/rebuilt features cannot cross measurement domains");
}
}
void register_bodylock_motion_anchor_incident_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock","bodylock_semantic_point_false_motion",incident);
    registry.add_context_case("BaseBodyLock","motion_anchor_learning_and_domains",learning_and_correspondence_validation);
}
