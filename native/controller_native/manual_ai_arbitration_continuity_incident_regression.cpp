#include "assist_control_state_machine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using controller_native::AssistControlPhase;
using controller_native::AssistControlStateMachine;
using controller_native::AssistControlStateMachineInput;
using controller_native::AssistControlStateMachineOutput;

constexpr const char* kIncidentId =
    "manual-ai-arbitration-continuity-20260814";
constexpr float kFirstAi = -0.24f;
constexpr float kSecondAi = -0.32f;
constexpr float kCoupledManual = 0.05f;
constexpr float kCoupledManualAfter = 0.04f;
constexpr float kDominantManual = 0.30f;
constexpr float kDominantManualAfter = 0.28f;
constexpr float kExplicitManual = 0.30f;
constexpr float kExplicitManualAfter = 0.28f;
constexpr float kMaximumAvoidableExcessStep = 0.03f;

enum class Axis { X, Y };
enum class Mode { Ads, BodyLock };

const char* axis_name(Axis axis) noexcept {
    return axis == Axis::X ? "x" : "y";
}

const char* mode_name(Mode mode) noexcept {
    return mode == Mode::Ads ? "ads_snap" : "body_lock";
}

float axis_value(pipeline_contract::Vec2f value, Axis axis) noexcept {
    return axis == Axis::X ? value.x : value.y;
}

pipeline_contract::Vec2f vector_for_axis(
    Axis axis,
    float axis_value,
    float orthogonal_value) noexcept {
    return axis == Axis::X
        ? pipeline_contract::Vec2f{axis_value, orthogonal_value}
        : pipeline_contract::Vec2f{orthogonal_value, axis_value};
}

struct SequenceResult {
    std::string label;
    std::string mode;
    std::string axis;
    float visual_authority = 0.0f;
    pipeline_contract::Vec2f first_manual{};
    pipeline_contract::Vec2f second_manual{};
    pipeline_contract::Vec2f first_filtered{};
    pipeline_contract::Vec2f second_filtered{};
    pipeline_contract::Vec2f first_ai{};
    pipeline_contract::Vec2f second_ai{};
    pipeline_contract::Vec2f first_output{};
    pipeline_contract::Vec2f second_output{};
    bool trigger_executed = false;
    bool first_target_aligned = false;
    bool late_flip = false;
    float output_axis_step = 0.0f;
    float ai_axis_step = 0.0f;
    float manual_axis_step = 0.0f;
    float avoidable_excess_step = 0.0f;
};

struct CounterfactualResult {
    bool compatible_manual_preserved = false;
    bool firing_down_preserved = false;
    bool no_target_manual_passthrough = false;
};

struct IncidentReport {
    std::vector<SequenceResult> coupled;
    std::vector<SequenceResult> explicit_axis;
    CounterfactualResult counterfactuals;
    bool trigger_executed = false;
    int ads_coupled_target_aligned = 0;
    int bodylock_high_coupled_target_aligned = 0;
    int explicit_late_flip_count = 0;
    float maximum_avoidable_excess_step = 0.0f;
    bool ads_initial_arbitration_pass = false;
    bool bodylock_high_initial_arbitration_pass = false;
    bool deadzone_continuity_pass = false;
    bool explicit_conflict_stability_pass = false;
    bool counterfactuals_pass = false;
    bool overall_pass = false;
};

AssistControlStateMachineInput input_for(
    Mode mode,
    double now_seconds,
    float visual_authority,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f filtered,
    pipeline_contract::Vec2f ai,
    bool firing = false,
    bool target_authoritative = true) {
    AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = target_authoritative;
    input.fresh_observation = true;
    input.target_id = target_authoritative ? 1401 : 0;
    input.selector_target_generation = target_authoritative ? 81 : 0;
    input.now_seconds = now_seconds;
    input.target_error_px = {-120.0f, 24.0f};
    input.mode = mode == Mode::Ads
        ? pipeline_contract::ControlMode::AdsAcquire
        : pipeline_contract::ControlMode::BodyLockFollow;
    input.visual_authority = visual_authority;
    input.firing = firing;
    input.manual_stick = manual;
    input.filtered_manual_stick = filtered;
    input.ai_stick = ai;
    return input;
}

