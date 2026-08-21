#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "close-cue-hold-authority-20260817";
constexpr float kCloseBodyHeightPx = 280.0f;
constexpr float kFarBodyHeightPx = 60.0f;
constexpr float kAxisErrorPx = 36.0f;
constexpr float kMinimumCloseRequestedRetention = 0.90f;
constexpr float kMinimumCloseShapedRetention = 0.85f;
constexpr float kMaximumFarRequestedRetention = 0.50f;
constexpr std::uint64_t kSelectorGeneration = 1701;

struct AxisRun {
    bool trigger_executed = false;
    bool cue_remained_aim_only = false;
    std::uint64_t target_id = 0;
    float normalized_target_height = 0.0f;
    float direct_authority = 0.0f;
    float cue_authority = 0.0f;
    float direct_requested = 0.0f;
    float cue_requested = 0.0f;
    float direct_shaped = 0.0f;
    float cue_shaped = 0.0f;
    float requested_retention = 0.0f;
    float shaped_retention = 0.0f;
};

struct IncidentReport {
    AxisRun close_x;
    AxisRun close_y;
    AxisRun far_x;
    AxisRun far_y;
    bool all_triggers_executed = false;
    bool close_x_requested_pass = false;
    bool close_y_requested_pass = false;
    bool close_x_shaped_pass = false;
    bool close_y_shaped_pass = false;
    bool far_x_counterfactual_pass = false;
    bool far_y_counterfactual_pass = false;
    bool overall_pass = false;
};

float magnitude(common_native::Vec2f value) {
    return std::hypot(value.x, value.y);
}

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 180.0f);
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 1;
    config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    config.ai_aim.body_lock_max_ai_force = 0.30f;
    config.ai_aim.body_lock_max_ai_force_y = 0.42f;
    return config;
}

controller_native::incident_fixture::TargetSpec target_spec(
    float body_height,
    std::uint64_t observation_id) {
    controller_native::incident_fixture::TargetSpec spec;
    spec.center_x = 320.0f;
    spec.center_y = 256.0f;
    spec.body_width = body_height * 0.40f;
    spec.body_height = body_height;
    spec.aim_height_ratio = 0.365f;
    spec.observation_id = observation_id;
    spec.selector_generation = kSelectorGeneration;
    spec.candidate_has_aim_region = true;
    spec.color_classified = true;
    spec.fire_authority = false;
    spec.has_enemy_cue = true;
    spec.enemy_identity_confirmed = true;
    spec.confidence = 1.0f;
    return spec;
}

PhysicalGamepadState neutral_ads_input() {
    return controller_native::incident_fixture::ads_input(0.0f, 0.0f, false);
}

AxisRun run_axis(
    float body_height,
    bool vertical,
    std::uint64_t observation_id) {
    const auto spec = target_spec(body_height, observation_id);
    double now = 200.0;
    NativeGamepadController controller(incident_config(), &now);
    std::uint64_t frame_id = 1;

    controller.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            spec, frame_id++, now, 0.0f, 0.0f));
    (void)controller.build_output(neutral_ads_input());
    now += 0.005;

    const float dx = vertical ? 0.0f : kAxisErrorPx;
    const float dy = vertical ? kAxisErrorPx : 0.0f;
    for (int index = 0; index < 10; ++index) {
        controller.submit_vision_snapshot(
            controller_native::incident_fixture::observed_snapshot(
                spec, frame_id++, now, dx, dy));
        (void)controller.build_output(neutral_ads_input());
        now += 0.005;
    }
    const auto direct_plan = controller.last_target_plan();
    const auto direct_components = controller.last_output_components();

    for (int index = 0; index < 10; ++index) {
        controller.submit_vision_snapshot(
            controller_native::incident_fixture::cue_snapshot(
                spec, frame_id++, now, dx, dy));
        (void)controller.build_output(neutral_ads_input());
        now += 0.005;
    }
    const auto cue_plan = controller.last_target_plan();
    const auto cue_components = controller.last_output_components();

    AxisRun result;
    result.target_id = direct_plan.target_id;
    result.normalized_target_height = body_height / 512.0f;
    result.direct_authority = direct_plan.aim_authority;
    result.cue_authority = cue_plan.aim_authority;
    result.direct_requested = magnitude(direct_components.requested_assist_stick);
    result.cue_requested = magnitude(cue_components.requested_assist_stick);
    result.direct_shaped = magnitude(direct_components.shaped_assist_stick);
    result.cue_shaped = magnitude(cue_components.shaped_assist_stick);
    result.requested_retention = result.direct_requested > 1.0e-6f
        ? result.cue_requested / result.direct_requested
        : 0.0f;
    result.shaped_retention = result.direct_shaped > 1.0e-6f
        ? result.cue_shaped / result.direct_shaped
        : 0.0f;
    result.cue_remained_aim_only =
        !cue_plan.fire_authority && !cue_plan.fire_requested &&
        !cue_components.auto_fire_active && !cue_components.fire_button;
    result.trigger_executed =
        direct_plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        cue_plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        direct_plan.target_id != 0 &&
        cue_plan.target_id == direct_plan.target_id &&
        cue_plan.selector_target_generation == kSelectorGeneration &&
        cue_plan.cue_continuation &&
        cue_plan.source_observation_id == 0 &&
        direct_plan.direct_person_observation &&
        result.direct_requested > 1.0e-4f &&
        result.direct_shaped > 1.0e-4f &&
        result.cue_remained_aim_only;
    return result;
}

