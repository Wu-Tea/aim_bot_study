#include "incident_fixture_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "neutral-drift-arbitration-early-stop-20260817";
constexpr float kTargetErrorPx = 70.0f;
constexpr float kStaticOpposingDrift = -0.0274f;
constexpr float kDeliberateOpposingInput = -0.30f;
constexpr float kMinimumAiProposal = 0.05f;
constexpr float kMinimumDriftProgress = 0.80f;
constexpr float kMinimumDeliberateRetentionRatio = 0.50f;
constexpr std::uint64_t kObservationId = 81701;
constexpr std::uint64_t kSelectorGeneration = 817;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 72.0f, 140.0f, 0.365f,
    kObservationId, kSelectorGeneration, true, false, false};

enum class Axis { X, Y };
enum class Mode { Ads, BodyLock };

const char* axis_name(Axis axis) noexcept {
    return axis == Axis::X ? "x" : "y";
}

const char* mode_name(Mode mode) noexcept {
    return mode == Mode::Ads ? "ads_snap" : "body_lock";
}

float axis_value(common_native::Vec2f value, Axis axis) noexcept {
    return axis == Axis::X ? value.x : value.y;
}

float axis_value(pipeline_contract::Vec2f value, Axis axis) noexcept {
    return axis == Axis::X ? value.x : value.y;
}

float axis_value(
    const controller_native::GamepadOutputState& value,
    Axis axis) noexcept {
    return axis == Axis::X ? value.right_x : value.right_y;
}

PhysicalGamepadState physical_input(Axis axis, float value) {
    return axis == Axis::X
        ? controller_native::incident_fixture::ads_input(value, 0.0f)
        : controller_native::incident_fixture::ads_input(0.0f, value);
}

GamepadRuntimeConfig incident_config(Mode mode) {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 180.0f);
    // Neutral-drift arbitration belongs to the retained layered controller.
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.66f;
    config.ai_aim.body_lock_activation_box_px = 150.0f;
    if (mode == Mode::Ads) {
        config.ai_aim.ads_completion_fresh_frames = 1000;
        config.ai_aim.ads_max_acquisition_ms = 500.0f;
    } else {
        config.ai_aim.ads_completion_radius_px = 8.0f;
        config.ai_aim.ads_completion_fresh_frames = 1;
        config.ai_aim.ads_max_acquisition_ms = 50.0f;
    }
    return config;
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now,
    Axis axis,
    float error_px) {
    const float dx = axis == Axis::X ? error_px : 0.0f;
    // Positive physical Y is up-stick. An above-center target therefore
    // requests a positive Y correction.
    const float dy = axis == Axis::Y ? -error_px : 0.0f;
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, dx, dy);
}

struct ScenarioResult {
    std::string mode;
    std::string axis;
    bool target_owned = false;
    bool correct_mode = false;
    bool drift_filtered_neutral = false;
    bool drift_ai_opposes_raw = false;
    bool deliberate_filtered_active = false;
    bool deliberate_ai_opposes_manual = false;
    bool deliberate_contract_aligned = false;
    bool finite = false;
    float drift_raw = 0.0f;
    float drift_filtered = 0.0f;
    float drift_ai = 0.0f;
    float drift_final = 0.0f;
    float drift_progress = 0.0f;
    float deliberate_raw = 0.0f;
    float deliberate_filtered = 0.0f;
    float deliberate_ai = 0.0f;
    float deliberate_final = 0.0f;
    float deliberate_retention_ratio = 0.0f;
};

struct IncidentReport {
    std::vector<ScenarioResult> scenarios;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    float minimum_drift_progress = 0.0f;
    float minimum_deliberate_retention_ratio = 0.0f;
    bool drift_progress_pass = false;
    bool deliberate_retention_pass = false;
    bool overall_pass = false;
};

