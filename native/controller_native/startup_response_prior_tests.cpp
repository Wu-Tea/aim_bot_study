#include "ads_acquisition_controller.h"
#include "pipeline_contract/target_acquisition.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"
#include "ds4_output_report.h"
#include "output_composer.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace controller_native;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Frozen coordinates from the observed first handoff. This is a component
// command counterfactual, NOT a closed-loop replay of the user's game plant.
void cold_prior(const native_test::TestContext& context) {
    AdsAcquisitionController ads;
    BodylockFollowController body;
    float maximum_delta=0, default_delta=0;
    unsigned int cases=0;
    for (auto mode : {pipeline_contract::ControlMode::AdsAcquire,
                      pipeline_contract::ControlMode::BodyLockFollow}) {
        for (auto error : {pipeline_contract::Vec2f{-4.91666f,-25.4667f},
                           pipeline_contract::Vec2f{24.5833f,-11.6f},
                           pipeline_contract::Vec2f{-24.5833f,11.6f}}) {
            pipeline_contract::TargetPlan plan;
            plan.target_id=20;
            plan.mode=mode;
            plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
            plan.error_px=error;
            plan.aim_authority=plan.reliability=1;
            plan.response_scale=650;
            const auto compute=[&](const pipeline_contract::TargetPlan& p) {
                return mode==pipeline_contract::ControlMode::AdsAcquire
                    ? ads.compute(p,{},.001f) : body.compute(p,{},.001f);
            };
            const auto cold=compute(plan);
            plan.response_confidence=1;
            const auto measured=compute(plan);
            plan.response_scale=0;
            const auto unspecified=compute(plan);
            maximum_delta=std::max(maximum_delta,
                std::hypot(cold.x-measured.x,cold.y-measured.y));
            default_delta=std::max(default_delta,
                std::hypot(unspecified.x-measured.x,unspecified.y-measured.y));
            require(std::hypot(cold.x,cold.y)>.01f &&
                    std::hypot(measured.x,measured.y)>.01f,
                    "prior fixture must exercise material target control");
            ++cases;
        }
    }
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("cold_response_prior.json"));
    report << "{\"cases\":" << cases
           << ",\"maximum_cold_vs_measured_delta\":" << maximum_delta
           << ",\"default_response_counterfactual_delta\":" << default_delta
           << ",\"game_plant_replay\":false}\n";
    report.close();
    require(cases==6 && default_delta>.01f,
            "removing prior must change the counterfactual output");
    require(maximum_delta<=1e-6f,
            "untrained prior must not be replaced by a stronger fallback command");
}

