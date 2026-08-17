#include "incident_fixture_support.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "vertical-correction-cue-release-20260811";
constexpr float kSourceErrorY = -14.0f;
constexpr float kDesiredErrorY = 16.0f;
constexpr float kManualDownY = -0.80f;
constexpr float kMinimumDownwardAcknowledgement = 0.25f;
constexpr float kMaximumReleaseStep = 0.25f;
constexpr std::uint64_t kObservationId = 7001;
constexpr std::uint64_t kSelectorGeneration = 91;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 56.0f, 140.0f, 0.365f,
    kObservationId, kSelectorGeneration, false, false, true};

struct IncidentRun {
    bool observed_target_owned = false;
    bool cue_same_target_owned = false;
    bool cue_ads_authoritative = false;
    bool manual_blocked_during_cue = false;
    bool release_restored_manual = false;
    bool finite_outputs = false;
    std::uint64_t target_id = 0;
    float observed_output_y = 0.0f;
    float cue_output_y = 0.0f;
    float release_output_y = 0.0f;
    float cue_aim_authority = 0.0f;
};

struct IncidentReport {
    IncidentRun neutral;
    IncidentRun manual_down;
    bool trigger_executed = false;
    bool neutral_counterfactual_valid = false;
    float observed_downward_ack_delta = 0.0f;
    float cue_downward_ack_delta = 0.0f;
    float cue_release_step = 0.0f;
    float neutral_release_step = 0.0f;
    bool observed_ack_pass = false;
    bool cue_ack_pass = false;
    bool release_step_pass = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(50.0f, 180.0f);
    // This fixture owns the retained layered handover/release contract.
    // Keep the short fixture in one acquisition policy. The incident is about
    // desired-point/manual authority and cue release, not ADS-to-BodyLock
    // ownership.
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;
    config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    return config;
}

PhysicalGamepadState physical_input(float right_y) {
    return controller_native::incident_fixture::ads_input(0.0f, right_y, true);
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now,
    float dy) {
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, 0.0f, dy);
}

ControllerVisionSnapshot cue_snapshot(
    std::uint64_t frame_id,
    double now,
    float dy) {
    return controller_native::incident_fixture::cue_snapshot(
        kTarget, frame_id, now, 0.0f, dy);
}

ControllerVisionSnapshot empty_snapshot(
    std::uint64_t frame_id,
    double now) {
    return controller_native::incident_fixture::empty_snapshot(
        kTarget, frame_id, now);
}

IncidentRun run_incident(float manual_y) {
    double now = 100.0;
    NativeGamepadController controller(
        incident_config(), &now);
    IncidentRun result;

    std::uint64_t frame_id = 1;
    for (int index = 0; index < 12; ++index) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame_id++, now, kSourceErrorY));
        const auto output = controller.build_output(physical_input(manual_y));
        result.observed_output_y = output.right_y;
        now += 0.005;
    }
    const auto observed_plan = controller.last_target_plan();
    result.target_id = observed_plan.target_id;
    result.observed_target_owned =
        observed_plan.target_id != 0 &&
        observed_plan.selector_target_generation == kSelectorGeneration &&
        observed_plan.lifecycle == pipeline_contract::TargetLifecycle::Observed;

    for (int index = 0; index < 12; ++index) {
        controller.submit_vision_snapshot(cue_snapshot(
            frame_id++, now, kSourceErrorY));
        const auto output = controller.build_output(physical_input(manual_y));
        result.cue_output_y = output.right_y;
        now += 0.005;
    }
    const auto cue_plan = controller.last_target_plan();
    const auto cue_components = controller.last_output_components();
    result.cue_aim_authority = cue_plan.aim_authority;
    result.cue_same_target_owned =
        result.target_id != 0 && cue_plan.target_id == result.target_id &&
        cue_plan.selector_target_generation == kSelectorGeneration &&
        cue_plan.cue_continuation;
    result.cue_ads_authoritative =
        cue_plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        cue_plan.ads_acquisition_active &&
        cue_plan.aim_authority >= 0.999f &&
        !cue_plan.fire_authority && !cue_plan.fire_requested;
    result.manual_blocked_during_cue =
        std::fabs(manual_y) <= 1.0e-6f ||
        !cue_components.manual_passthrough_y;

    controller.submit_vision_snapshot(empty_snapshot(frame_id, now));
    const auto release_output = controller.build_output(
        physical_input(manual_y));
    const auto release_plan = controller.last_target_plan();
    const auto release_components = controller.last_output_components();
    result.release_output_y = release_output.right_y;
    result.release_restored_manual =
        release_plan.target_id == 0 &&
        release_components.manual_passthrough_y &&
        std::fabs(release_output.right_y - manual_y) <= 1.0e-5f;
    result.finite_outputs =
        controller_native::incident_fixture::finite_unit(result.observed_output_y) &&
        controller_native::incident_fixture::finite_unit(result.cue_output_y) &&
        controller_native::incident_fixture::finite_unit(result.release_output_y);
    return result;
}

