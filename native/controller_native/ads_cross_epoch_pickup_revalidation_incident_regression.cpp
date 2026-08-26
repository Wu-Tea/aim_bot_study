#include "incident_fixture_support.h"
#include "pipeline_contract/target_acquisition.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "ads-cross-epoch-pickup-revalidation-20260825";
constexpr float kFrameWidth = 640.0f;
constexpr float kFrameHeight = 512.0f;
constexpr float kPickupBaseRadiusPx = 150.0f;
constexpr float kPrimaryBodyWidthPx = 86.8125f;
constexpr float kPrimaryBodyHeightPx = 240.333f;
constexpr float kFirstScopeDxPx = 180.0f;
constexpr float kFarSecondScopeDxPx = 277.74f;
constexpr float kInsideSecondScopeDxPx = 195.0f;
constexpr float kAiZeroTolerance = 0.001f;

struct TwoScopeResult {
    const char* name = "";
    float second_scope_distance_px = 0.0f;
    float effective_pickup_radius_px = 0.0f;
    std::uint64_t first_ads_epoch = 0;
    std::uint64_t second_ads_epoch = 0;
    std::uint64_t first_selector_generation = 0;
    std::uint64_t second_selector_generation = 0;
    bool first_scope_selected = false;
    bool first_scope_admitted = false;
    bool release_completed = false;
    bool second_scope_selected = false;
    bool second_scope_selector_changed = false;
    bool second_scope_admitted = false;
    float second_scope_ai_magnitude = 0.0f;
};

struct Report {
    TwoScopeResult stale_far;
    TwoScopeResult inside_envelope;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    int second_epoch_far_admissions = 0;
    float second_epoch_far_ai_magnitude = 0.0f;
    bool overall_pass = false;
};

float effective_pickup_radius(float body_height_px) {
    return pipeline_contract::target_scaled_pickup_radius(
        kPickupBaseRadiusPx,
        body_height_px / kFrameHeight);
}

controller_native::ControllerVisionSnapshot selected_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    bool selector_changed = false) {
    controller_native::incident_fixture::TargetSpec target;
    target.center_x = kFrameWidth * 0.5f;
    target.center_y = kFrameHeight * 0.5f;
    target.body_width = kPrimaryBodyWidthPx;
    target.body_height = kPrimaryBodyHeightPx;
    target.aim_height_ratio = 0.40f;
    target.observation_id = frame_id;
    target.selector_generation = 1;
    return controller_native::incident_fixture::observed_snapshot(
        target, frame_id, now, dx, 0.0f, selector_changed);
}

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 135.0f);
    config.ai_aim.ads_pickup_base_radius_px = kPickupBaseRadiusPx;
    config.ai_aim.ads_scope_ready_trigger = 0.80f;
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_target_wait_ms = 500.0f;
    config.ai_aim.ads_extension_budget_ms = 500.0f;
    return config;
}

PhysicalGamepadState neutral_input() {
    PhysicalGamepadState input;
    input.connected = true;
    return input;
}

TwoScopeResult run_two_scope_case(
    const char* name,
    float second_scope_dx) {
    TwoScopeResult value;
    value.name = name;
    value.second_scope_distance_px = std::fabs(second_scope_dx);
    value.effective_pickup_radius_px =
        effective_pickup_radius(kPrimaryBodyHeightPx);

    double now = 100.0;
    std::uint64_t frame_id = 1;
    NativeGamepadController controller(incident_config(), &now);

    const auto first_selected = selected_snapshot(
        frame_id++, now, kFirstScopeDxPx, true);
    value.first_scope_selected = first_selected.state.has_target;
    value.first_selector_generation =
        first_selected.selector_target_generation;
    controller.submit_vision_snapshot(first_selected);
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    const auto first_plan = controller.last_target_plan();
    value.first_ads_epoch = controller.ads_epoch();
    value.first_scope_admitted = first_plan.ads_plan_admitted;

    const auto neutral = neutral_input();
    for (int sample = 0; sample < 3; ++sample) {
        now += 0.001;
        (void)controller.build_output(neutral);
    }
    value.release_completed =
        controller.last_target_plan().mode ==
            pipeline_contract::ControlMode::Manual &&
        controller.ads_epoch() == value.first_ads_epoch;

    float current_dx = kFirstScopeDxPx;
    while (std::fabs(second_scope_dx - current_dx) > 35.0f) {
        current_dx += second_scope_dx > current_dx ? 35.0f : -35.0f;
        now += 0.006;
        controller.submit_vision_snapshot(selected_snapshot(
            frame_id++, now, current_dx));
        (void)controller.build_output(neutral);
    }
    if (current_dx != second_scope_dx) {
        current_dx = second_scope_dx;
        now += 0.006;
        controller.submit_vision_snapshot(selected_snapshot(
            frame_id++, now, current_dx));
        (void)controller.build_output(neutral);
    }

    now += 0.006;
    const auto second_selected = selected_snapshot(
        frame_id++, now, second_scope_dx);
    value.second_scope_selected = second_selected.state.has_target;
    value.second_selector_generation =
        second_selected.selector_target_generation;
    value.second_scope_selector_changed =
        second_selected.selector_target_changed;
    controller.submit_vision_snapshot(second_selected);
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    const auto second_plan = controller.last_target_plan();
    const auto& components = controller.last_output_components();
    value.second_ads_epoch = controller.ads_epoch();
    value.second_scope_admitted = second_plan.ads_plan_admitted;
    value.second_scope_ai_magnitude = std::hypot(
        components.shaped_assist_stick.x,
        components.shaped_assist_stick.y);
    return value;
}

