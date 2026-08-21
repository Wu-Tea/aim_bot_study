#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::GamepadOutputState;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "ads-scope-ready-authority-20260821";
constexpr float kScopeReadyTrigger = 0.80f;
constexpr float kSlowTrigger = 0.45f;
constexpr float kManualX = -0.341187f;
constexpr float kManualY = 0.184326f;
constexpr float kMaximumPreReadyDeviation = 0.02f;
constexpr float kMinimumPostReadyDeviation = 0.05f;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f, 256.0f, 53.0f, 112.167f, 0.365f,
    43001, 55, true, false, false};

struct Report {
    float slow_trigger = kSlowTrigger;
    float scope_ready_trigger = kScopeReadyTrigger;
    float maximum_pre_ready_output_deviation = 0.0f;
    float maximum_post_ready_output_deviation = 0.0f;
    bool pre_ready_plan_admitted = false;
    bool post_ready_plan_admitted = false;
    bool trigger_executed = false;
    bool pre_ready_manual_preserved = false;
    bool post_ready_assist_available = false;
    bool overall_pass = false;
};

float output_deviation(const GamepadOutputState& output) {
    return std::hypot(output.right_x - kManualX, output.right_y - kManualY);
}

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 120.0f);
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;
    return config;
}

PhysicalGamepadState input(float left_trigger) {
    auto physical = controller_native::incident_fixture::ads_input(
        kManualX, kManualY);
    physical.left_trigger = left_trigger;
    return physical;
}

Report evaluate() {
    double now = 100.0;
    NativeGamepadController controller(incident_config(), &now);
    Report report;
    std::uint64_t frame_id = 1;

    for (int sample = 0; sample < 5; ++sample) {
        controller.submit_vision_snapshot(
            controller_native::incident_fixture::observed_snapshot(
                kTarget, frame_id++, now, -67.1667f, -6.46666f,
                sample == 0));
        const auto output = controller.build_output(input(kSlowTrigger));
        report.maximum_pre_ready_output_deviation = std::max(
            report.maximum_pre_ready_output_deviation,
            output_deviation(output));
        report.pre_ready_plan_admitted = report.pre_ready_plan_admitted ||
            controller.last_target_plan().ads_plan_admitted;
        now += 0.020;
    }

    for (int sample = 0; sample < 5; ++sample) {
        controller.submit_vision_snapshot(
            controller_native::incident_fixture::observed_snapshot(
                kTarget, frame_id++, now, -67.1667f, -6.46666f));
        const auto output = controller.build_output(input(1.0f));
        report.maximum_post_ready_output_deviation = std::max(
            report.maximum_post_ready_output_deviation,
            output_deviation(output));
        report.post_ready_plan_admitted = report.post_ready_plan_admitted ||
            controller.last_target_plan().ads_plan_admitted;
        now += 0.020;
    }

    report.trigger_executed =
        kSlowTrigger > controller_native::kPhysicalAdsPressThreshold &&
        kSlowTrigger < kScopeReadyTrigger;
    report.pre_ready_manual_preserved =
        !report.pre_ready_plan_admitted &&
        report.maximum_pre_ready_output_deviation <=
            kMaximumPreReadyDeviation;
    report.post_ready_assist_available =
        report.post_ready_plan_admitted &&
        report.maximum_post_ready_output_deviation >=
            kMinimumPostReadyDeviation;
    report.overall_pass =
        report.trigger_executed &&
        report.pre_ready_manual_preserved &&
        report.post_ready_assist_available;
    return report;
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"slow_trigger\": " << report.slow_trigger << ",\n"
           << "  \"scope_ready_trigger\": "
           << report.scope_ready_trigger << ",\n"
           << "  \"maximum_pre_ready_output_deviation\": "
           << report.maximum_pre_ready_output_deviation << ",\n"
           << "  \"maximum_post_ready_output_deviation\": "
           << report.maximum_post_ready_output_deviation << ",\n"
           << "  \"pre_ready_plan_admitted\": "
           << report.pre_ready_plan_admitted << ",\n"
           << "  \"post_ready_plan_admitted\": "
           << report.post_ready_plan_admitted << ",\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"pre_ready_manual_preserved\": "
           << report.pre_ready_manual_preserved << ",\n"
           << "  \"post_ready_assist_available\": "
           << report.post_ready_assist_available << ",\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_scope_ready_authority_incident_regression(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv,
                "ads_scope_ready_authority_incident.json");
        const Report report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " pre_ready_deviation="
                  << report.maximum_pre_ready_output_deviation
                  << " post_ready_deviation="
                  << report.maximum_post_ready_output_deviation
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_ads_scope_ready_authority_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_ads_scope_ready_authority", "ads_scope_ready_authority_incident.json", run_ads_scope_ready_authority_incident_regression);
}