IncidentReport evaluate_incident() {
    IncidentReport report;
    report.neutral = run_incident(0.0f);
    report.manual_down = run_incident(kManualDownY);
    // Trigger validity is defined by the reproduced target/cue lifecycle and
    // finite native output. manual_passthrough_y is diagnostic metadata, not a
    // product oracle: the cooperative solver may preserve exactly the same
    // physical value without describing it as the legacy blocked-axis path.
    report.trigger_executed =
        report.neutral.observed_target_owned &&
        report.neutral.cue_same_target_owned &&
        report.neutral.cue_ads_authoritative &&
        report.neutral.release_restored_manual &&
        report.neutral.finite_outputs &&
        report.manual_down.observed_target_owned &&
        report.manual_down.cue_same_target_owned &&
        report.manual_down.cue_ads_authoritative &&
        report.manual_down.release_restored_manual &&
        report.manual_down.finite_outputs;
    report.neutral_counterfactual_valid =
        std::fabs(report.neutral.release_output_y) <= 1.0e-5f &&
        report.neutral.target_id != 0 &&
        report.neutral.target_id == report.manual_down.target_id;
    report.observed_downward_ack_delta =
        report.neutral.observed_output_y -
        report.manual_down.observed_output_y;
    report.cue_downward_ack_delta =
        report.neutral.cue_output_y - report.manual_down.cue_output_y;
    report.cue_release_step = std::fabs(
        report.manual_down.release_output_y -
        report.manual_down.cue_output_y);
    report.neutral_release_step = std::fabs(
        report.neutral.release_output_y - report.neutral.cue_output_y);
    report.observed_ack_pass =
        report.observed_downward_ack_delta >=
        kMinimumDownwardAcknowledgement;
    report.cue_ack_pass =
        report.cue_downward_ack_delta >=
        kMinimumDownwardAcknowledgement;
    report.release_step_pass =
        report.cue_release_step <= kMaximumReleaseStep;
    report.overall_pass =
        report.trigger_executed &&
        report.neutral_counterfactual_valid &&
        report.observed_ack_pass &&
        report.cue_ack_pass &&
        report.release_step_pass;
    return report;
}

void write_run(std::ofstream& output, const IncidentRun& run, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner = pad + "  ";
    output << pad << "{\n"
           << inner << "\"observed_target_owned\": "
           << run.observed_target_owned << ",\n"
           << inner << "\"cue_same_target_owned\": "
           << run.cue_same_target_owned << ",\n"
           << inner << "\"cue_ads_authoritative\": "
           << run.cue_ads_authoritative << ",\n"
           << inner << "\"manual_blocked_during_cue\": "
           << run.manual_blocked_during_cue << ",\n"
           << inner << "\"release_restored_manual\": "
           << run.release_restored_manual << ",\n"
           << inner << "\"finite_outputs\": " << run.finite_outputs << ",\n"
           << inner << "\"target_id\": " << run.target_id << ",\n"
           << inner << "\"observed_output_y\": "
           << run.observed_output_y << ",\n"
           << inner << "\"cue_output_y\": " << run.cue_output_y << ",\n"
           << inner << "\"release_output_y\": "
           << run.release_output_y << ",\n"
           << inner << "\"cue_aim_authority\": "
           << run.cue_aim_authority << "\n"
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
           << "  \"symptom\": \"downward correction is weak while cue owns Y, followed by an abrupt manual-release step\",\n"
           << "  \"truth\": {\n"
           << "    \"source_error_y_px\": " << kSourceErrorY << ",\n"
           << "    \"desired_error_y_px\": " << kDesiredErrorY << ",\n"
           << "    \"manual_down_y\": " << kManualDownY << ",\n"
           << "    \"intent\": \"correct_down_with_same_target\"\n"
           << "  },\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"12 direct and 12 cue frames at 5 ms cadence, then one fresh no-target frame\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"neutral 0.0 Y versus sustained -0.80 Y\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"physical RT held; recoil output disabled to isolate desired-point and authority behavior\",\n"
           << "    \"controller_mode\": \"production NativeGamepadController ADS acquisition plus same-generation cue continuation\",\n"
           << "    \"refresh_rate_hz\": 200,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"neutral_counterfactual_valid\": "
           << report.neutral_counterfactual_valid << ",\n"
           << "  \"runs\": {\n"
           << "    \"neutral\": ";
    write_run(output, report.neutral, 4);
    output << ",\n    \"manual_down\": ";
    write_run(output, report.manual_down, 4);
    output << "\n  },\n"
           << "  \"metrics\": {\n"
           << "    \"observed_downward_ack_delta\": "
           << report.observed_downward_ack_delta << ",\n"
           << "    \"cue_downward_ack_delta\": "
           << report.cue_downward_ack_delta << ",\n"
           << "    \"cue_release_step\": " << report.cue_release_step << ",\n"
           << "    \"neutral_release_step\": "
           << report.neutral_release_step << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\": \"O1\", \"metric\": \"observed_downward_ack_delta\", \"operator\": \">=\", \"threshold\": "
           << kMinimumDownwardAcknowledgement << ", \"observed\": "
           << report.observed_downward_ack_delta << ", \"pass\": "
           << report.observed_ack_pass << "},\n"
           << "    {\"id\": \"O2\", \"metric\": \"cue_downward_ack_delta\", \"operator\": \">=\", \"threshold\": "
           << kMinimumDownwardAcknowledgement << ", \"observed\": "
           << report.cue_downward_ack_delta << ", \"pass\": "
           << report.cue_ack_pass << "},\n"
           << "    {\"id\": \"O3\", \"metric\": \"cue_release_step\", \"operator\": \"<=\", \"threshold\": "
           << kMaximumReleaseStep << ", \"observed\": "
           << report.cue_release_step << ", \"pass\": "
           << report.release_step_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(argc, argv);
        const IncidentReport report = evaluate_incident();
        write_report(output_path, report);
        std::cout << "[VerticalCorrectionReleaseIncident] trigger="
                  << (report.trigger_executed ? 1 : 0)
                  << " observed_ack=" << report.observed_downward_ack_delta
                  << " cue_ack=" << report.cue_downward_ack_delta
                  << " release_step=" << report.cue_release_step
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed ||
            !report.neutral_counterfactual_valid) {
            return 3;
        }
        return report.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[VerticalCorrectionReleaseIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[VerticalCorrectionReleaseIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}