SequenceResult run_sequence(
    const char* label,
    Mode mode,
    Axis axis,
    float visual_authority,
    bool coupled) {
    AssistControlStateMachine machine;
    const float first_orthogonal = coupled ? kDominantManual : 0.0f;
    const float second_orthogonal = coupled ? kDominantManualAfter : 0.0f;
    const float first_axis = coupled ? kCoupledManual : kExplicitManual;
    const float second_axis = coupled
        ? kCoupledManualAfter : kExplicitManualAfter;

    const auto first_manual = vector_for_axis(
        axis, first_axis, first_orthogonal);
    const auto second_manual = vector_for_axis(
        axis, second_axis, second_orthogonal);
    const auto first_filtered = first_manual;
    const auto second_filtered = vector_for_axis(
        axis, 0.0f, second_orthogonal);
    const auto first_ai = vector_for_axis(
        axis, kFirstAi, first_orthogonal);
    const auto second_ai = vector_for_axis(
        axis, kSecondAi, second_orthogonal);

    const auto first = machine.update(input_for(
        mode,
        100.000,
        visual_authority,
        first_manual,
        first_filtered,
        first_ai));
    const auto second = machine.update(input_for(
        mode,
        100.005,
        visual_authority,
        second_manual,
        second_filtered,
        second_ai));

    SequenceResult result;
    result.label = label;
    result.mode = mode_name(mode);
    result.axis = axis_name(axis);
    result.visual_authority = visual_authority;
    result.first_manual = first_manual;
    result.second_manual = second_manual;
    result.first_filtered = first_filtered;
    result.second_filtered = second_filtered;
    result.first_ai = first_ai;
    result.second_ai = second_ai;
    result.first_output = first.stick;
    result.second_output = second.stick;
    const float first_output_axis = axis_value(first.stick, axis);
    const float second_output_axis = axis_value(second.stick, axis);
    result.trigger_executed =
        first.phase == AssistControlPhase::Track &&
        second.phase == AssistControlPhase::Track &&
        first_axis * kFirstAi < 0.0f &&
        second_axis * kSecondAi < 0.0f &&
        axis_value(first_filtered, axis) != 0.0f &&
        axis_value(second_filtered, axis) == 0.0f;
    result.first_target_aligned = first_output_axis * kFirstAi > 0.0f;
    result.late_flip =
        first_output_axis * first_axis > 0.0f &&
        second_output_axis * kSecondAi > 0.0f;
    result.output_axis_step = std::fabs(
        second_output_axis - first_output_axis);
    result.ai_axis_step = std::fabs(kSecondAi - kFirstAi);
    result.manual_axis_step = std::fabs(second_axis - first_axis);
    result.avoidable_excess_step = std::max(
        0.0f,
        result.output_axis_step - result.ai_axis_step -
            result.manual_axis_step);
    return result;
}

CounterfactualResult run_counterfactuals() {
    CounterfactualResult result;

    {
        AssistControlStateMachine machine;
        auto input = input_for(
            Mode::Ads,
            200.0,
            1.0f,
            {0.40f, 0.0f},
            {0.40f, 0.0f},
            {0.25f, 0.0f});
        const auto output = machine.update(input);
        result.compatible_manual_preserved =
            std::fabs(output.stick.x - 0.40f) <= 1.0e-6f;
    }

    {
        AssistControlStateMachine machine;
        auto input = input_for(
            Mode::Ads,
            201.0,
            1.0f,
            {0.0f, -0.35f},
            {0.0f, -0.35f},
            {0.0f, 0.30f},
            true);
        const auto output = machine.update(input);
        result.firing_down_preserved =
            std::fabs(output.stick.y + 0.35f) <= 1.0e-6f;
    }

    {
        AssistControlStateMachine machine;
        auto input = input_for(
            Mode::Ads,
            202.0,
            0.0f,
            {0.22f, -0.18f},
            {0.22f, -0.18f},
            {},
            false,
            false);
        const auto output = machine.update(input);
        result.no_target_manual_passthrough =
            std::fabs(output.stick.x - 0.22f) <= 1.0e-6f &&
            std::fabs(output.stick.y + 0.18f) <= 1.0e-6f;
    }

    return result;
}