Report evaluate() {
    Report report;
    report.stale_far = run_two_scope_case(
        "stale_far_same_generation", kFarSecondScopeDxPx);
    report.inside_envelope = run_two_scope_case(
        "inside_envelope_same_generation", kInsideSecondScopeDxPx);

    report.trigger_executed =
        report.stale_far.first_scope_selected &&
        report.stale_far.first_scope_admitted &&
        report.stale_far.release_completed &&
        report.stale_far.second_ads_epoch > report.stale_far.first_ads_epoch &&
        report.stale_far.second_scope_selected &&
        !report.stale_far.second_scope_selector_changed &&
        report.stale_far.first_selector_generation != 0 &&
        report.stale_far.second_selector_generation ==
            report.stale_far.first_selector_generation &&
        report.stale_far.second_scope_distance_px >
            report.stale_far.effective_pickup_radius_px;
    report.counterfactuals_valid =
        report.inside_envelope.second_scope_distance_px <=
            report.inside_envelope.effective_pickup_radius_px &&
        report.inside_envelope.first_scope_admitted &&
        report.inside_envelope.release_completed &&
        report.inside_envelope.second_ads_epoch >
            report.inside_envelope.first_ads_epoch &&
        report.inside_envelope.second_selector_generation ==
            report.inside_envelope.first_selector_generation &&
        report.inside_envelope.second_scope_admitted;
    report.second_epoch_far_admissions =
        report.stale_far.second_scope_admitted ? 1 : 0;
    report.second_epoch_far_ai_magnitude =
        report.stale_far.second_scope_ai_magnitude;
    report.overall_pass =
        report.trigger_executed &&
        report.counterfactuals_valid &&
        report.second_epoch_far_admissions == 0 &&
        report.second_epoch_far_ai_magnitude <= kAiZeroTolerance;
    return report;
}

void write_two_scope_case(
    std::ofstream& stream,
    const TwoScopeResult& value,
    bool comma) {
    stream << "    \"" << value.name << "\": {\n"
           << "      \"second_scope_distance_px\": "
           << value.second_scope_distance_px << ",\n"
           << "      \"effective_pickup_radius_px\": "
           << value.effective_pickup_radius_px << ",\n"
           << "      \"first_ads_epoch\": " << value.first_ads_epoch << ",\n"
           << "      \"second_ads_epoch\": " << value.second_ads_epoch << ",\n"
           << "      \"first_selector_generation\": "
           << value.first_selector_generation << ",\n"
           << "      \"second_selector_generation\": "
           << value.second_selector_generation << ",\n"
           << "      \"first_scope_selected\": "
           << value.first_scope_selected << ",\n"
           << "      \"first_scope_admitted\": "
           << value.first_scope_admitted << ",\n"
           << "      \"release_completed\": "
           << value.release_completed << ",\n"
           << "      \"second_scope_selected\": "
           << value.second_scope_selected << ",\n"
           << "      \"second_scope_selector_changed\": "
           << value.second_scope_selector_changed << ",\n"
           << "      \"second_scope_admitted\": "
           << value.second_scope_admitted << ",\n"
           << "      \"second_scope_ai_magnitude\": "
           << value.second_scope_ai_magnitude << "\n"
           << "    }" << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"thresholds\": {\n"
           << "    \"pickup_base_radius_px\": " << kPickupBaseRadiusPx
           << ",\n"
           << "    \"ai_zero_tolerance\": " << kAiZeroTolerance << "\n"
           << "  },\n"
           << "  \"cases\": {\n";
    write_two_scope_case(stream, report.stale_far, true);
    write_two_scope_case(stream, report.inside_envelope, false);
    stream << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"second_epoch_far_admissions\": "
           << report.second_epoch_far_admissions << ",\n"
           << "    \"second_epoch_far_ai_magnitude\": "
           << report.second_epoch_far_ai_magnitude << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_cross_epoch_pickup_revalidation_incident_regression(
    int argc,
    char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc,
                argv,
                "ads_cross_epoch_pickup_revalidation_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " trigger=" << report.trigger_executed
                  << " counterfactuals=" << report.counterfactuals_valid
                  << " far_second_epoch_admissions="
                  << report.second_epoch_far_admissions
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_ads_cross_epoch_pickup_revalidation_incident_regression(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseAds",
        "incident_ads_cross_epoch_pickup_revalidation",
        "ads_cross_epoch_pickup_revalidation_incident.json",
        run_ads_cross_epoch_pickup_revalidation_incident_regression);
}
