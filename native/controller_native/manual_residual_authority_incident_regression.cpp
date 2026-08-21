#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <array>
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
    "manual-residual-authority-yield-20260811";
constexpr float kTargetErrorPx = 60.0f;
constexpr float kSubDeadzoneInput = 0.015f;
constexpr float kMicroInput = 0.05f;
constexpr float kMinimumAiHeadroom = 0.05f;
constexpr float kMinimumAlignedAssistGain = 0.025f;
constexpr float kMinimumAlignedRetentionRatio = 0.75f;
constexpr float kMaximumDeadzoneCrossingDrop = 0.10f;
// ADS owns the desired total after admission; BodyLock remains cooperative.
// The same opposing-input fixture therefore has a mode-specific expected
// output while retaining one tight numerical tolerance.
constexpr float kMaximumOpposingContractError = 0.020f;
constexpr std::uint64_t kObservationId = 8101;
constexpr std::uint64_t kSelectorGeneration = 111;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 72.0f, 140.0f, 0.365f,
    kObservationId, kSelectorGeneration, true, false, false};

enum class Axis {
    X,
    Y,
};

enum class ControlMode {
    AdsSnap,
    BodyLock,
};

const char* axis_name(Axis axis) noexcept {
    return axis == Axis::X ? "x" : "y";
}

const char* mode_name(ControlMode mode) noexcept {
    return mode == ControlMode::AdsSnap ? "ads_snap" : "body_lock";
}

float axis_value(common_native::Vec2f value, Axis axis) noexcept {
    return axis == Axis::X ? value.x : value.y;
}

float axis_value(const controller_native::GamepadOutputState& value, Axis axis) noexcept {
    return axis == Axis::X ? value.right_x : value.right_y;
}

float axis_value(const pipeline_contract::TargetPlan& plan, Axis axis) noexcept {
    return axis == Axis::X
        ? plan.desired_point_normalized.x
        : plan.desired_point_normalized.y;
}

struct CaseResult {
    std::string label;
    std::string mode;
    std::string axis;
    bool target_owned = false;
    bool correct_mode = false;
    bool finite = false;
    bool manual_correction = false;
    bool other_axis_correction = false;
    bool ai_has_expected_direction = false;
    bool desired_point_moved_as_requested = false;
    float manual_input = 0.0f;
    float filtered_manual = 0.0f;
    float ai_proposal = 0.0f;
    float target_final = 0.0f;
    float desired_before = 0.0f;
    float desired_after = 0.0f;
};

struct SustainedEscapeResult {
    int tick_count = 0;
    bool trigger_valid = false;
    bool every_tick_target_owned = false;
    bool every_tick_finite = false;
    bool every_tick_ai_opposed = false;
    bool every_tick_manual_correction = false;
    bool desired_point_moved_as_requested = false;
    float manual_input = 0.0f;
    float minimum_opposing_ai_proposal = 0.0f;
    float maximum_contract_error = 0.0f;
    float desired_before = 0.0f;
    float desired_after = 0.0f;
};

struct ScenarioResult {
    std::string mode;
    std::string axis;
    CaseResult neutral;
    CaseResult sub_deadzone;
    CaseResult aligned;
    CaseResult opposing;
    SustainedEscapeResult sustained_escape;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    float aligned_assist_gain = 0.0f;
    float aligned_retention_ratio = 0.0f;
    float deadzone_crossing_drop = 0.0f;
    float opposing_contract_error = 0.0f;
};

struct IncidentReport {
    std::vector<ScenarioResult> scenarios;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    float minimum_aligned_assist_gain = 0.0f;
    float minimum_aligned_retention_ratio = 0.0f;
    float maximum_deadzone_crossing_drop = 0.0f;
    float maximum_opposing_contract_error = 0.0f;
    bool aligned_gain_pass = false;
    bool aligned_retention_pass = false;
    bool deadzone_continuity_pass = false;
    bool opposing_contract_pass = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config(ControlMode mode) {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 180.0f);
    // Residual allocation is a legacy-layer rollback contract. The direct
    // controller intentionally uses manual + independent assist instead.
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.66f;
    config.ai_aim.body_lock_activation_box_px = 150.0f;
    config.ai_aim.desired_point_traversal_ms = 180.0f;
    config.ai_aim.desired_point_boundary_exit_ms = 250.0f;
    if (mode == ControlMode::AdsSnap) {
        config.ai_aim.ads_completion_fresh_frames = 1000;
        config.ai_aim.ads_max_acquisition_ms = 500.0f;
    } else {
        config.ai_aim.ads_completion_radius_px = 8.0f;
        config.ai_aim.ads_completion_fresh_frames = 1;
        config.ai_aim.ads_max_acquisition_ms = 50.0f;
    }
    return config;
}