IncidentReport evaluate_incident() {
    IncidentReport report;
    for (Axis axis : {Axis::X, Axis::Y}) {
        report.coupled.push_back(run_sequence(
            "ads_coupled", Mode::Ads, axis, 1.0f, true));
        report.coupled.push_back(run_sequence(
            "bodylock_high_coupled", Mode::BodyLock, axis, 1.0f, true));
        report.coupled.push_back(run_sequence(
            "bodylock_low_coupled", Mode::BodyLock, axis, 0.20f, true));
        report.explicit_axis.push_back(run_sequence(
            "ads_explicit_axis", Mode::Ads, axis, 1.0f, false));
        report.explicit_axis.push_back(run_sequence(
            "bodylock_explicit_axis", Mode::BodyLock, axis, 1.0f, false));
    }
    report.counterfactuals = run_counterfactuals();

    report.trigger_executed = std::all_of(
        report.coupled.begin(), report.coupled.end(),
        [](const SequenceResult& value) { return value.trigger_executed; }) &&
        std::all_of(
            report.explicit_axis.begin(), report.explicit_axis.end(),
            [](const SequenceResult& value) { return value.trigger_executed; });
    for (const auto& value : report.coupled) {
        if (value.label == "ads_coupled" && value.first_target_aligned) {
            ++report.ads_coupled_target_aligned;
        }
        if (value.label == "bodylock_high_coupled" &&
            value.first_target_aligned) {
            ++report.bodylock_high_coupled_target_aligned;
        }
        report.maximum_avoidable_excess_step = std::max(
            report.maximum_avoidable_excess_step,
            value.avoidable_excess_step);
    }
    for (const auto& value : report.explicit_axis) {
        if (value.late_flip) ++report.explicit_late_flip_count;
        report.maximum_avoidable_excess_step = std::max(
            report.maximum_avoidable_excess_step,
            value.avoidable_excess_step);
    }

    report.ads_initial_arbitration_pass =
        report.ads_coupled_target_aligned == 2;
    report.bodylock_high_initial_arbitration_pass =
        report.bodylock_high_coupled_target_aligned == 2;
    report.deadzone_continuity_pass =
        report.maximum_avoidable_excess_step <=
        kMaximumAvoidableExcessStep;
    report.explicit_conflict_stability_pass =
        report.explicit_late_flip_count == 0;
    report.counterfactuals_pass =
        report.counterfactuals.compatible_manual_preserved &&
        report.counterfactuals.firing_down_preserved &&
        report.counterfactuals.no_target_manual_passthrough;
    report.overall_pass =
        report.trigger_executed &&
        report.ads_initial_arbitration_pass &&
        report.bodylock_high_initial_arbitration_pass &&
        report.deadzone_continuity_pass &&
        report.explicit_conflict_stability_pass &&
        report.counterfactuals_pass;
    return report;
}

void write_vec(std::ofstream& output, pipeline_contract::Vec2f value) {
    output << '[' << value.x << ',' << value.y << ']';
}

void write_sequence(
    std::ofstream& output,
    const SequenceResult& value,
    const std::string& indent) {
    const std::string inner = indent + "  ";
    output << indent << "{\n"
           << inner << "\"label\": \"" << value.label << "\",\n"
           << inner << "\"mode\": \"" << value.mode << "\",\n"
           << inner << "\"axis\": \"" << value.axis << "\",\n"
           << inner << "\"visual_authority\": "
           << value.visual_authority << ",\n"
           << inner << "\"first_manual\": ";
    write_vec(output, value.first_manual);
    output << ",\n" << inner << "\"second_manual\": ";
    write_vec(output, value.second_manual);
    output << ",\n" << inner << "\"first_filtered\": ";
    write_vec(output, value.first_filtered);
    output << ",\n" << inner << "\"second_filtered\": ";
    write_vec(output, value.second_filtered);
    output << ",\n" << inner << "\"first_ai\": ";
    write_vec(output, value.first_ai);
    output << ",\n" << inner << "\"second_ai\": ";
    write_vec(output, value.second_ai);
    output << ",\n" << inner << "\"first_output\": ";
    write_vec(output, value.first_output);
    output << ",\n" << inner << "\"second_output\": ";
    write_vec(output, value.second_output);
    output << ",\n"
           << inner << "\"trigger_executed\": "
           << value.trigger_executed << ",\n"
           << inner << "\"first_target_aligned\": "
           << value.first_target_aligned << ",\n"
           << inner << "\"late_flip\": " << value.late_flip << ",\n"
           << inner << "\"output_axis_step\": "
           << value.output_axis_step << ",\n"
           << inner << "\"ai_axis_step\": "
           << value.ai_axis_step << ",\n"
           << inner << "\"manual_axis_step\": "
           << value.manual_axis_step << ",\n"
           << inner << "\"avoidable_excess_step\": "
           << value.avoidable_excess_step << "\n"
           << indent << '}';
}

