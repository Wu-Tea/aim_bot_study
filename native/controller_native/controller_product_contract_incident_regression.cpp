#include "assist_control_state_machine.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace {

using controller_native::AssistControlStateMachine;
using controller_native::AssistControlStateMachineInput;
using controller_native::NativeGamepadController;
using controller_native::incident_fixture::TargetSpec;

struct AdsSample {
    float aim_authority = 0.0f;
    float requested_magnitude = 0.0f;
};

AdsSample ads_sample(bool has_enemy_cue) {
    auto config = controller_native::incident_fixture::base_config(1000.0f, 180.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;

    double now = 120.0;
    NativeGamepadController controller(config, &now);
    TargetSpec target;
    target.observation_id = has_enemy_cue ? 9102 : 9101;
    target.selector_generation = has_enemy_cue ? 102 : 101;
    target.color_classified = true;
    target.has_enemy_cue = has_enemy_cue;
    target.enemy_identity_confirmed = has_enemy_cue;
    controller.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 1, now, 83.0f, -18.0f, true));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    const auto& requested =
        controller.last_output_components().requested_assist_stick;
    return {
        controller.last_target_plan().aim_authority,
        std::hypot(requested.x, requested.y),
    };
}

struct FusionSample {
    pipeline_contract::Vec2f mixed_axis{};
    pipeline_contract::Vec2f opposing_ads{};
};

FusionSample fusion_sample() {
    AssistControlStateMachine mixed_state;
    AssistControlStateMachineInput mixed;
    mixed.aiming = true;
    mixed.target_authoritative = true;
    mixed.target_id = 1;
    mixed.selector_target_generation = 1;
    mixed.mode = pipeline_contract::ControlMode::BodyLockFollow;
    mixed.visual_authority = 1.0f;
    mixed.firing = true;
    mixed.manual_stick = {0.18f, -0.12f};
    mixed.filtered_manual_stick = mixed.manual_stick;
    mixed.ai_stick = {0.45f, 0.20f};

    AssistControlStateMachine opposing_state;
    AssistControlStateMachineInput opposing = mixed;
    opposing.target_id = 2;
    opposing.selector_target_generation = 2;
    opposing.mode = pipeline_contract::ControlMode::AdsAcquire;
    opposing.firing = false;
    opposing.manual_stick = {0.18f, 0.0f};
    opposing.filtered_manual_stick = opposing.manual_stick;
    opposing.ai_stick = {-0.60f, 0.0f};
    return {
        mixed_state.update(mixed).stick,
        opposing_state.update(opposing).stick,
    };
}

struct DesiredPointSample {
    bool manual_correction_y = false;
    float before_y = 0.0f;
    float after_y = 0.0f;
};

DesiredPointSample firing_down_sample() {
    auto config = controller_native::incident_fixture::base_config(1000.0f, 180.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.desired_point_traversal_ms = 100.0f;
    double now = 140.0;
    NativeGamepadController controller(config, &now);
    TargetSpec target;
    target.observation_id = 9201;
    target.selector_generation = 201;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;
    controller.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 1, now, 0.0f, 0.0f, true));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    const float before_y = controller.last_target_plan().aim_px.y;

    now += 0.050;
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input(0.0f, -0.80f, true));
    const auto& after = controller.last_target_plan();
    return {after.manual_correction_y, before_y, after.aim_px.y};
}

}  // namespace

int run_controller_product_contract_incident_regression(int argc, char** argv) {
    try {
        const std::filesystem::path output_path =
            controller_native::incident_fixture::output_path_from_args(argc, argv);
        const AdsSample no_cue = ads_sample(false);
        const AdsSample cue = ads_sample(true);
        const FusionSample fusion = fusion_sample();
        const DesiredPointSample desired = firing_down_sample();

        constexpr float kEpsilon = 1.0e-4f;
        const bool ads_full_without_cue = no_cue.aim_authority >= 0.999f;
        const bool ads_full_with_cue = cue.aim_authority >= 0.999f;
        const bool cue_does_not_change_ads_gain =
            std::fabs(no_cue.aim_authority - cue.aim_authority) <= kEpsilon &&
            std::fabs(no_cue.requested_magnitude - cue.requested_magnitude) <=
                0.002f;
        const bool firing_down_is_not_opposed =
            fusion.mixed_axis.y <= -0.12f + kEpsilon;
        const bool compatible_axis_still_gets_fill =
            fusion.mixed_axis.x > 0.18f + kEpsilon;
        const bool ads_owns_wrong_manual =
            std::fabs(fusion.opposing_ads.x + 0.60f) <= kEpsilon;
        const bool firing_down_updates_d =
            desired.manual_correction_y &&
            desired.after_y > desired.before_y + 0.5f;
        const bool pass = ads_full_without_cue && ads_full_with_cue &&
            cue_does_not_change_ads_gain && firing_down_is_not_opposed &&
            compatible_axis_still_gets_fill && ads_owns_wrong_manual &&
            firing_down_updates_d;

        auto report = controller_native::incident_fixture::open_report(output_path);
        report << std::fixed << std::setprecision(6)
               << "{\n"
               << "  \"schema_version\": 1,\n"
               << "  \"incident_id\": \"controller-v1-product-contract\",\n"
               << "  \"samples\": {\n"
               << "    \"ads_no_cue\": {\"aim_authority\": "
               << no_cue.aim_authority << ", \"requested_magnitude\": "
               << no_cue.requested_magnitude << "},\n"
               << "    \"ads_cue\": {\"aim_authority\": "
               << cue.aim_authority << ", \"requested_magnitude\": "
               << cue.requested_magnitude << "},\n"
               << "    \"mixed_axis_output\": {\"x\": "
               << fusion.mixed_axis.x << ", \"y\": "
               << fusion.mixed_axis.y << "},\n"
               << "    \"opposing_ads_output\": {\"x\": "
               << fusion.opposing_ads.x << ", \"y\": "
               << fusion.opposing_ads.y << "},\n"
               << "    \"firing_down_d\": {\"manual_correction_y\": "
               << (desired.manual_correction_y ? "true" : "false")
               << ", \"before_y\": " << desired.before_y
               << ", \"after_y\": " << desired.after_y << "}\n"
               << "  },\n"
               << "  \"oracles\": {\n"
               << "    \"ads_full_without_cue\": "
               << (ads_full_without_cue ? "true" : "false") << ",\n"
               << "    \"ads_full_with_cue\": "
               << (ads_full_with_cue ? "true" : "false") << ",\n"
               << "    \"cue_does_not_change_ads_gain\": "
               << (cue_does_not_change_ads_gain ? "true" : "false") << ",\n"
               << "    \"firing_down_is_not_opposed\": "
               << (firing_down_is_not_opposed ? "true" : "false") << ",\n"
               << "    \"compatible_axis_still_gets_fill\": "
               << (compatible_axis_still_gets_fill ? "true" : "false") << ",\n"
               << "    \"ads_owns_wrong_manual\": "
               << (ads_owns_wrong_manual ? "true" : "false") << ",\n"
               << "    \"firing_down_updates_d\": "
               << (firing_down_updates_d ? "true" : "false") << "\n"
               << "  },\n"
               << "  \"pass\": " << (pass ? "true" : "false") << "\n"
               << "}\n";
        report.close();
        std::cout << "controller V1 product contract: "
                  << (pass ? "GREEN" : "RED")
                  << " report=" << output_path.string() << '\n';
        return pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

void register_controller_product_contract_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseEndToEnd", "incident_controller_product_contract", "controller_product_contract_incident.json", run_controller_product_contract_incident_regression);
}