ScenarioResult evaluate_scenario(Mode mode, Axis axis) {
    double now = 100.0;
    NativeGamepadController controller(incident_config(mode), &now);
    std::uint64_t frame_id = 1;

    // Calibrate a stable physical offset while ADS is held but no target is
    // owned. The production IntentFilter must classify this as neutral before
    // the target arrives; this is the live schema-18 incident boundary.
    for (int tick = 0; tick < 180; ++tick) {
        (void)controller.build_output(
            physical_input(axis, kStaticOpposingDrift));
        now += 0.001;
    }

    if (mode == Mode::BodyLock) {
        for (int frame = 0; frame < 3; ++frame) {
            controller.submit_vision_snapshot(observed_snapshot(
                frame_id++, now, axis, 0.0f));
            (void)controller.build_output(
                physical_input(axis, kStaticOpposingDrift));
            now += 0.005;
        }
    }

    for (int frame = 0; frame < 4; ++frame) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame_id++, now, axis, kTargetErrorPx));
        (void)controller.build_output(
            physical_input(axis, kStaticOpposingDrift));
        now += 0.005;
    }

    const auto& drift_components = controller.last_output_components();
    const auto& drift_plan = controller.last_target_plan();

    ScenarioResult result;
    result.mode = mode_name(mode);
    result.axis = axis_name(axis);
    result.drift_raw = kStaticOpposingDrift;
    result.drift_filtered = axis_value(
        drift_components.filtered_manual_stick, axis);
    result.drift_ai = axis_value(
        drift_components.ai_aim_stick, axis);
    result.drift_final = axis_value(
        drift_components.target_final_stick, axis);
    const float drift_denominator = result.drift_ai - result.drift_raw;
    result.drift_progress = std::fabs(drift_denominator) > 1.0e-6f
        ? (result.drift_final - result.drift_raw) / drift_denominator
        : 0.0f;
    result.target_owned =
        drift_plan.target_id != 0 &&
        drift_plan.selector_target_generation == kSelectorGeneration &&
        drift_plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        drift_plan.aim_authority > 0.0f;
    result.correct_mode = drift_components.aim_mode == mode_name(mode);
    result.drift_filtered_neutral =
        std::fabs(result.drift_filtered) <= 1.0e-6f;
    result.drift_ai_opposes_raw =
        result.drift_ai >= kMinimumAiProposal &&
        result.drift_ai * result.drift_raw < 0.0f;

    // Counterfactual: a deliberate movement remains visible to the intent/D
    // path. ADS still owns final positioning, while cooperative BodyLock keeps
    // the manual sign; the two modes must not accidentally share one policy.
    controller.submit_vision_snapshot(observed_snapshot(
        frame_id, now, axis, kTargetErrorPx));
    const auto deliberate_output = controller.build_output(
        physical_input(axis, kDeliberateOpposingInput));
    const auto& deliberate_components = controller.last_output_components();
    result.deliberate_raw = kDeliberateOpposingInput;
    result.deliberate_filtered = axis_value(
        deliberate_components.filtered_manual_stick, axis);
    result.deliberate_ai = axis_value(
        deliberate_components.ai_aim_stick, axis);
    result.deliberate_final = axis_value(
        deliberate_components.target_final_stick, axis);
    const float deliberate_expected = mode == Mode::Ads
        ? result.deliberate_ai : kDeliberateOpposingInput;
    result.deliberate_retention_ratio =
        std::fabs(deliberate_expected) > 1.0e-6f
        ? std::fabs(result.deliberate_final) /
            std::fabs(deliberate_expected)
        : 0.0f;
    result.deliberate_filtered_active =
        result.deliberate_filtered < -0.10f;
    result.deliberate_ai_opposes_manual =
        result.deliberate_ai >= kMinimumAiProposal &&
        result.deliberate_ai * result.deliberate_raw < 0.0f;
    result.deliberate_contract_aligned =
        result.deliberate_final * deliberate_expected > 0.0f;
    result.finite =
        controller_native::incident_fixture::finite_unit(result.drift_ai) &&
        controller_native::incident_fixture::finite_unit(result.drift_final) &&
        controller_native::incident_fixture::finite_unit(result.deliberate_ai) &&
        controller_native::incident_fixture::finite_unit(
            result.deliberate_final) &&
        controller_native::incident_fixture::finite_unit(
            axis_value(deliberate_output, axis)) &&
        std::isfinite(result.drift_progress) &&
        std::isfinite(result.deliberate_retention_ratio);
    return result;
}

IncidentReport evaluate_incident() {
    IncidentReport report;
    for (Mode mode : {Mode::Ads, Mode::BodyLock}) {
        for (Axis axis : {Axis::X, Axis::Y}) {
            report.scenarios.push_back(evaluate_scenario(mode, axis));
        }
    }

    report.trigger_executed = std::all_of(
        report.scenarios.begin(), report.scenarios.end(),
        [](const ScenarioResult& value) {
            return value.target_owned && value.correct_mode && value.finite &&
                value.drift_filtered_neutral && value.drift_ai_opposes_raw;
        });
    report.counterfactuals_valid = std::all_of(
        report.scenarios.begin(), report.scenarios.end(),
        [](const ScenarioResult& value) {
            return value.deliberate_filtered_active &&
                value.deliberate_ai_opposes_manual &&
                value.deliberate_contract_aligned;
        });
    report.minimum_drift_progress = std::numeric_limits<float>::infinity();
    report.minimum_deliberate_retention_ratio =
        std::numeric_limits<float>::infinity();
    for (const auto& scenario : report.scenarios) {
        report.minimum_drift_progress = std::min(
            report.minimum_drift_progress, scenario.drift_progress);
        report.minimum_deliberate_retention_ratio = std::min(
            report.minimum_deliberate_retention_ratio,
            scenario.deliberate_retention_ratio);
    }
    report.drift_progress_pass =
        report.minimum_drift_progress >= kMinimumDriftProgress;
    report.deliberate_retention_pass =
        report.minimum_deliberate_retention_ratio >=
        kMinimumDeliberateRetentionRatio;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactuals_valid &&
        report.drift_progress_pass &&
        report.deliberate_retention_pass;
    return report;
}

