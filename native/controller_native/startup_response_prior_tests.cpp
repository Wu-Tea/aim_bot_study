#include "ads_acquisition_controller.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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
            "prior must reduce demand without shortening the BodyLock horizon");
    require(std::fabs(next.second-650)<1e-4f,"configured prior must reach native plan");
    seeded.ai_aim.adapter_direct_mouse_manual=true;
    const auto mouse=run(seeded);
    require(std::fabs(mouse.second-500)<1e-4f,"gamepad prior must not change mouse calibration");
}
}

void register_startup_response_prior_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock","cold_response_prior_before_learning",cold_prior);
    registry.add_case("BaseBodyLock","gamepad_prior_preserves_horizon_and_mouse",native_prior);
}
