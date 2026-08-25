#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

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
    "far-selected-person-ads-admission-20260811";
constexpr float kFarDx = 250.0f;
constexpr float kNearDx = 80.0f;
constexpr float kMinimumMaterialAi = 0.05f;
constexpr std::uint64_t kObservationId = 9101;
constexpr std::uint64_t kSelectorGeneration = 141;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 50.0f, 120.0f, 0.365f,
    kObservationId, kSelectorGeneration, true, true, false};

struct CaseResult {
    const char* name = "";
    float target_distance_px = 0.0f;
    bool vision_selected_person = false;
    bool person_has_enemy_cue = false;
    bool controller_received_candidate = false;
    bool target_admitted = false;
    float configured_ads_radius_px = 0.0f;
    float ai_magnitude = 0.0f;
    float final_magnitude = 0.0f;
};

struct Report {
    CaseResult far_selected_person;
    CaseResult near_counterfactual;
    bool trigger_executed = false;
    bool counterfactual_valid = false;
    bool far_target_admitted = false;
    bool far_ai_material = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 135.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_extension_budget_ms = 500.0f;
    return config;
}

PhysicalGamepadState neutral_ads() {
    return controller_native::incident_fixture::ads_input();
}

ControllerVisionSnapshot selected_person_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx) {
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, dx, 0.0f, true);
}

CaseResult run_case(const char* name, float dx) {
    double now = 100.0;
    const auto config = incident_config();
    NativeGamepadController controller(config, &now);
    controller.submit_vision_snapshot(selected_person_snapshot(1, now, dx));
    const auto output = controller.build_output(neutral_ads());
    const auto& plan = controller.last_target_plan();
    const auto& components = controller.last_output_components();

    CaseResult result;
    result.name = name;
    result.target_distance_px = std::fabs(dx);
    result.vision_selected_person = true;
    result.person_has_enemy_cue = false;
    result.controller_received_candidate =
        plan.ads_candidate_count == 1 &&
        plan.ads_preferred_source_id == kObservationId;
    result.target_admitted =
        plan.target_id != 0 &&
        plan.selector_target_generation == kSelectorGeneration &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        plan.aim_authority > 0.0f;
    result.configured_ads_radius_px = config.ai_aim.ads_activation_radius_px;
    result.ai_magnitude = std::hypot(
        components.shaped_assist_stick.x,
        components.shaped_assist_stick.y);
    result.final_magnitude = std::hypot(output.right_x, output.right_y);
    return result;
}

Report evaluate() {
    Report report;
    report.far_selected_person = run_case("far_selected_person", kFarDx);
    report.near_counterfactual = run_case("near_counterfactual", kNearDx);
    report.trigger_executed =
        report.far_selected_person.vision_selected_person &&
        !report.far_selected_person.person_has_enemy_cue &&
        report.far_selected_person.controller_received_candidate &&
        report.far_selected_person.target_distance_px >
            report.far_selected_person.configured_ads_radius_px;
    report.counterfactual_valid =
        report.near_counterfactual.vision_selected_person &&
        !report.near_counterfactual.person_has_enemy_cue &&
        report.near_counterfactual.controller_received_candidate &&
        report.near_counterfactual.target_admitted &&
        report.near_counterfactual.ai_magnitude >= kMinimumMaterialAi;
    report.far_target_admitted = report.far_selected_person.target_admitted;
    report.far_ai_material =
        report.far_selected_person.ai_magnitude >= kMinimumMaterialAi;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactual_valid &&
        report.far_target_admitted &&
        report.far_ai_material;
    return report;
}

void write_case(std::ofstream& stream, const CaseResult& value, bool comma) {
    stream << "    \"" << value.name << "\": {\n"
           << "      \"target_distance_px\": " << value.target_distance_px << ",\n"
           << "      \"vision_selected_person\": "
           << value.vision_selected_person << ",\n"
           << "      \"person_has_enemy_cue\": "
           << value.person_has_enemy_cue << ",\n"
           << "      \"controller_received_candidate\": "
           << value.controller_received_candidate << ",\n"
           << "      \"target_admitted\": " << value.target_admitted << ",\n"
           << "      \"configured_ads_radius_px\": "
           << value.configured_ads_radius_px << ",\n"
           << "      \"ai_magnitude\": " << value.ai_magnitude << ",\n"
           << "      \"final_magnitude\": " << value.final_magnitude << "\n"
           << "    }" << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"thresholds\": {\n"
           << "    \"minimum_material_ai\": " << kMinimumMaterialAi << "\n"
           << "  },\n"
           << "  \"cases\": {\n";
    write_case(stream, report.far_selected_person, true);
    write_case(stream, report.near_counterfactual, false);
    stream << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactual_valid\": "
           << report.counterfactual_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"far_target_admitted\": "
           << report.far_target_admitted << ",\n"
           << "    \"far_ai_material\": " << report.far_ai_material << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_far_selected_person_ads_admission_incident_regression(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv,
                "far_selected_person_ads_admission_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " far_admitted=" << report.far_target_admitted
                  << " far_ai=" << report.far_selected_person.ai_magnitude
                  << " near_admitted="
                  << report.near_counterfactual.target_admitted
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_far_selected_person_ads_admission_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_far_selected_person_ads_admission", "far_selected_person_ads_admission_incident.json", run_far_selected_person_ads_admission_incident_regression);
}