void write_scenario(
    std::ofstream& output,
    const ScenarioResult& value,
    int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner = pad + "  ";
    output << pad << "{\n"
           << inner << "\"mode\": \"" << value.mode << "\",\n"
           << inner << "\"axis\": \"" << value.axis << "\",\n"
           << inner << "\"target_owned\": " << value.target_owned << ",\n"
           << inner << "\"correct_mode\": " << value.correct_mode << ",\n"
           << inner << "\"drift_filtered_neutral\": "
           << value.drift_filtered_neutral << ",\n"
           << inner << "\"drift_ai_opposes_raw\": "
           << value.drift_ai_opposes_raw << ",\n"
           << inner << "\"drift_raw\": " << value.drift_raw << ",\n"
           << inner << "\"drift_filtered\": "
           << value.drift_filtered << ",\n"
           << inner << "\"drift_ai\": " << value.drift_ai << ",\n"
           << inner << "\"drift_final\": " << value.drift_final << ",\n"
           << inner << "\"drift_progress\": "
           << value.drift_progress << ",\n"
           << inner << "\"deliberate_filtered_active\": "
           << value.deliberate_filtered_active << ",\n"
           << inner << "\"deliberate_ai_opposes_manual\": "
           << value.deliberate_ai_opposes_manual << ",\n"
           << inner << "\"deliberate_contract_aligned\": "
           << value.deliberate_contract_aligned << ",\n"
           << inner << "\"deliberate_raw\": "
           << value.deliberate_raw << ",\n"
           << inner << "\"deliberate_filtered\": "
           << value.deliberate_filtered << ",\n"
           << inner << "\"deliberate_ai\": "
           << value.deliberate_ai << ",\n"
           << inner << "\"deliberate_final\": "
           << value.deliberate_final << ",\n"
           << inner << "\"deliberate_retention_ratio\": "
           << value.deliberate_retention_ratio << ",\n"
           << inner << "\"finite\": " << value.finite << "\n"
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
           << "  \"symptom\": \"AI requests a material correction while filtered manual is neutral, but calibrated opposing raw drift leaves final output stopped or wrong-way\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"180 one-millisecond controller warm-up ticks, then fresh target observations at 5 ms cadence\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"per-axis static opposing -0.0274 calibrated to filtered neutral; deliberate opposing -0.30 counterfactual\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"recoil disabled and no fire input\",\n"
           << "    \"controller_mode\": \"production NativeGamepadController ADS Snap and BodyLock\",\n"
           << "    \"controller_tick_hz\": 1000,\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON report only\"\n"
           << "  },\n"
           << "  \"trigger_executed\": "
           << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < report.scenarios.size(); ++index) {
        write_scenario(output, report.scenarios[index], 4);
        output << (index + 1 == report.scenarios.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"metrics\": {\n"
           << "    \"minimum_drift_progress\": "
           << report.minimum_drift_progress << ",\n"
           << "    \"minimum_deliberate_retention_ratio\": "
           << report.minimum_deliberate_retention_ratio << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"minimum_drift_progress\",\"operator\":\">=\",\"threshold\":"
           << kMinimumDriftProgress << ",\"observed\":"
           << report.minimum_drift_progress << ",\"pass\":"
           << report.drift_progress_pass << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"minimum_deliberate_retention_ratio\",\"operator\":\">=\",\"threshold\":"
           << kMinimumDeliberateRetentionRatio << ",\"observed\":"
           << report.minimum_deliberate_retention_ratio << ",\"pass\":"
           << report.deliberate_retention_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const IncidentReport report = evaluate_incident();
        write_report(output_path, report);
        std::cout << "[NeutralDriftArbitrationIncident] trigger="
                  << (report.trigger_executed ? 1 : 0)
                  << " counterfactuals="
                  << (report.counterfactuals_valid ? 1 : 0)
                  << " min_drift_progress="
                  << report.minimum_drift_progress
                  << " deliberate_retention="
                  << report.minimum_deliberate_retention_ratio
                  << " result="
                  << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) {
            return 3;
        }
        return report.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[NeutralDriftArbitrationIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[NeutralDriftArbitrationIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}