void four_region_snapshot_priors() {
    const auto path = std::filesystem::temp_directory_path()/"cod_four_region_priors.toml";
    { std::ofstream file(path); file << "[gamepad.ai_aim]\naim_response_initial_scale=650\n"
        "body_free_initial_scale=1363.69\nbody_slow_initial_scale=1008.82\n"
        "ads_free_initial_scale=1656.79\nads_slow_initial_scale=1397.20\n"; }
    const auto loaded = load_runtime_config(path);
    std::filesystem::remove(path);
    require(loaded.diagnostics.empty(), "four region priors must be recognized configuration");
    const float expected[] = {1363.69f, 1008.82f, 1656.79f, 1397.20f};
    auto check = [&](const NativeGamepadController& controller) {
        const auto snapshot = controller.learning_snapshot();
        for (int index=0; index<4; ++index) {
            require(std::fabs(snapshot[index].scale_px_per_stick_second-expected[index])<.01f,
                "cold or cleared runtime must use each configured region prior");
            require(snapshot[index].confidence==0 && snapshot[index].accepted_samples==0,
                "imported priors must not invent sample evidence");
        }
    };
    NativeGamepadController controller(loaded.gamepad);
    check(controller);
    controller.clear_learning(); check(controller);
    controller.reset(); check(controller);
    auto active = loaded.gamepad;
    active.recoil.enabled = false;
    double now = 10;
    NativeGamepadController ads(active, &now);
    incident_fixture::TargetSpec spec;
    spec.observation_id = spec.selector_generation = 1;
    spec.has_enemy_cue = spec.enemy_identity_confirmed = true;
    for (unsigned frame=1; frame<=2; ++frame) {
        ads.submit_vision_snapshot(incident_fixture::observed_snapshot(spec, frame, now, 80, 0));
        ads.build_output(incident_fixture::ads_input()); now += .005;
    }
    const auto& plan = ads.last_target_plan();
    require(plan.mode==pipeline_contract::ControlMode::AdsAcquire, "prior fixture must exercise active ADS");
    const float weight = aim_response_slow_zone_weight(plan.error_px, plan.ads_target_size_px);
    require(std::fabs(plan.response_scale-(expected[2]+weight*(expected[3]-expected[2])))<.01f &&
        plan.response_confidence==0, "ADS control must consume its own cold priors without inventing confidence");
    const auto target_id = plan.target_id;
    require(target_id!=0, "active prior fixture must own a real target");
    active.ai_aim.ads_free_initial_scale = 1500;
    active.ai_aim.ads_slow_initial_scale = 1100;
    ads.apply_hot_config(active);
    ads.submit_vision_snapshot(incident_fixture::observed_snapshot(spec, 3, now, 80, 0));
    ads.build_output(incident_fixture::ads_input());
    const auto& reloaded = ads.last_target_plan();
    const float reloaded_weight = aim_response_slow_zone_weight(reloaded.error_px, reloaded.ads_target_size_px);
    require(reloaded.target_id==target_id && reloaded.mode==pipeline_contract::ControlMode::AdsAcquire,
        "changing priors must preserve active target and ADS lifecycle ownership");
    require(std::fabs(reloaded.response_scale-(1500+reloaded_weight*(1100-1500)))<.01f,
        "hot prior must reach the active plan on the next tick");
    auto released_input = incident_fixture::ads_input(.21f,-.13f);
    released_input.left_trigger = 0;
    const auto released = ads.build_output(released_input);
    require(released.right_x==released_input.right_x && released.right_y==released_input.right_y,
        "regional prior changes must preserve raw manual authority on release");
    auto changed = loaded.gamepad;
    changed.ai_aim = GamepadAiAimConfig{};
    controller.apply_hot_config(changed);
    for (const auto& region : controller.learning_snapshot())
        require(region.scale_px_per_stick_second==500 && region.confidence==0 && region.accepted_samples==0,
            "hot reload must replace priors and clear old evidence");
    controller.apply_hot_config(loaded.gamepad); check(controller);
    changed = loaded.gamepad;
    changed.ai_aim.adapter_direct_mouse_manual = true;
    NativeGamepadController mouse(changed);
    for (const auto& region : mouse.learning_snapshot())
        require(region.scale_px_per_stick_second==500, "gamepad region priors must not change mouse calibration");
}