PhysicalGamepadState physical_input(Axis axis, float value) {
    return axis == Axis::X
        ? controller_native::incident_fixture::ads_input(value, 0.0f)
        : controller_native::incident_fixture::ads_input(0.0f, value);
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy) {
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, dx, dy);
}

CaseResult run_case(
    ControlMode mode,
    Axis axis,
    const char* label,
    float manual_input) {
    double now = 100.0;
    NativeGamepadController controller(
        incident_config(mode), &now);
    std::uint64_t frame_id = 1;

    if (mode == ControlMode::BodyLock) {
        for (int index = 0; index < 3; ++index) {
            controller.submit_vision_snapshot(observed_snapshot(
                frame_id++, now, 0.0f, 0.0f));
            (void)controller.build_output(physical_input(axis, 0.0f));
            now += 0.005;
        }
    }

    const float dx = axis == Axis::X ? kTargetErrorPx : 0.0f;
    // Positive physical Y is up-stick. Use an above-center target so the
    // aligned manual request and the AI proposal have the same positive sign.
    const float dy = axis == Axis::Y ? -kTargetErrorPx : 0.0f;
    for (int index = 0; index < 4; ++index) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame_id++, now, dx, dy));
        (void)controller.build_output(physical_input(axis, 0.0f));
        now += 0.005;
    }
    const auto before = controller.last_target_plan();

    controller.submit_vision_snapshot(observed_snapshot(
        frame_id, now, dx, dy));
    const auto output = controller.build_output(
        physical_input(axis, manual_input));
    const auto after = controller.last_target_plan();
    const auto& components = controller.last_output_components();

    CaseResult result;
    result.label = label;
    result.mode = mode_name(mode);
    result.axis = axis_name(axis);
    result.manual_input = manual_input;
    result.filtered_manual = axis_value(
        components.filtered_manual_stick, axis);
    result.ai_proposal = axis_value(components.ai_aim_stick, axis);
    result.target_final = axis_value(
        components.target_final_stick, axis);
    result.desired_before = axis_value(before, axis);
    result.desired_after = axis_value(after, axis);
    result.target_owned =
        after.target_id != 0 &&
        after.selector_target_generation == kSelectorGeneration &&
        after.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        after.aim_authority > 0.0f;
    result.correct_mode = components.aim_mode == mode_name(mode);
    result.finite =
        controller_native::incident_fixture::finite_unit(axis_value(output, axis)) &&
        controller_native::incident_fixture::finite_unit(result.ai_proposal) &&
        controller_native::incident_fixture::finite_unit(result.target_final) &&
        std::isfinite(result.desired_before) &&
        std::isfinite(result.desired_after);
    result.manual_correction = axis == Axis::X
        ? components.manual_correction_x
        : components.manual_correction_y;
    result.other_axis_correction = axis == Axis::X
        ? components.manual_correction_y
        : components.manual_correction_x;
    result.ai_has_expected_direction = manual_input == 0.0f
        ? result.ai_proposal > kMicroInput + kMinimumAiHeadroom
        : result.ai_proposal * manual_input > 0.0f
            ? std::fabs(result.ai_proposal) >=
                std::fabs(manual_input) + kMinimumAiHeadroom
            : manual_input < 0.0f &&
                result.ai_proposal > kMicroInput + kMinimumAiHeadroom;
    if (manual_input > kSubDeadzoneInput) {
        result.desired_point_moved_as_requested = axis == Axis::X
            ? result.desired_after > result.desired_before
            : result.desired_after < result.desired_before;
    } else if (manual_input < -kSubDeadzoneInput) {
        result.desired_point_moved_as_requested = axis == Axis::X
            ? result.desired_after < result.desired_before
            : result.desired_after > result.desired_before;
    } else {
        result.desired_point_moved_as_requested =
            std::fabs(result.desired_after - result.desired_before) <= 1.0e-6f;
    }
    return result;
}

