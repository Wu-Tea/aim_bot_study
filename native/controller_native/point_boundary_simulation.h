#pragma once

#include "aim_dynamics_shaper.h"
#include "bodylock_target_motion_observer.h"
#include "response_model_aim_solver.h"
#include "test_support/native_test_registry.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>

// Diagnostic experiment, not a release gate or a replay of the full runtime.
// Single fixed identity, one horizontal axis, linear calibrated plant, no
// manual input/recoil/selector/authority transitions. Only the plant and sensor
// are synthetic; solver, motion observer, shaper and deadzone are production.
inline void simulate_point_boundary(const native_test::TestContext& context) {
    using namespace controller_native;
    std::ofstream boundary(context.artifact_path("point-boundary.csv"));
    std::ofstream summary(context.artifact_path("point-loop-summary.csv"));
    std::ofstream trace(context.artifact_path("point-loop-trace.csv"));
    if (!boundary || !summary || !trace) throw std::runtime_error("simulation artifacts unavailable");
    boundary << "radius_px,floor,error_px,requested\n";
    summary << "seed,profile,motion,delay_ms,duration_ms,variant,noise_px,gain,mean_abs_error_px,rms_error_px,peak_error_px,output_zero_edges,output_reversals,output_total_variation\n";
    trace << "seed,profile,motion,delay_ms,duration_ms,variant,t_ms,target_px,camera_px,true_error_px,observed_error_px,requested,output,motion_estimate_px_s\n";
    for (int profile=0; profile<2; ++profile) {
        ResponseModelAimRequest r;
        r.response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        r.range_position_response=true; r.position_range_px=150;
        r.minimum_position_stick=profile ? .30f : .20f;
        r.arrival_radius_px=profile ? 12.f : 2.f;
        r.response_px_per_stick_second=1600;
        r.arrival_horizon_seconds=profile ? .06f : .04f;
        r.max_force={.8f,.8f};
        for (float offset : {-0.1f,-0.01f,0.f,.01f,.1f}) {
            r.error_px={r.arrival_radius_px+offset,0};
            boundary << r.arrival_radius_px << ',' << r.minimum_position_stick << ',' << r.error_px.x << ',' << solve_response_model_aim(r).stick.x << '\n';
        }
    }
    // Independent deterministic seeds; each ablation gets exactly the same
    // capture schedule, noise stream and plant. Metrics exclude first 500 ms.
    for (unsigned seed : {1062026u, 917331u})
    for (int profile=0; profile<2; ++profile)
    for (int motion=0; motion<4; ++motion)
    for (int delay : {0,8,20})
    for (int duration : {2000,12000})
    for (int variant=0; variant<2; ++variant) {
        std::mt19937 rng(seed+profile*100+motion*10+delay);
        std::uniform_real_distribution<float> noise(-1.f,1.f);
        const float noise_px=motion==0 ? 0.f : (seed==1062026u ? .6f : 1.2f);
        const float gain=seed==1062026u ? 1600.f : 1100.f;
        ResponseModelAimRequest request;
        request.response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        request.range_position_response=true; request.position_range_px=150;
        request.minimum_position_stick=variant ? 0.f : (profile ? .30f : .20f);
        request.arrival_radius_px=profile ? 12.f : 2.f;
        request.arrival_horizon_seconds=profile ? .06f : .04f;
        request.response_px_per_stick_second=gain;
        request.max_force={.8f,.8f};
        AimDynamicsShaper shaper;
        BodylockTargetMotionObserver observer;
        pipeline_contract::TargetPlan plan;
        plan.target_id=1; plan.mode=pipeline_contract::ControlMode::BodyLockFollow;
        plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
        plan.aim_authority=plan.reliability=1;
        struct Frame { int tick; float error; float camera; };
        std::deque<Frame> pending;
        std::vector<float> output_history(duration,0);
        float camera=0, observed=0, previous_observed=0, previous_camera=0;
        float previous_output=0, previous_nonzero=0;
        int next_frame=0, prior_capture=-1, samples=0, zero_edges=0, reversals=0;
        double absolute=0, square=0, variation=0; float peak=0;
        for (int tick=0; tick<duration; ++tick) {
            const float applied=tick>delay ? output_history[tick-delay-1] : 0;
            camera+=applied*gain*.001f;
            const float seconds=tick*.001f;
            // Static clean/noisy, constant 90 px/s, then alternating +/-90
            // px/s every 750 ms. Target schedule never depends on controller.
            float target=30;
            if (motion==2) target+=90*seconds;
            if (motion==3) { const float phase=std::fmod(seconds,1.5f); target+=90*(phase<.75f ? phase : 1.5f-phase); }
            const float error=target-camera;
            if (tick*240>=next_frame*1000) {
                pending.push_back({tick,error+noise(rng)*noise_px,camera}); ++next_frame;
            }
            // Four milliseconds capture-to-controller latency, independently
            // of the actuator delay. No duplicate tick becomes a new image.
            while (!pending.empty() && pending.front().tick+4<=tick) {
                const auto frame=pending.front(); pending.pop_front(); observed=frame.error;
                if (prior_capture>=0) {
                    const float dt=(frame.tick-prior_capture)*.001f;
                    observer.update({1,1.+frame.tick*.001,dt,
                        {(observed-previous_observed)/dt,0},
                        {(frame.camera-previous_camera)/(gain*dt),0},gain,1,true});
                }
                prior_capture=frame.tick; previous_observed=observed; previous_camera=frame.camera;
            }
            const auto estimate=observer.estimate(1,1.+tick*.001);
            request.error_px={observed,0};
            request.motion_is_sustaining_target_motion=estimate.valid;
            request.relative_velocity_px_per_sec={estimate.valid ? estimate.target_motion_stick.x*gain : 0,0};
            const auto requested=solve_response_model_aim(request).stick;
            const auto output=filter_ai_input(shaper.shape(requested,{},plan,.001f),.03f);
            if (!std::isfinite(output.x) || std::abs(output.x)>.80001f)
                throw std::runtime_error("point simulation violated finite force envelope");
            output_history[tick]=output.x;
            if (tick>=500) {
                ++samples; absolute+=std::abs(error); square+=error*error; peak=std::max(peak,std::abs(error));
                zero_edges+=(output.x==0)!=(previous_output==0);
                if (output.x!=0) { reversals+=previous_nonzero*output.x<0; previous_nonzero=output.x; }
                variation+=std::abs(output.x-previous_output);
            }
            previous_output=output.x;
            if (seed==1062026u && duration==2000 && delay==8)
                trace << seed << ',' << profile << ',' << motion << ',' << delay << ',' << duration << ',' << variant << ',' << tick << ',' << target << ',' << camera << ',' << error << ',' << observed << ',' << requested.x << ',' << output.x << ',' << request.relative_velocity_px_per_sec.x << '\n';
        }
        summary << seed << ',' << profile << ',' << motion << ',' << delay << ',' << duration << ',' << variant << ',' << noise_px << ',' << gain << ',' << absolute/samples << ',' << std::sqrt(square/samples) << ',' << peak << ',' << zero_edges << ',' << reversals << ',' << variation << '\n';
    }
}