void native_prior() {
    const auto path=std::filesystem::temp_directory_path()/"cod_startup_prior_test.toml";
    { std::ofstream file(path); file << "[gamepad.ai_aim]\naim_response_initial_scale = 650\n"; }
    auto seeded=load_runtime_config(path).gamepad;
    std::filesystem::remove(path);
    auto run=[](GamepadRuntimeConfig config) {
        config.ai_aim.aim_response_learning_enabled=false;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.recoil.enabled=false;
        config.ai_aim.body_lock_max_ai_force=.8f;
        config.ai_aim.body_lock_max_ai_force_y=.6f;
        config.ai_aim.body_lock_box_tolerance_px=8;
        config.ai_aim.body_lock_feedback_distance_px=0; // Exercise the legacy in-memory path.
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.bodylock_output_limit_x=.8f;
        config.ai_aim.bodylock_output_limit_y=.6f;
        config.ai_aim.bodylock_response_time_x_ms=45.f;
        config.ai_aim.bodylock_response_time_y_ms=60.f;
        config.tracker.max_observation_age_ms=1000;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=20; spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        for (unsigned int tick=1;tick<=4;++tick) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec,tick,now,tick<4 ? 0.f : -4.91666f,tick<4 ? 0.f : -11.6f));
            (void)controller.build_output(incident_fixture::ads_input());
            now+=.005;
        }
        require(controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow,
                "fixture must pass ADS admission and enter BodyLock");
        require(controller.last_target_plan().response_confidence==0,
                "prior must not invent learned confidence");
        const float position=controller.last_output_components().bodylock_position_stick.x;
        const float response=controller.last_target_plan().response_scale;
        auto physical=incident_fixture::ads_input(.012f,-.012f); physical.left_trigger=0;
        const auto output=controller.build_output(physical);
        require(output.right_x==physical.right_x && output.right_y==physical.right_y,
                "prior must not alter physical passthrough at release");
        return std::pair<float,float>{position,response};
    };
    auto baseline=seeded;
    baseline.ai_aim=GamepadAiAimConfig{};
    const auto old=run(baseline), next=run(seeded);
    require(std::fabs(old.first)>.01f && std::fabs(next.first)>.01f,
            "position counterfactual must be nonzero");
    require(std::fabs(next.first/old.first-500.f/650.f)<1e-4f,
            "prior must reduce planning demand without shortening the BodyLock horizon");
    require(std::fabs(next.second-650)<1e-4f,"configured prior must reach native plan");
    seeded.ai_aim.adapter_direct_mouse_manual=true;
    const auto mouse=run(seeded);
    require(std::fabs(mouse.second-500)<1e-4f,"gamepad prior must not change mouse calibration");
}

void direct_feedback_distance_reaches_native_chain() {
    auto run=[](float distance) {
        GamepadRuntimeConfig config;
        config.ai_aim.body_lock_box_tolerance_px=8;
        config.ai_aim.body_lock_feedback_distance_px=distance;
        config.ai_aim.aim_response_learning_enabled=false;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        config.recoil.enabled=false;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        for (unsigned frame=1;frame<=4;++frame) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,frame<4 ? 0.f : .5f,0));
            controller.build_output(incident_fixture::ads_input());now+=.005;
        }
        require(controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow,
                "direct feedback regression must enter the production BodyLock chain");
        const float demand=controller.last_output_components().bodylock_position_stick.x;
        const auto identity=controller.last_target_plan().target_id;
        config.ai_aim.body_lock_max_ai_force=.2f;
        config.ai_aim.body_lock_max_ai_force_y=.25f;
        controller.apply_hot_config(config,true);
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,5,now,.5f,0));
        controller.build_output(incident_fixture::ads_input());now+=.005;
        require(controller.last_target_plan().target_id==identity &&
                controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow,
                "hot force tuning must retain identity and lifecycle");
        auto physical=incident_fixture::ads_input(.021f,-.018f);physical.left_trigger=0;
        const auto raw=controller.build_output(physical);
        require(raw.right_x==physical.right_x && raw.right_y==physical.right_y,
                "aggressive response must retain raw manual passthrough on release");
        return demand;
    };
    const float legacy=run(0),equivalent=run(18),fine=run(1);
    require(std::fabs(legacy-equivalent)<1e-6f,
            "old 8 and direct 18 must produce identical native position demand");
    require(std::fabs(fine/legacy-18)<1e-3f,
            "one pixel must reach the actual controller, not just config and preview");
}

void undelivered_commands_cannot_train_motion() {
    auto run=[](bool deliver) {
        auto config=incident_fixture::base_config(100,200);
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.aim_response_effect_delay_ms=0;
        config.ai_aim.aim_response_learning_enabled=false;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=1; spec.selector_generation=1;
        spec.has_enemy_cue=true;
        unsigned observed=0;
        for (unsigned frame=1;frame<=20;++frame) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec,frame,now,frame<4?0.f:12.f,0));
            if (deliver) controller.build_output(incident_fixture::ads_input());
            else {
                controller.begin_tick(incident_fixture::ads_input());
                (void)controller.resolve_control_frame();
            }
            observed+=controller.last_target_plan().bodylock_target_motion_valid;
            now+=.010;
        }
        require(controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow,
            "delivery fixture must enter BodyLock with direct fresh observations");
        return observed;
    };
    require(run(true)>10,"delivered control must establish sustained motion evidence");
    require(run(false)==0,"unpublished AI proposals cannot become delivered camera work");
}