SustainedEscapeResult run_sustained_escape(
    ControlMode mode,
    Axis axis) {
    constexpr int kEscapeTicks = 12;
    constexpr float kEscapeInput = -kMicroInput;
    double now = 200.0;
    NativeGamepadController controller(
        incident_config(mode), &now);
    std::uint64_t frame_id = 100;

    if (mode == ControlMode::BodyLock) {
        for (int index = 0; index < 3; ++index) {
            controller.submit_vision_snapshot(observed_snapshot(
                frame_id++, now, 0.0f, 0.0f));
            (void)controller.build_output(physical_input(axis, 0.0f));
            now += 0.005;
        }
    }

    const float dx = axis == Axis::X ? kTargetErrorPx : 0.0f;
    const float dy = axis == Axis::Y ? -kTargetErrorPx : 0.0f;
    for (int index = 0; index < 4; ++index) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame_id++, now, dx, dy));
        (void)controller.build_output(physical_input(axis, 0.0f));
        now += 0.005;
    }

    SustainedEscapeResult result;
    result.tick_count = kEscapeTicks;
    result.manual_input = kEscapeInput;
    result.minimum_opposing_ai_proposal =
        std::numeric_limits<float>::infinity();
    result.every_tick_manual_correction = true;
    result.every_tick_target_owned = true;
    result.every_tick_finite = true;
    result.every_tick_ai_opposed = true;
    result.trigger_valid = true;
    result.desired_before = axis_value(controller.last_target_plan(), axis);

    for (int index = 0; index < kEscapeTicks; ++index) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame_id++, now, dx, dy));
        const auto output = controller.build_output(
            physical_input(axis, kEscapeInput));
        const auto& components = controller.last_output_components();
        const auto& plan = controller.last_target_plan();
        const float ai_proposal = axis_value(
            components.ai_aim_stick, axis);
        const float target_final = axis_value(
            components.target_final_stick, axis);
        const bool manual_correction = axis == Axis::X
            ? components.manual_correction_x
            : components.manual_correction_y;
        const bool target_owned =
            plan.target_id != 0 &&
            plan.selector_target_generation == kSelectorGeneration &&
            plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
            plan.aim_authority > 0.0f;

        result.minimum_opposing_ai_proposal = std::min(
            result.minimum_opposing_ai_proposal, ai_proposal);
        const float expected_output = mode == ControlMode::AdsSnap
            ? ai_proposal : kEscapeInput;
        result.maximum_contract_error = std::max(
            result.maximum_contract_error,
            std::fabs(target_final - expected_output));
        result.every_tick_manual_correction =
            result.every_tick_manual_correction && manual_correction;
        result.every_tick_target_owned =
            result.every_tick_target_owned && target_owned;
        result.every_tick_finite =
            result.every_tick_finite &&
            std::isfinite(axis_value(output, axis)) &&
            std::isfinite(ai_proposal) &&
            controller_native::incident_fixture::finite_unit(target_final);
        result.every_tick_ai_opposed =
            result.every_tick_ai_opposed &&
            ai_proposal > kMicroInput + kMinimumAiHeadroom;
        result.trigger_valid =
            result.every_tick_target_owned &&
            result.every_tick_finite &&
            result.every_tick_ai_opposed;
        now += 0.005;
    }

    result.desired_after = axis_value(controller.last_target_plan(), axis);
    result.desired_point_moved_as_requested = axis == Axis::X
        ? result.desired_after < result.desired_before
        : result.desired_after > result.desired_before;
    return result;
}

