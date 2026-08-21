#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "ads-initial-scope-existing-target-20260817";
constexpr float kDx = 21.0f;
constexpr float kDy = 28.0f;
constexpr float kMinimumMaterialAssist = 0.05f;
constexpr std::uint64_t kObservationId = 101176544591873ull;
constexpr std::uint64_t kSelectorGeneration = 192;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 56.0f, 140.0f, 0.365f,
    kObservationId, kSelectorGeneration, true, true, false};

struct CaseResult {
    const char* name = "";
    bool target_visible_before_lt = false;
    bool physical_lt_rose = false;
    bool fresh_same_target_after_lt = false;
    bool ads_mode = false;
    bool full_authority = false;
    bool acquisition_exists = false;
    float assist_magnitude = 0.0f;
    std::uint64_t target_id = 0;
    std::uint64_t acquisition_id = 0;
};

struct Report {
    CaseResult observed;
    CaseResult lt_before_target_counterfactual;
    bool trigger_executed = false;
    bool counterfactual_valid = false;
    bool observed_ads_mode = false;
    bool observed_full_authority = false;
    bool observed_acquisition_exists = false;
    bool observed_material_assist = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 135.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;
    return config;
}

PhysicalGamepadState neutral_input() {
    PhysicalGamepadState physical;
    physical.connected = true;
    return physical;
}

ControllerVisionSnapshot target_snapshot(
    std::uint64_t frame_id,
    double now) {
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, kDx, kDy, frame_id == 1);
}

void capture_final_state(
    CaseResult* result,
    const NativeGamepadController& controller) {
    const auto& plan = controller.last_target_plan();
    const auto& components = controller.last_output_components();
    result->ads_mode = plan.mode == pipeline_contract::ControlMode::AdsAcquire;
    result->full_authority = plan.aim_authority >= 0.999f;
    result->acquisition_exists =
        plan.ads_acquisition_exists && plan.target_acquisition_id != 0;
    result->assist_magnitude = std::hypot(
        components.shaped_assist_stick.x,
        components.shaped_assist_stick.y);
    result->target_id = plan.target_id;
    result->acquisition_id = plan.target_acquisition_id;
}

CaseResult run_observed_case() {
    double now = 100.0;
    NativeGamepadController controller(incident_config(), &now);

    controller.submit_vision_snapshot(target_snapshot(1, now));
    (void)controller.build_output(neutral_input());
    const auto before_lt = controller.last_target_plan();

    now += 0.005;
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());

    now += 0.005;
    controller.submit_vision_snapshot(target_snapshot(2, now));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());

    CaseResult result;
    result.name = "target_visible_before_initial_lt";
    result.target_visible_before_lt =
        before_lt.target_id != 0 &&
        before_lt.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        before_lt.mode == pipeline_contract::ControlMode::Manual;
    result.physical_lt_rose = true;
    result.fresh_same_target_after_lt = true;
    capture_final_state(&result, controller);
    return result;
}

CaseResult run_lt_before_target_counterfactual() {
    double now = 200.0;
    NativeGamepadController controller(incident_config(), &now);

    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    now += 0.005;
    controller.submit_vision_snapshot(target_snapshot(1, now));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());

    CaseResult result;
    result.name = "initial_lt_before_target";
    result.target_visible_before_lt = false;
    result.physical_lt_rose = true;
    result.fresh_same_target_after_lt = true;
    capture_final_state(&result, controller);
    return result;
}

Report evaluate() {
    Report report;
    report.observed = run_observed_case();
    report.lt_before_target_counterfactual =
        run_lt_before_target_counterfactual();
    report.trigger_executed =
        report.observed.target_visible_before_lt &&
        report.observed.physical_lt_rose &&
        report.observed.fresh_same_target_after_lt &&
        report.observed.target_id != 0;
    report.counterfactual_valid =
        report.lt_before_target_counterfactual.ads_mode &&
        report.lt_before_target_counterfactual.full_authority &&
        report.lt_before_target_counterfactual.acquisition_exists &&
        report.lt_before_target_counterfactual.assist_magnitude >=
            kMinimumMaterialAssist;
    report.observed_ads_mode = report.observed.ads_mode;
    report.observed_full_authority = report.observed.full_authority;
    report.observed_acquisition_exists = report.observed.acquisition_exists;
    report.observed_material_assist =
        report.observed.assist_magnitude >= kMinimumMaterialAssist;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactual_valid &&
        report.observed_ads_mode &&
        report.observed_full_authority &&
        report.observed_acquisition_exists &&
        report.observed_material_assist;
    return report;
}

void write_case(std::ostream& stream, const CaseResult& value) {
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\"target_visible_before_lt\":"
           << value.target_visible_before_lt
           << ",\"physical_lt_rose\":" << value.physical_lt_rose
           << ",\"fresh_same_target_after_lt\":"
           << value.fresh_same_target_after_lt
           << ",\"ads_mode\":" << value.ads_mode
           << ",\"full_authority\":" << value.full_authority
           << ",\"acquisition_exists\":" << value.acquisition_exists
           << ",\"assist_magnitude\":" << value.assist_magnitude
           << ",\"target_id\":" << value.target_id
           << ",\"acquisition_id\":" << value.acquisition_id << "}";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"An initial physical LT press with an already-visible selected person leaves ADS armed-waiting, emits zero assist, and never acquires control authority\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"fresh selected target before LT; fresh same-generation target 5 ms after LT\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"zero\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"disabled and not firing\",\n"
           << "    \"controller_mode\": \"production NativeGamepadController\",\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"thresholds\": {\"minimum_material_assist\":"
           << kMinimumMaterialAssist << "},\n"
           << "  \"cases\": {\n"
           << "    \"observed\": ";
    write_case(stream, report.observed);
    stream << ",\n    \"lt_before_target_counterfactual\": ";
    write_case(stream, report.lt_before_target_counterfactual);
    stream << "\n  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactual_valid\": "
           << report.counterfactual_valid << ",\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"observed_ads_mode\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.observed_ads_mode << ",\"pass\":" << report.observed_ads_mode << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"observed_full_authority\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.observed_full_authority << ",\"pass\":" << report.observed_full_authority << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"observed_acquisition_exists\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.observed_acquisition_exists << ",\"pass\":" << report.observed_acquisition_exists << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"observed_assist_magnitude\",\"operator\":\">=\",\"threshold\":" << kMinimumMaterialAssist << ",\"observed\":" << report.observed.assist_magnitude << ",\"pass\":" << report.observed_material_assist << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_initial_scope_existing_target_incident_regression(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv,
                "ads_initial_scope_existing_target_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " observed_mode=" << report.observed_ads_mode
                  << " observed_authority=" << report.observed_full_authority
                  << " observed_assist=" << report.observed.assist_magnitude
                  << " counterfactual=" << report.counterfactual_valid
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactual_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 3;
    }
}

void register_ads_initial_scope_existing_target_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_ads_initial_scope_existing_target", "ads_initial_scope_existing_target_incident.json", run_ads_initial_scope_existing_target_incident_regression);
}