IncidentReport evaluate_incident() {
    IncidentReport report;
    report.close_x = run_axis(kCloseBodyHeightPx, false, 17011);
    report.close_y = run_axis(kCloseBodyHeightPx, true, 17012);
    report.far_x = run_axis(kFarBodyHeightPx, false, 17013);
    report.far_y = run_axis(kFarBodyHeightPx, true, 17014);
    report.all_triggers_executed =
        report.close_x.trigger_executed &&
        report.close_y.trigger_executed &&
        report.far_x.trigger_executed &&
        report.far_y.trigger_executed;
    report.close_x_requested_pass =
        report.close_x.requested_retention >=
        kMinimumCloseRequestedRetention;
    report.close_y_requested_pass =
        report.close_y.requested_retention >=
        kMinimumCloseRequestedRetention;
    report.close_x_shaped_pass =
        report.close_x.shaped_retention >=
        kMinimumCloseShapedRetention;
    report.close_y_shaped_pass =
        report.close_y.shaped_retention >=
        kMinimumCloseShapedRetention;
    report.far_x_counterfactual_pass =
        report.far_x.requested_retention <=
        kMaximumFarRequestedRetention;
    report.far_y_counterfactual_pass =
        report.far_y.requested_retention <=
        kMaximumFarRequestedRetention;
    report.overall_pass =
        report.all_triggers_executed &&
        report.close_x_requested_pass &&
        report.close_y_requested_pass &&
        report.close_x_shaped_pass &&
        report.close_y_shaped_pass &&
        report.far_x_counterfactual_pass &&
        report.far_y_counterfactual_pass;
    return report;
}

void write_axis_run(
    std::ofstream& output,
    const AxisRun& run,
    int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner = pad + "  ";
    output << pad << "{\n"
           << inner << "\"trigger_executed\": "
           << run.trigger_executed << ",\n"
           << inner << "\"cue_remained_aim_only\": "
           << run.cue_remained_aim_only << ",\n"
           << inner << "\"target_id\": " << run.target_id << ",\n"
           << inner << "\"normalized_target_height\": "
           << run.normalized_target_height << ",\n"
           << inner << "\"direct_authority\": "
           << run.direct_authority << ",\n"
           << inner << "\"cue_authority\": "
           << run.cue_authority << ",\n"
           << inner << "\"direct_requested\": "
           << run.direct_requested << ",\n"
           << inner << "\"cue_requested\": "
           << run.cue_requested << ",\n"
           << inner << "\"direct_shaped\": "
           << run.direct_shaped << ",\n"
           << inner << "\"cue_shaped\": "
           << run.cue_shaped << ",\n"
           << inner << "\"requested_retention\": "
           << run.requested_retention << ",\n"
           << inner << "\"shaped_retention\": "
           << run.shaped_retention << "\n"
           << pad << "}";
}