ScenarioResult evaluate_scenario(ControlMode mode, Axis axis) {
    ScenarioResult scenario;
    scenario.mode = mode_name(mode);
    scenario.axis = axis_name(axis);
    scenario.neutral = run_case(mode, axis, "neutral", 0.0f);
    scenario.sub_deadzone = run_case(
        mode, axis, "sub_deadzone", kSubDeadzoneInput);
    scenario.aligned = run_case(mode, axis, "aligned_micro", kMicroInput);
    scenario.opposing = run_case(mode, axis, "opposing_micro", -kMicroInput);
    scenario.sustained_escape = run_sustained_escape(mode, axis);

    scenario.trigger_executed =
        scenario.aligned.target_owned &&
        scenario.aligned.correct_mode &&
        scenario.aligned.finite &&
        scenario.aligned.manual_correction &&
        !scenario.aligned.other_axis_correction &&
        scenario.aligned.ai_has_expected_direction &&
        scenario.aligned.desired_point_moved_as_requested;
    scenario.counterfactuals_valid =
        scenario.neutral.target_owned &&
        scenario.neutral.correct_mode &&
        scenario.neutral.finite &&
        !scenario.neutral.manual_correction &&
        scenario.neutral.ai_has_expected_direction &&
        scenario.neutral.desired_point_moved_as_requested &&
        scenario.sub_deadzone.target_owned &&
        scenario.sub_deadzone.correct_mode &&
        scenario.sub_deadzone.finite &&
        !scenario.sub_deadzone.manual_correction &&
        scenario.sub_deadzone.ai_has_expected_direction &&
        scenario.sub_deadzone.desired_point_moved_as_requested &&
        scenario.opposing.target_owned &&
        scenario.opposing.correct_mode &&
        scenario.opposing.finite &&
        scenario.opposing.manual_correction &&
        !scenario.opposing.other_axis_correction &&
        scenario.opposing.ai_has_expected_direction &&
        scenario.opposing.desired_point_moved_as_requested &&
        scenario.sustained_escape.trigger_valid &&
        scenario.sustained_escape.every_tick_manual_correction &&
        scenario.sustained_escape.desired_point_moved_as_requested;

    scenario.aligned_assist_gain =
        scenario.aligned.target_final - scenario.aligned.manual_input;
    scenario.aligned_retention_ratio =
        std::fabs(scenario.aligned.ai_proposal) > 1.0e-6f
        ? std::fabs(scenario.aligned.target_final) /
            std::fabs(scenario.aligned.ai_proposal)
        : 0.0f;
    scenario.deadzone_crossing_drop = std::max(
        0.0f,
        std::fabs(scenario.sub_deadzone.target_final) -
            std::fabs(scenario.aligned.target_final));
    const float opposing_expected = mode == ControlMode::AdsSnap
        ? scenario.opposing.ai_proposal
        : scenario.opposing.manual_input;
    scenario.opposing_contract_error = std::max(
        std::fabs(scenario.opposing.target_final - opposing_expected),
        scenario.sustained_escape.maximum_contract_error);
    return scenario;
}

IncidentReport evaluate_incident() {
    IncidentReport report;
    for (ControlMode mode : {ControlMode::AdsSnap, ControlMode::BodyLock}) {
        for (Axis axis : {Axis::X, Axis::Y}) {
            report.scenarios.push_back(evaluate_scenario(mode, axis));
        }
    }

    report.trigger_executed = std::all_of(
        report.scenarios.begin(), report.scenarios.end(),
        [](const ScenarioResult& value) { return value.trigger_executed; });
    report.counterfactuals_valid = std::all_of(
        report.scenarios.begin(), report.scenarios.end(),
        [](const ScenarioResult& value) { return value.counterfactuals_valid; });
    report.minimum_aligned_assist_gain = std::numeric_limits<float>::infinity();
    report.minimum_aligned_retention_ratio = std::numeric_limits<float>::infinity();
    for (const auto& scenario : report.scenarios) {
        report.minimum_aligned_assist_gain = std::min(
            report.minimum_aligned_assist_gain,
            scenario.aligned_assist_gain);
        report.minimum_aligned_retention_ratio = std::min(
            report.minimum_aligned_retention_ratio,
            scenario.aligned_retention_ratio);
        report.maximum_deadzone_crossing_drop = std::max(
            report.maximum_deadzone_crossing_drop,
            scenario.deadzone_crossing_drop);
        report.maximum_opposing_contract_error = std::max(
            report.maximum_opposing_contract_error,
            scenario.opposing_contract_error);
    }
    report.aligned_gain_pass =
        report.minimum_aligned_assist_gain >= kMinimumAlignedAssistGain;
    report.aligned_retention_pass =
        report.minimum_aligned_retention_ratio >=
        kMinimumAlignedRetentionRatio;
    report.deadzone_continuity_pass =
        report.maximum_deadzone_crossing_drop <=
        kMaximumDeadzoneCrossingDrop;
    report.opposing_contract_pass =
        report.maximum_opposing_contract_error <=
        kMaximumOpposingContractError;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactuals_valid &&
        report.aligned_gain_pass &&
        report.aligned_retention_pass &&
        report.deadzone_continuity_pass &&
        report.opposing_contract_pass;
    return report;
}

