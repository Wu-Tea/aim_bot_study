#include "incident_fixture_support.h"
#include "output_composer.h"
#include "test_support/native_test_registry.h"
#include <cmath>
#include <fstream>
#include <stdexcept>

// OPEN diagnostic RED. This probe proves ADS calibration starvation after
// acquisition. Extending learning naively failed the paired product matrix;
// it is intentionally excluded from ordinary GREEN product suites.
namespace {
using namespace controller_native;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void held_ads_learning_survives_bodylock(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("held_ads_learning.json"));
    report << "{\"fidelity\":\"native held-ADS lifecycle with scripted camera-only delivery receipts; synthetic known plant, no live acceptance\",\"cases\":[";
    int count=0, failures=0;
    for (int hz : {100,160,200,250,320,500,1000}) for (int axis : {0,1})
        for (int control : {0,1,2}) {
            auto config=incident_fixture::base_config(100,200);
            config.ai_aim.ads_completion_fresh_frames=1;
            config.ai_aim.aim_response_effect_delay_ms=0;
            config.ai_aim.body_free_initial_scale=1000;
            config.ai_aim.body_slow_initial_scale=1000;
            config.ai_aim.ads_free_initial_scale=1000;
            config.ai_aim.ads_slow_initial_scale=1000;
            config.ai_aim.aim_response_learning_enabled=control!=1;
            config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
            double now=10;
            NativeGamepadController controller(config,&now);
            incident_fixture::TargetSpec target;
            target.observation_id=target.selector_generation=1;
            target.has_enemy_cue=true;
            float error=0, delivered=0;
            int body=0;
            for (int frame=0;frame<2*hz;++frame) {
                now=10+static_cast<double>(frame)/hz;
                error-=delivered*1800/hz;
                controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                    target,frame+1,now,axis?0:error,axis?error:0));
                controller.begin_tick(incident_fixture::ads_input(0,0,control==2));
                OutputComposer composer;
                require(composer.compose(controller.resolve_control_frame())==OutputComposeStatus::Ok,
                    "held ADS probe must use the real output composer");
                controller.observe_composed_output(*composer.finalized_output());
                body+=controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow;
                delivered=static_cast<int>(frame*50.0/hz)%2 ? -.15f:.15f;
                GamepadOutputState receipt;
                receipt.right_x=axis?0:delivered;receipt.right_y=axis?-delivered:0;
                controller.observe_delivered_output(receipt,true,now);
            }
            const auto learning=controller.learning_snapshot();
            const auto& ads=learning[3];
            const bool learned=ads.accepted_samples>=8 && ads.confidence>.35f &&
                std::fabs(ads.scale_px_per_stick_second-1800)<180;
            const bool blocked=ads.accepted_samples==0 && ads.confidence==0 &&
                std::fabs(ads.scale_px_per_stick_second-1000)<.001f;
            const bool pass=body>hz && (control==0 ? learned : blocked);
            failures+=!pass;
            report << (count++?",":"") << "{\"hz\":"<<hz<<",\"axis\":"<<axis
                <<",\"control\":"<<control<<",\"body_ticks\":"<<body
                <<",\"ads_samples\":"<<ads.accepted_samples<<",\"ads_confidence\":"<<ads.confidence
                <<",\"ads_response\":"<<ads.scale_px_per_stick_second<<",\"pass\":"<<pass<<"}";
        }
    report << "],\"count\":"<<count<<",\"failures\":"<<failures<<"}";
    report.close();
    require(failures==0,"physical held ADS must keep learning after acquisition, while disabled/firing evidence stays rejected");
}
}
int main(int argc, char** argv) {
    native_test::Registry registry;
    registry.add_context_case("OpenIncident", "held_ads_learning_survives_bodylock", held_ads_learning_survives_bodylock);
    return native_test::run(registry, argc, argv, "HeldAdsResponseLearningIncident");
}