void delivery_receipt_owns_camera_coordinate_and_failure_boundary() {
    auto config=incident_fixture::base_config(100,200);
    config.ai_aim.ads_completion_fresh_frames=1;
    config.ai_aim.aim_response_effect_delay_ms=0;
    config.ai_aim.aim_response_learning_enabled=false;
    double now=20;
    NativeGamepadController controller(config,&now);
    incident_fixture::TargetSpec spec;
    spec.observation_id=1; spec.selector_generation=1;
    spec.has_enemy_cue=true;
    GamepadOutputState receipt;
    receipt.right_x=ds4_axis_value(ds4_axis(.123f));
    receipt.right_y=-ds4_axis_value(ds4_axis(.207f));
    unsigned valid=0;
    for (unsigned frame=1;frame<=50;++frame) {
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,0,0));
        controller.begin_tick(incident_fixture::ads_input());
        auto commands=controller.resolve_control_frame();
        OutputComposer composer;
        require(composer.compose(commands)==OutputComposeStatus::Ok,"receipt fixture must compose");
        controller.observe_composed_output(*composer.finalized_output());
        const auto& plan=controller.last_target_plan();
        if (frame>10 && frame<=20) {
            require(plan.bodylock_target_motion_valid,"successful output must establish motion history");
            ++valid;
            require(std::fabs(plan.bodylock_target_motion_px_per_sec.x-receipt.right_x*500)<.01f &&
                std::fabs(plan.bodylock_target_motion_px_per_sec.y+receipt.right_y*500)<.01f,
                "model must consume acknowledged coordinates, not the controller proposal");
        }
        if (frame>21 && frame<=31) require(!plan.bodylock_target_motion_valid,
            "failed deliveries must not reuse previous camera-work evidence");
        if (frame==36) require(!plan.bodylock_target_motion_valid,
            "same-tick reconnect must break the interval before its first receipt");
        if (frame>40) require(plan.bodylock_target_motion_valid,
            "fresh successful receipts must restore motion evidence after reconnect");
        if (frame==35) controller.observe_delivered_output({},false,0);
        controller.observe_delivered_output(receipt,frame<=20 || frame>30,
            now+(frame==35 ? .005 : 0));
        now+=.010;
    }
    require(valid==10,"receipt test must exercise a sustained accepted interval");
}

void high_rate_source_keeps_camera_observation(const native_test::TestContext& context) {
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("high_rate_camera_observation.json"));
    report << "{\"cases\":[";
    bool first=true, all_valid=true;
    for(int hz : {100,160,200,250,320,500,1000}) for(int axis : {0,1}) {
        auto config=incident_fixture::base_config(100,200);
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.aim_response_effect_delay_ms=0;
        config.ai_aim.aim_response_learning_enabled=false;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=1; spec.selector_generation=1; spec.has_enemy_cue=true;
        int body=0, valid=0;
        float error=0,last=0,max_false_motion=0;
        for(int frame=0;frame<hz;++frame) {
            now=10+static_cast<double>(frame)/hz;
            error-=last*500/hz;
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                spec,frame+1,now,axis?0:error,axis?error:0));
            controller.begin_tick(incident_fixture::ads_input());
            OutputComposer composer;
            require(composer.compose(controller.resolve_control_frame())==OutputComposeStatus::Ok,
                "source replay must compose each frame");
            controller.observe_composed_output(*composer.finalized_output());
            const auto& plan=controller.last_target_plan();
            if(frame>hz/4) {
                body+=plan.mode==pipeline_contract::ControlMode::BodyLockFollow;
                valid+=plan.bodylock_target_motion_valid;
                if(plan.bodylock_target_motion_valid) max_false_motion=std::max(max_false_motion,
                    std::hypot(plan.bodylock_target_motion_px_per_sec.x,plan.bodylock_target_motion_px_per_sec.y));
            }
            last=.1f*std::sin(static_cast<float>(frame)*12.566370614f/hz);
            GamepadOutputState receipt;
            receipt.right_x=axis?0:last; receipt.right_y=axis?-last:0;
            controller.observe_delivered_output(receipt,true,now);
        }
        require(body>hz/2,"source replay must sustain BodyLock");
        all_valid=all_valid && valid==body && max_false_motion<.1f;
        report << (first?"":",") << "{\"hz\":"<<hz<<",\"axis\":"<<axis
            <<",\"body\":"<<body<<",\"valid\":"<<valid
            <<",\"false_motion_px_s\":"<<max_false_motion<<"}";
        first=false;
    }
    report << "]}"; report.close();
    require(all_valid,"high-rate fresh observations must retain stationary-world camera cancellation");
}