void write_sequence_array(
    std::ofstream& output,
    const std::vector<SequenceResult>& values,
    const std::string& indent) {
    output << "[\n";
    for (std::size_t index = 0; index < values.size(); ++index) {
        write_sequence(output, values[index], indent + "  ");
        output << (index + 1 == values.size() ? "\n" : ",\n");
    }
    output << indent << ']';
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return std::filesystem::path(argv[2]);
    }
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

void write_report(
    const std::filesystem::path& output_path,
    const IncidentReport& report) {
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path, std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open report output");
    output << std::boolalpha << std::fixed << std::setprecision(6);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"manual/AI conflict is visible on the first sample but arbitration waits for filtered manual to reach zero, then flips the final axis\",\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"coupled_sequences\": ";
    write_sequence_array(output, report.coupled, "  ");
    output << ",\n  \"explicit_axis_sequences\": ";
    write_sequence_array(output, report.explicit_axis, "  ");
    output << ",\n"
           << "  \"counterfactuals\": {\n"
           << "    \"compatible_manual_preserved\": "
           << report.counterfactuals.compatible_manual_preserved << ",\n"
           << "    \"firing_down_preserved\": "
           << report.counterfactuals.firing_down_preserved << ",\n"
           << "    \"no_target_manual_passthrough\": "
           << report.counterfactuals.no_target_manual_passthrough << "\n"
           << "  },\n"
           << "  \"metrics\": {\n"
           << "    \"ads_coupled_target_aligned_count\": "
           << report.ads_coupled_target_aligned << ",\n"
           << "    \"bodylock_high_coupled_target_aligned_count\": "
           << report.bodylock_high_coupled_target_aligned << ",\n"
           << "    \"explicit_late_flip_count\": "
           << report.explicit_late_flip_count << ",\n"
           << "    \"maximum_avoidable_excess_step\": "
           << report.maximum_avoidable_excess_step << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"ads_coupled_target_aligned_count\",\"operator\":\">=\",\"threshold\":2,\"observed\":"
           << report.ads_coupled_target_aligned << ",\"pass\":"
           << report.ads_initial_arbitration_pass << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"bodylock_high_coupled_target_aligned_count\",\"operator\":\">=\",\"threshold\":2,\"observed\":"
           << report.bodylock_high_coupled_target_aligned << ",\"pass\":"
           << report.bodylock_high_initial_arbitration_pass << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"maximum_avoidable_excess_step\",\"operator\":\"<=\",\"threshold\":"
           << kMaximumAvoidableExcessStep << ",\"observed\":"
           << report.maximum_avoidable_excess_step << ",\"pass\":"
           << report.deadzone_continuity_pass << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"explicit_late_flip_count\",\"operator\":\"==\",\"threshold\":0,\"observed\":"
           << report.explicit_late_flip_count << ",\"pass\":"
           << report.explicit_conflict_stability_pass << "},\n"
           << "    {\"id\":\"O5\",\"metric\":\"counterfactuals_pass\",\"operator\":\"==\",\"threshold\":true,\"observed\":"
           << report.counterfactuals_pass << ",\"pass\":"
           << report.counterfactuals_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const IncidentReport report = evaluate_incident();
        write_report(output_path_from_args(argc, argv), report);
        std::cout << "[ManualAiArbitrationContinuityIncident] trigger="
                  << (report.trigger_executed ? 1 : 0)
                  << " ads_initial=" << report.ads_coupled_target_aligned
                  << " body_high_initial="
                  << report.bodylock_high_coupled_target_aligned
                  << " excess_step="
                  << report.maximum_avoidable_excess_step
                  << " late_flips=" << report.explicit_late_flip_count
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_pass) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[ManualAiArbitrationContinuityIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[ManualAiArbitrationContinuityIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}