void write_case(
    std::ofstream& output,
    const CaseResult& value,
    int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner = pad + "  ";
    output << pad << "{\n"
           << inner << "\"label\": \"" << value.label << "\",\n"
           << inner << "\"target_owned\": " << value.target_owned << ",\n"
           << inner << "\"correct_mode\": " << value.correct_mode << ",\n"
           << inner << "\"finite\": " << value.finite << ",\n"
           << inner << "\"manual_correction\": "
           << value.manual_correction << ",\n"
           << inner << "\"other_axis_correction\": "
           << value.other_axis_correction << ",\n"
           << inner << "\"ai_has_expected_direction\": "
           << value.ai_has_expected_direction << ",\n"
           << inner << "\"desired_point_moved_as_requested\": "
           << value.desired_point_moved_as_requested << ",\n"
           << inner << "\"manual_input\": " << value.manual_input << ",\n"
           << inner << "\"filtered_manual\": "
           << value.filtered_manual << ",\n"
           << inner << "\"ai_proposal\": " << value.ai_proposal << ",\n"
           << inner << "\"target_final\": " << value.target_final << ",\n"
           << inner << "\"desired_before\": "
           << value.desired_before << ",\n"
           << inner << "\"desired_after\": "
           << value.desired_after << "\n"
           << pad << "}";
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
           << inner << "\"trigger_executed\": "
           << value.trigger_executed << ",\n"
           << inner << "\"counterfactuals_valid\": "
           << value.counterfactuals_valid << ",\n"
           << inner << "\"aligned_assist_gain\": "
           << value.aligned_assist_gain << ",\n"
           << inner << "\"aligned_retention_ratio\": "
           << value.aligned_retention_ratio << ",\n"
           << inner << "\"deadzone_crossing_drop\": "
           << value.deadzone_crossing_drop << ",\n"
           << inner << "\"opposing_contract_error\": "
           << value.opposing_contract_error << ",\n"
           << inner << "\"cases\": {\n"
           << inner << "  \"neutral\": ";
    write_case(output, value.neutral, indent + 4);
    output << ",\n" << inner << "  \"sub_deadzone\": ";
    write_case(output, value.sub_deadzone, indent + 4);
    output << ",\n" << inner << "  \"aligned\": ";
    write_case(output, value.aligned, indent + 4);
    output << ",\n" << inner << "  \"opposing\": ";
    write_case(output, value.opposing, indent + 4);
    output << "\n" << inner << "},\n"
           << inner << "\"sustained_escape\": {\n"
           << inner << "  \"tick_count\": "
           << value.sustained_escape.tick_count << ",\n"
           << inner << "  \"trigger_valid\": "
           << value.sustained_escape.trigger_valid << ",\n"
           << inner << "  \"every_tick_target_owned\": "
           << value.sustained_escape.every_tick_target_owned << ",\n"
           << inner << "  \"every_tick_finite\": "
           << value.sustained_escape.every_tick_finite << ",\n"
           << inner << "  \"every_tick_ai_opposed\": "
           << value.sustained_escape.every_tick_ai_opposed << ",\n"
           << inner << "  \"every_tick_manual_correction\": "
           << value.sustained_escape.every_tick_manual_correction << ",\n"
           << inner << "  \"desired_point_moved_as_requested\": "
           << value.sustained_escape.desired_point_moved_as_requested << ",\n"
           << inner << "  \"manual_input\": "
           << value.sustained_escape.manual_input << ",\n"
           << inner << "  \"minimum_opposing_ai_proposal\": "
           << value.sustained_escape.minimum_opposing_ai_proposal << ",\n"
           << inner << "  \"maximum_contract_error\": "
           << value.sustained_escape.maximum_contract_error << ",\n"
           << inner << "  \"desired_before\": "
           << value.sustained_escape.desired_before << ",\n"
           << inner << "  \"desired_after\": "
           << value.sustained_escape.desired_after << "\n"
           << inner << "}\n" << pad << "}";
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
           << "  \"symptom\": \"same-direction micro input moves D but collapses final aim output from AI assistance to the raw physical axis\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"all observations fresh at 5 ms cadence\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"per-axis neutral 0.0, sub-deadzone +0.015, aligned +0.05, opposing -0.05, then 12 sustained opposing ticks at -0.05\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"recoil disabled and no fire input\",\n"
           << "    \"controller_mode\": \"production NativeGamepadController ADS Snap and BodyLock\",\n"
           << "    \"controller_tick_hz\": 200,\n"
           << "    \"game_refresh_hz\": \"not_applicable: no game plant is used by this same-tick authority fixture\",\n"
           << "    \"logging_mode\": \"fixture JSON report only\"\n"
           << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < report.scenarios.size(); ++index) {
        write_scenario(output, report.scenarios[index], 4);
        output << (index + 1 == report.scenarios.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"metrics\": {\n"
           << "    \"minimum_aligned_assist_gain\": "
           << report.minimum_aligned_assist_gain << ",\n"
           << "    \"minimum_aligned_retention_ratio\": "
           << report.minimum_aligned_retention_ratio << ",\n"
           << "    \"maximum_deadzone_crossing_drop\": "
           << report.maximum_deadzone_crossing_drop << ",\n"
           << "    \"maximum_opposing_contract_error\": "
           << report.maximum_opposing_contract_error << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\": \"O1\", \"metric\": \"minimum_aligned_assist_gain\", \"operator\": \">=\", \"threshold\": "
           << kMinimumAlignedAssistGain << ", \"observed\": "
           << report.minimum_aligned_assist_gain << ", \"pass\": "
           << report.aligned_gain_pass << "},\n"
           << "    {\"id\": \"O2\", \"metric\": \"minimum_aligned_retention_ratio\", \"operator\": \">=\", \"threshold\": "
           << kMinimumAlignedRetentionRatio << ", \"observed\": "
           << report.minimum_aligned_retention_ratio << ", \"pass\": "
           << report.aligned_retention_pass << "},\n"
           << "    {\"id\": \"O3\", \"metric\": \"maximum_deadzone_crossing_drop\", \"operator\": \"<=\", \"threshold\": "
           << kMaximumDeadzoneCrossingDrop << ", \"observed\": "
           << report.maximum_deadzone_crossing_drop << ", \"pass\": "
           << report.deadzone_continuity_pass << "},\n"
           << "    {\"id\": \"O4\", \"metric\": \"maximum_opposing_contract_error\", \"operator\": \"<=\", \"threshold\": "
           << kMaximumOpposingContractError << ", \"observed\": "
           << report.maximum_opposing_contract_error << ", \"pass\": "
           << report.opposing_contract_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_manual_residual_authority_incident_regression(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(argc, argv);
        const IncidentReport report = evaluate_incident();
        write_report(output_path, report);
        std::cout << "[ManualResidualAuthorityIncident] trigger="
                  << (report.trigger_executed ? 1 : 0)
                  << " counterfactuals="
                  << (report.counterfactuals_valid ? 1 : 0)
                  << " min_gain=" << report.minimum_aligned_assist_gain
                  << " min_retention="
                  << report.minimum_aligned_retention_ratio
                  << " max_deadzone_drop="
                  << report.maximum_deadzone_crossing_drop
                  << " opposing_contract_error="
                  << report.maximum_opposing_contract_error
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) {
            return 3;
        }
        return report.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[ManualResidualAuthorityIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[ManualResidualAuthorityIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_manual_residual_authority_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseBodyLock", "incident_manual_residual_authority", "manual_residual_authority_incident.json", run_manual_residual_authority_incident_regression);
}