void high_rate_source_keeps_response_learning(const native_test::TestContext& context) {
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("high_rate_response_learning.json"));
    report << "{\"plant_source\":\"scripted_ack_probe\",\"cases\":[";
    bool first = true, all_valid = true;
    for (int hz : {100,160,200,250,320,500,1000}) {
        for (int axis : {0,1}) for (int control : {0,1,2}) {
            auto config = incident_fixture::base_config(100,200);
            config.ai_aim.ads_completion_fresh_frames = 1;
            config.ai_aim.aim_response_effect_delay_ms = 0;
            config.ai_aim.aim_response_initial_scale = 500;
            config.ai_aim.aim_response_learning_enabled = control != 1;
            double now = 10;
            NativeGamepadController controller(config, &now);
            incident_fixture::TargetSpec spec;
            spec.observation_id = spec.selector_generation = 1;
            spec.has_enemy_cue = true;
            float error = 0, last = 0;
            int body = 0;
            for (int frame = 0; frame < 2*hz; ++frame) {
                now = 10 + static_cast<double>(frame)/hz;
                error -= last*800/hz;
                controller.submit_vision_snapshot(incident_fixture::observed_snapshot(
                    spec, frame+1, now, axis ? 0:error, axis ? error:0));
                controller.begin_tick(incident_fixture::ads_input(0,0,control == 2));
                OutputComposer composer;
                require(composer.compose(controller.resolve_control_frame()) == OutputComposeStatus::Ok,
                    "learning replay must compose each frame");
                controller.observe_composed_output(*composer.finalized_output());
                body += controller.last_target_plan().mode == pipeline_contract::ControlMode::BodyLockFollow;
                // A bounded camera-only excitation; no target acceleration,
                // player input, recoil, noise, or unobserved plant latency.
                last = (static_cast<int>(frame*50.0/hz)%2) ? -.15f:.15f;
                GamepadOutputState receipt;
                receipt.right_x = axis ? 0:last;
                receipt.right_y = axis ? -last:0;
                controller.observe_delivered_output(receipt,true,now);
            }
            const auto& plan = controller.last_target_plan();
            require(body > hz,"learning replay must sustain the actual BodyLock path");
            const bool passes = control == 0
                ? plan.response_confidence > .5f && std::fabs(plan.response_scale-800) < 80
                : plan.response_confidence == 0 && std::fabs(plan.response_scale-500) < .001f;
            all_valid = all_valid && passes;
            report << (first ? "":",") << "{\"hz\":" << hz << ",\"axis\":" << axis
                << ",\"control\":" << control << ",\"body\":" << body
                << ",\"response\":" << plan.response_scale
                << ",\"confidence\":" << plan.response_confidence << "}";
            first = false;
        }
    }
    report << "]}"; report.close();
    require(all_valid,"high-rate learning must converge without bypassing disabled/fire ambiguity controls");
}

}

