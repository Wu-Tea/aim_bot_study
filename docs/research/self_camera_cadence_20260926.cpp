#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_dynamics_shaper.h"
#include "controller_native/aim_response_estimator.h"
#include "controller_native/ads_response_estimator.h"
#include "controller_native/output_composer.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// Diagnostic source replay, not a game-plant acceptance test. Every published
// camera command is known; the world target remains stationary. No jump,
// crouch, left-stick input, recoil, AA, detection noise or transport delay.
int main() {
    using namespace controller_native;
    bool first=true;
    std::cout << "{\"kind\":\"synthetic_known_camera_replay\",\"cases\":[";
    for (int hz : {100,160,200,250,320,500,1000}) for (int axis : {0,1}) {
        const double dt=1.0/hz;
        auto config=incident_fixture::base_config(100,200);
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.aim_response_effect_delay_ms=0;
        config.ai_aim.aim_response_learning_enabled=false;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=1; spec.selector_generation=1;
        spec.has_enemy_cue=true;
        float error=0, last_command=0, max_false_motion=0;
        int body=0, valid=0;
        AimResponseEstimator estimator;
        AdsResponseEstimator ads;
        for (int frame=0;frame<hz;++frame) {
            now=10+frame*dt;
            error-=last_command*500*static_cast<float>(dt);
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec,frame+1,now,axis==0?error:0,axis==1?error:0));
            controller.begin_tick(incident_fixture::ads_input());
            OutputComposer composer;
            composer.compose(controller.resolve_control_frame());
            controller.observe_composed_output(*composer.finalized_output());
            const auto& plan=controller.last_target_plan();
            if (frame>hz/4) {
                body+=plan.mode==pipeline_contract::ControlMode::BodyLockFollow;
                valid+=plan.bodylock_target_motion_valid;
                if (plan.bodylock_target_motion_valid) max_false_motion=std::max(
                    max_false_motion,std::hypot(plan.bodylock_target_motion_px_per_sec.x,
                                              plan.bodylock_target_motion_px_per_sec.y));
            }
            // Scripted consumer receipt: isolates knowledge of our own camera
            // from the controller's proposed correction, without a second solver.
            last_command=.1f*std::sin(static_cast<float>(frame*dt*12.566370614));
            GamepadOutputState receipt;
            receipt.right_x=axis==0?last_command:0;
            receipt.right_y=axis==1?-last_command:0;
            controller.observe_delivered_output(receipt,true,now);
            AimResponseInterval interval;
            const float u=frame%2?.1f:-.1f;
            interval.average_final_stick={axis==0?u:0,axis==1?-u:0};
            interval.observed_error_rate_px_per_sec={axis==0?-u*800:0,axis==1?-u*800:0};
            interval.target_id=1; interval.dt_seconds=static_cast<float>(dt);
            interval.reliability=1; interval.observed=true;
            estimator.update(interval); ads.update(interval);
        }
        if(body<hz/2) return 2;
        AimDynamicsShaper shaper;
        pipeline_contract::TargetPlan p;
        p.target_id=1; p.mode=pipeline_contract::ControlMode::BodyLockFollow;
        p.lifecycle=pipeline_contract::TargetLifecycle::Observed;
        shaper.shape({}, {}, p, static_cast<float>(dt));
        shaper.adopt({.8f,0});
        int ticks=0;
        while(shaper.current().x>1e-6f && ticks<hz) {
            shaper.shape({-.8f,0}, {}, p, static_cast<float>(dt)); ++ticks;
        }
        if(!first) std::cout << ','; first=false;
        std::cout << "{\"hz\":"<<hz<<",\"axis\":"<<axis
            <<",\"body_frames\":"<<body<<",\"valid_motion_frames\":"<<valid
            <<",\"false_motion_px_s\":"<<max_false_motion
            <<",\"response_samples\":"<<estimator.estimate().accepted_samples
            <<",\"ads_response_samples\":"<<ads.estimate().accepted_samples
            <<",\"shaper_zero_ms\":"<<ticks*dt*1000<<"}";
    }
    std::cout << "]}\n";
}
