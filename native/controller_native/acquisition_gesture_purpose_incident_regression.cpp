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
    "acquisition-gesture-purpose-20260811";
constexpr float kDefaultV = 0.365f;
constexpr float kManualX = 0.30f;
constexpr float kManualY = -0.20f;
constexpr float kMaximumAcquisitionDShift = 1.0e-6f;
constexpr float kMinimumNewGestureDShift = 0.005f;
constexpr std::uint64_t kObservationId = 8201;
constexpr std::uint64_t kSelectorGeneration = 121;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 80.0f, 160.0f, kDefaultV,
    kObservationId, kSelectorGeneration, true, false, false};

struct CaseResult {
    const char* name = "";
    bool target_owned = false;
    bool acquisition_manual_material = false;
    bool same_manual_continued = false;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool vision_default = false;
    float desired_u = 0.0f;
    float desired_v = 0.0f;
    float d_shift = 0.0f;
};

struct Report {
    CaseResult carry_in;
    CaseResult neutral_acquisition;
    CaseResult new_owned_gesture;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool acquisition_d_preserved = false;
    bool acquisition_not_tagged_correction = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 240.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;
    config.ai_aim.desired_point_traversal_ms = 180.0f;
    return config;
}

PhysicalGamepadState physical(float x, float y) {
    return controller_native::incident_fixture::ads_input(x, y);
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now) {
    return controller_native::incident_fixture::observed_snapshot(
        kTarget, frame_id, now, 48.0f, -24.0f);
}

CaseResult collect_case(
    const char* name,
    NativeGamepadController& controller,
    bool acquisition_manual_material,
    bool same_manual_continued) {
    const auto& plan = controller.last_target_plan();
    const auto& components = controller.last_output_components();
    const float du = plan.desired_point_normalized.x - 0.5f;
    const float dv = plan.desired_point_normalized.y - kDefaultV;
    CaseResult result;
    result.name = name;
    result.target_owned =
        plan.target_id != 0 &&
        plan.selector_target_generation == kSelectorGeneration &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        plan.aim_authority > 0.0f;
    result.acquisition_manual_material = acquisition_manual_material;
    result.same_manual_continued = same_manual_continued;
    result.manual_correction_x = components.manual_correction_x;
    result.manual_correction_y = components.manual_correction_y;
    result.vision_default =
        plan.desired_point_source ==
        pipeline_contract::DesiredPointSource::VisionDefault;
    result.desired_u = plan.desired_point_normalized.x;
    result.desired_v = plan.desired_point_normalized.y;
    result.d_shift = std::hypot(du, dv);
    return result;
}

CaseResult run_carry_in_case() {
    double now = 100.0;
    NativeGamepadController controller(
        incident_config(), &now);

    // The gesture begins while no target is owned.
    (void)controller.build_output(physical(kManualX, kManualY));
    const auto& pre = controller.last_output_components();
    const bool material =
        std::hypot(
            pre.filtered_manual_stick.x,
            pre.filtered_manual_stick.y) > 0.05f;
    now += 0.005;

    // The exact same physical gesture continues through first ownership.
    controller.submit_vision_snapshot(observed_snapshot(1, now));
    (void)controller.build_output(physical(kManualX, kManualY));
    return collect_case("carry_in", controller, material, true);
}

CaseResult run_neutral_acquisition_case() {
    double now = 200.0;
    NativeGamepadController controller(
        incident_config(), &now);
    (void)controller.build_output(physical(0.0f, 0.0f));
    now += 0.005;
    controller.submit_vision_snapshot(observed_snapshot(1, now));
    (void)controller.build_output(physical(0.0f, 0.0f));
    return collect_case("neutral_acquisition", controller, false, false);
}

CaseResult run_new_owned_gesture_case() {
    double now = 300.0;
    NativeGamepadController controller(
        incident_config(), &now);
    (void)controller.build_output(physical(0.0f, 0.0f));
    now += 0.005;
    controller.submit_vision_snapshot(observed_snapshot(1, now));
    (void)controller.build_output(physical(0.0f, 0.0f));
    now += 0.005;
    controller.submit_vision_snapshot(observed_snapshot(2, now));
    (void)controller.build_output(physical(kManualX, kManualY));
    return collect_case("new_owned_gesture", controller, false, false);
}

Report evaluate() {
    Report report;
    report.carry_in = run_carry_in_case();
    report.neutral_acquisition = run_neutral_acquisition_case();
    report.new_owned_gesture = run_new_owned_gesture_case();
    report.trigger_executed =
        report.carry_in.target_owned &&
        report.carry_in.acquisition_manual_material &&
        report.carry_in.same_manual_continued;
    report.counterfactuals_valid =
        report.neutral_acquisition.target_owned &&
        report.neutral_acquisition.d_shift <= kMaximumAcquisitionDShift &&
        report.neutral_acquisition.vision_default &&
        report.new_owned_gesture.target_owned &&
        report.new_owned_gesture.d_shift >= kMinimumNewGestureDShift &&
        report.new_owned_gesture.manual_correction_x &&
        report.new_owned_gesture.manual_correction_y;
    report.acquisition_d_preserved =
        report.carry_in.d_shift <= kMaximumAcquisitionDShift &&
        report.carry_in.vision_default;
    report.acquisition_not_tagged_correction =
        !report.carry_in.manual_correction_x &&
        !report.carry_in.manual_correction_y;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactuals_valid &&
        report.acquisition_d_preserved &&
        report.acquisition_not_tagged_correction;
    return report;
}

void write_case(std::ostream& stream, const CaseResult& value, bool comma) {
    stream << "    \"" << value.name << "\": {\n"
           << "      \"target_owned\": " << std::boolalpha
           << value.target_owned << ",\n"
           << "      \"acquisition_manual_material\": "
           << value.acquisition_manual_material << ",\n"
           << "      \"same_manual_continued\": "
           << value.same_manual_continued << ",\n"
           << "      \"manual_correction_x\": "
           << value.manual_correction_x << ",\n"
           << "      \"manual_correction_y\": "
           << value.manual_correction_y << ",\n"
           << "      \"vision_default\": " << value.vision_default << ",\n"
           << "      \"desired_u\": " << value.desired_u << ",\n"
           << "      \"desired_v\": " << value.desired_v << ",\n"
           << "      \"d_shift\": " << value.d_shift << "\n"
           << "    }" << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::fixed << std::setprecision(7)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"thresholds\": {\n"
           << "    \"maximum_acquisition_d_shift\": "
           << kMaximumAcquisitionDShift << ",\n"
           << "    \"minimum_new_gesture_d_shift\": "
           << kMinimumNewGestureDShift << "\n"
           << "  },\n"
           << "  \"cases\": {\n";
    write_case(stream, report.carry_in, true);
    write_case(stream, report.neutral_acquisition, true);
    write_case(stream, report.new_owned_gesture, false);
    stream << "  },\n"
           << "  \"trigger_executed\": " << std::boolalpha
           << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"acquisition_d_preserved\": "
           << report.acquisition_d_preserved << ",\n"
           << "    \"acquisition_not_tagged_correction\": "
           << report.acquisition_not_tagged_correction << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_acquisition_gesture_purpose_incident_regression(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv, "acquisition_gesture_purpose_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " carry_d_shift=" << report.carry_in.d_shift
                  << " carry_correction="
                  << (report.carry_in.manual_correction_x ||
                      report.carry_in.manual_correction_y)
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_acquisition_gesture_purpose_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_acquisition_gesture_purpose", "acquisition_gesture_purpose_incident.json", run_acquisition_gesture_purpose_incident_regression);
}