void write_report(
    const std::filesystem::path& output_path,
    const IncidentReport& report) {
    auto output = controller_native::incident_fixture::open_report(
        output_path, true);
    output << std::boolalpha << std::fixed << std::setprecision(6);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"a short close-target person-box dropout cuts BodyLock correction while same-generation current cue evidence retains the target\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"centered direct observation -> ten offset direct observations -> ten fresh same-generation cue continuations at 5 ms cadence\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"zero on X and Y\",\n"
           << "    \"left_stick_manual\": \"zero on X and Y\",\n"
           << "    \"recoil_firing\": \"disabled; no physical or synthetic fire\",\n"
           << "    \"controller_mode\": \"production NativeGamepadController BodyLock after completed ADS acquisition\",\n"
           << "    \"refresh_rate_hz\": 200,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"all_triggers_executed\": "
           << report.all_triggers_executed << ",\n"
           << "  \"runs\": {\n"
           << "    \"close_x\": ";
    write_axis_run(output, report.close_x, 4);
    output << ",\n    \"close_y\": ";
    write_axis_run(output, report.close_y, 4);
    output << ",\n    \"far_x\": ";
    write_axis_run(output, report.far_x, 4);
    output << ",\n    \"far_y\": ";
    write_axis_run(output, report.far_y, 4);
    output << "\n  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\": \"O1\", \"metric\": \"close_x_requested_retention\", \"operator\": \">=\", \"threshold\": "
           << kMinimumCloseRequestedRetention << ", \"observed\": "
           << report.close_x.requested_retention << ", \"pass\": "
           << report.close_x_requested_pass << "},\n"
           << "    {\"id\": \"O2\", \"metric\": \"close_y_requested_retention\", \"operator\": \">=\", \"threshold\": "
           << kMinimumCloseRequestedRetention << ", \"observed\": "
           << report.close_y.requested_retention << ", \"pass\": "
           << report.close_y_requested_pass << "},\n"
           << "    {\"id\": \"O3\", \"metric\": \"close_x_shaped_retention\", \"operator\": \">=\", \"threshold\": "
           << kMinimumCloseShapedRetention << ", \"observed\": "
           << report.close_x.shaped_retention << ", \"pass\": "
           << report.close_x_shaped_pass << "},\n"
           << "    {\"id\": \"O4\", \"metric\": \"close_y_shaped_retention\", \"operator\": \">=\", \"threshold\": "
           << kMinimumCloseShapedRetention << ", \"observed\": "
           << report.close_y.shaped_retention << ", \"pass\": "
           << report.close_y_shaped_pass << "},\n"
           << "    {\"id\": \"O5\", \"metric\": \"far_x_requested_retention\", \"operator\": \"<=\", \"threshold\": "
           << kMaximumFarRequestedRetention << ", \"observed\": "
           << report.far_x.requested_retention << ", \"pass\": "
           << report.far_x_counterfactual_pass << "},\n"
           << "    {\"id\": \"O6\", \"metric\": \"far_y_requested_retention\", \"operator\": \"<=\", \"threshold\": "
           << kMaximumFarRequestedRetention << ", \"observed\": "
           << report.far_y.requested_retention << ", \"pass\": "
           << report.far_y_counterfactual_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_close_cue_hold_authority_incident_regression(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const IncidentReport report = evaluate_incident();
        write_report(output_path, report);
        std::cout << "[CloseCueHoldAuthorityIncident] trigger="
                  << (report.all_triggers_executed ? 1 : 0)
                  << " close_x=" << report.close_x.requested_retention
                  << " close_y=" << report.close_y.requested_retention
                  << " far_x=" << report.far_x.requested_retention
                  << " far_y=" << report.far_y.requested_retention
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.all_triggers_executed) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[CloseCueHoldAuthorityIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[CloseCueHoldAuthorityIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_close_cue_hold_authority_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseBodyLock", "incident_close_cue_hold_authority", "close_cue_hold_authority_incident.json", run_close_cue_hold_authority_incident_regression);
}