void register_startup_response_prior_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock","gap_uses_actual_delivery_and_never_becomes_observation", [] {
        GamepadRuntimeConfig config;config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.aim_response_effect_delay_ms=0;config.recoil.enabled=false;
        double now=10;NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        for(unsigned frame=1;frame<=8;++frame) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,20,0));
            controller.build_output(incident_fixture::ads_input());now+=.001;
        }
        const auto identity=controller.last_target_plan().target_id;
        ControllerVisionSnapshot miss;miss.frame_updated=true;miss.selector_identity_protocol=true;
        miss.selector_target_generation=1;miss.frame_id=9;miss.capture_time_seconds=now;
        miss.state.screen_center_x=320;miss.state.screen_center_y=256;
        controller.submit_vision_snapshot(miss);controller.build_output(incident_fixture::ads_input());
        require(controller.last_target_plan().observation_gap && controller.last_target_plan().target_id==identity,
            "production chain must bridge qualified same-person missing frames");
        require(!controller.last_frame_vision_state().fresh_observation &&
            !controller.last_frame_vision_state().current_observed_target_present &&
            !controller.last_target_plan().fire_authority,"predicted continuation cannot become a fresh measurement or fire authority");
        require(controller.last_output_components().requested_assist_stick.x>0,
            "brief miss cannot neutralize the existing target correction");
        now+=.033;controller.build_output(incident_fixture::ads_input());
        require(controller.last_target_plan().aim_authority==0,"production chain stops beyond original capture plus 32ms");
    });
    registry.add_case("BaseBodyLock","inside_box_is_not_point_arrival", [] {
        GamepadRuntimeConfig config;
        config.ai_aim.normalized_assist_parameters=true;config.ai_aim.arrival_radius_px=2;
        config.ai_aim.minimum_position_stick=.15f;config.ai_aim.ads_response_time_ms=350;
        config.ai_aim.ads_completion_fresh_frames=1;config.ai_aim.aim_response_learning_enabled=false;
        config.recoil.enabled=false;config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        double now=10;NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        for(unsigned frame=1;frame<=6;++frame) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,3,0));
            const auto output=controller.build_output(incident_fixture::ads_input());now+=.005;
            const auto& plan=controller.last_target_plan();
            require(plan.mode==pipeline_contract::ControlMode::AdsAcquire,
                "3 px is inside the box but outside the configured 2 px point arrival; ADS cannot finish early");
            require(controller.last_output_components().requested_assist_stick.x>0,
                "position solver must keep a continuous final approach outside the point radius");
            if(frame>=3) require(output.right_x>0,"after existing onset slew, composed output must keep advancing");
        }
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,7,now,0,0));
        controller.build_output(incident_fixture::ads_input());
        require(controller.last_output_components().bodylock_position_stick.x==0,
            "point arrival must stop position correction while observed motion remains separately owned");
    });

    registry.add_case("BaseBodyLock","real_plan_owns_stage_range_and_handover", [] {
        GamepadRuntimeConfig config;
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.ads_pickup_base_radius_px=73;
        config.ai_aim.body_lock_activation_box_px=227;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.aim_response_learning_enabled=false;
        config.ai_aim.ai_input_deadzone=0;
        config.recoil.enabled=false;
        config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::CodDynamicLegacyLut;
        double now=10;NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        bool saw_ads=false,saw_follow=false;std::uint64_t identity=0;
        for(unsigned frame=1;frame<=10;++frame) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,frame<4 ? 30.f : 0.f,0));
            controller.build_output(incident_fixture::ads_input());now+=.005;
            const auto& plan=controller.last_target_plan();
            if(plan.lifecycle==pipeline_contract::TargetLifecycle::None)continue;
            const bool ads=plan.mode==pipeline_contract::ControlMode::AdsAcquire;
            const float expected=pipeline_contract::target_scaled_pickup_radius(ads ? 73.f : 227.f,plan.normalized_size);
            require(std::abs(plan.position_response_radius_px-expected)<1e-4f,"stage radius must come from actual configured admission geometry");
            saw_ads|=ads;saw_follow|=!ads;
            if(identity)require(identity==plan.target_id,"changing phase must preserve selected identity");
            identity=plan.target_id;
        }
        require(saw_ads && saw_follow,"fixture must cross actual ADS to BodyLock transition");
        config.ai_aim.minimum_position_stick=.15f;config.ai_aim.bodylock_response_time_x_ms=1000;
        controller.apply_hot_config(config,true);
        controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,11,now,3,0));
        controller.build_output(incident_fixture::ads_input());
        require(controller.last_target_plan().error_px.x>2 &&
            controller.last_output_components().bodylock_position_stick.x>0,
            "being inside the person box must keep the position task active");


    });

    registry.add_case("BaseBodyLock","ai_deadzone_hot_reload_preserves_manual_and_recoil", [] {
        GamepadRuntimeConfig config;
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.arrival_radius_px=1;
        config.ai_aim.minimum_position_stick=0;
        config.ai_aim.aim_response_learning_enabled=false;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.bodylock_response_time_x_ms=200;
        config.ai_aim.bodylock_response_time_y_ms=200;
        config.recoil.enabled=true;config.recoil.output_amount=.2f;
        config.auto_fire.enabled=false;
        config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        auto physical=incident_fixture::ads_input(.012f,-.021f);
        auto tick=[&](unsigned frame,float error) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,error,0));
            const auto output=controller.build_output(physical);now+=.005;return output;
        };
        for(unsigned frame=1;frame<=3;++frame)tick(frame,0);
        const auto output=tick(4,1.5f);
        require(controller.last_output_components().requested_assist_stick.x>0 &&
            controller.last_output_components().shaped_assist_stick.x==0,
            "small real native AI request is filtered at the input boundary");
        require(output.right_x==physical.right_x && output.right_y==physical.right_y,
            "AI filtering must preserve tiny physical input exactly while aiming");
        const auto identity=controller.last_target_plan().target_id;
        config.ai_aim.ai_input_deadzone=0;controller.apply_hot_config(config,true);tick(5,1.5f);
        require(controller.last_output_components().shaped_assist_stick.x>0,
            "zero deadzone immediately admits the same request");
        config.ai_aim.ai_input_deadzone=1;controller.apply_hot_config(config,true);
        physical.right_trigger=1;
        const auto firing=tick(6,1.5f);
        require(std::abs(firing.right_y-(physical.right_y-.2f))<1e-6f,
            "independent recoil bypasses AI deadzone");
        require(controller.last_target_plan().target_id==identity,"filter changes cannot discard identity");
    });
    registry.add_case("BaseBodyLock","independent_parameters_hot_apply_keeps_identity", [] {
        GamepadRuntimeConfig config;
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.arrival_radius_px=1;
        config.ai_aim.minimum_position_stick=0;
        config.ai_aim.aim_response_learning_enabled=false;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.bodylock_output_limit_x=.8f;
        config.ai_aim.bodylock_output_limit_y=.8f;
        config.ai_aim.bodylock_response_time_x_ms=200;
        config.ai_aim.bodylock_response_time_y_ms=200;
        config.recoil.enabled=false;
        config.aim_response_curve.algorithm=AimResponseCurveAlgorithm::Linear;
        double now=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=20;spec.selector_generation=1;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        auto tick=[&](unsigned frame,float error) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec,frame,now,error,0));
            controller.build_output(incident_fixture::ads_input());now+=.005;
            return controller.last_output_components().bodylock_position_stick.x;
        };
        for(unsigned frame=1;frame<=3;++frame)tick(frame,0);
        const float before=tick(4,1.5f);
        const auto identity=controller.last_target_plan().target_id;
        require(std::abs(before)>1e-5f,"fixture must exercise actual position correction");
        config.ai_aim.bodylock_output_limit_x=.2f;
        controller.apply_hot_config(config,true);
        require(std::abs(tick(5,1.5f)-before)<1e-5f,"hot limit change must leave unsaturated position demand alone");
        config.ai_aim.bodylock_response_time_x_ms=100;
        controller.apply_hot_config(config,true);
        require(std::abs(tick(6,1.5f)-before*2)<1e-5f,"hot response time must change actual native demand");
        require(controller.last_target_plan().target_id==identity &&
            controller.last_target_plan().mode==pipeline_contract::ControlMode::BodyLockFollow,
            "hot control updates must preserve selected identity and lifecycle");
        auto physical=incident_fixture::ads_input(.012f,-.021f);physical.left_trigger=0;
        const auto raw=controller.build_output(physical);
        require(raw.right_x==physical.right_x && raw.right_y==physical.right_y,"release retains zero-deadzone manual passthrough");
    });
    registry.add_case("BaseBodyLock", "learning_pause_preserves_and_resumes_samples", [] {
        for (int axis : {0, 1}) {
            auto config = incident_fixture::base_config(100, 200);
            config.ai_aim.ads_completion_fresh_frames = 1;
            config.ai_aim.aim_response_effect_delay_ms = 0;
            config.ai_aim.aim_response_initial_scale = 500;
            double now = 10;
            NativeGamepadController controller(config, &now);
            incident_fixture::TargetSpec spec;
            spec.observation_id = spec.selector_generation = 1; spec.has_enemy_cue = true;
            float error = 0, last = 0; int frame = 0;
            const auto advance = [&](int count, float gain) {
                for (int i = 0; i < count; ++i, ++frame) {
                    now = 10 + frame / 160.0;
                    error -= last * gain / 160;
                    controller.submit_vision_snapshot(incident_fixture::observed_snapshot(spec, frame + 1, now,
                        axis ? 0 : error, axis ? error : 0));
                    controller.build_output(incident_fixture::ads_input());
                    last = static_cast<int>(frame * 50.0 / 160) % 2 ? -.15f : .15f;
                    GamepadOutputState receipt; receipt.right_x = axis ? 0 : last; receipt.right_y = axis ? -last : 0;
                    controller.observe_delivered_output(receipt, true, now);
                }
            };
            advance(320, 800);
            auto learned = controller.learning_snapshot();
            require(learned[0].accepted_samples + learned[1].accepted_samples > 10, "pause test must first establish real learned samples");
            config.ai_aim.aim_response_learning_enabled = false;
            controller.apply_hot_config(config, true);
            advance(160, 1000);
            auto paused = controller.learning_snapshot();
            for (int region = 0; region < 4; ++region) require(paused[region].accepted_samples == learned[region].accepted_samples &&
                paused[region].learned_scale == learned[region].learned_scale && paused[region].confidence == learned[region].confidence,
                "disabled learning must preserve every region without accepting samples");
            config.ai_aim.aim_response_learning_enabled = true;
            controller.apply_hot_config(config, true);
            advance(160, 800);
            auto resumed = controller.learning_snapshot();
            require(resumed[0].accepted_samples + resumed[1].accepted_samples > paused[0].accepted_samples + paused[1].accepted_samples,
                "re-enabled learning must resume actual native samples");
        }
    });
    registry.add_case("BaseBodyLock","four_region_snapshot_priors",four_region_snapshot_priors);
    registry.add_context_case("BaseBodyLock","high_rate_camera_observation",high_rate_source_keeps_camera_observation);
    registry.add_context_case("BaseBodyLock","high_rate_response_learning",high_rate_source_keeps_response_learning);
    registry.add_case("BaseBodyLock","unpublished_commands_cannot_train_motion",undelivered_commands_cannot_train_motion);
    registry.add_case("BaseBodyLock","delivery_receipt_coordinate_and_failure",delivery_receipt_owns_camera_coordinate_and_failure_boundary);
    registry.add_context_case("BaseBodyLock","cold_response_prior_before_learning",cold_prior);
    registry.add_case("BaseBodyLock","gamepad_prior_preserves_horizon_and_mouse",native_prior);
    registry.add_case("BaseBodyLock","direct_feedback_distance_reaches_native_chain",direct_feedback_distance_reaches_native_chain);
}
