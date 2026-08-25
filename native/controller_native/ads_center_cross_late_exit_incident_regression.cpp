#include "incident_fixture_support.h"
#include "target_coordinator.h"
#include "test_support/native_test_registry.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

constexpr const char* kIncidentId =
    "ads-center-cross-late-exit-20260817";

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent{};
    intent.ads = true;
    return intent;
}

pipeline_contract::VisionObservationBatch selected_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float error_x,
    float error_y) {
    constexpr float kCenterX = 240.0f;
    constexpr float kCenterY = 208.0f;
    constexpr std::uint64_t kSourceId = 81701;
    constexpr std::uint64_t kSelectorGeneration = 817;

    pipeline_contract::VisionObservationBatch batch{};
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = kCenterX * 2.0f;
    batch.frame_height_px = kCenterY * 2.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = kSelectorGeneration;
    batch.preferred_source_id = kSourceId;
    batch.count = 1;

    auto& candidate = batch.candidates[0];
    candidate.source_id = kSourceId;
    candidate.aim_px = {kCenterX + error_x, kCenterY + error_y};
    candidate.has_aim_point = true;
    candidate.aim_region_px = {
        candidate.aim_px.x - 24.0f,
        candidate.aim_px.y - 40.0f,
        48.0f,
        80.0f,
    };
    candidate.aim_region_source =
        pipeline_contract::AimRegionSource::VisionGeometry;
    candidate.has_aim_region = true;
    candidate.body_box_px = candidate.aim_region_px;
    candidate.has_body_box = true;
    candidate.box_size_px = {48.0f, 112.0f};
    candidate.confidence = 0.95f;
    candidate.reliability = 0.90f;
    candidate.normalized_size = 0.25f;
    candidate.body_cue = true;
    return batch;
}

controller_native::TargetCoordinatorConfig incident_config() {
    controller_native::TargetCoordinatorConfig config{};
    // Settling and watchdog completion are deliberately unavailable. The
    // fixture isolates the already-detected meaningful radial center cross.
    config.settle_radius_px = 8.0f;
    config.settle_frames = 1000;
    config.ads_nominal_acquisition_ms = 500.0f;
    config.ads_extension_budget_ms = 1000.0f;
    config.visual_authority_enabled = false;
    return config;
}

struct CaseResult {
    const char* name = "";
    pipeline_contract::Vec2f first_error{};
    pipeline_contract::Vec2f second_error{};
    std::uint64_t first_acquisition_id = 0;
    std::uint64_t second_acquisition_id = 0;
    bool first_ads_active = false;
    bool radial_cross_detected = false;
    bool second_ads_active = false;
    bool entered_bodylock = false;
    bool bodylock_authoritative = false;
    bool terminal_center_cross = false;
};

CaseResult run_case(
    const char* name,
    pipeline_contract::Vec2f first_error,
    pipeline_contract::Vec2f second_error,
    std::uint64_t epoch) {
    const double start = 100.0 + static_cast<double>(epoch);
    controller_native::TargetCoordinator coordinator(incident_config());
    coordinator.begin_ads_epoch(epoch, start);
    const auto intent = ads_intent();
    const auto first = coordinator.update(
        selected_frame(
            epoch * 10 + 1,
            start,
            first_error.x,
            first_error.y),
        intent,
        start);
    const auto second = coordinator.update(
        selected_frame(
            epoch * 10 + 2,
            start + 0.006,
            second_error.x,
            second_error.y),
        intent,
        start + 0.006);

    CaseResult result{};
    result.name = name;
    result.first_error = first.error_px;
    result.second_error = second.error_px;
    result.first_acquisition_id = first.target_acquisition_id;
    result.second_acquisition_id = second.target_acquisition_id;
    result.first_ads_active =
        first.mode == pipeline_contract::ControlMode::AdsAcquire &&
        first.ads_acquisition_active;
    result.radial_cross_detected =
        second.ads_decision_reason ==
            pipeline_contract::AdsDecisionReason::CenterCross;
    result.second_ads_active =
        second.mode == pipeline_contract::ControlMode::AdsAcquire &&
        second.ads_acquisition_active;
    result.entered_bodylock =
        second.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        !second.ads_acquisition_active;
    result.bodylock_authoritative = second.aim_authority > 0.0f;
    result.terminal_center_cross =
        second.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::CenterCross;
    return result;
}

struct IncidentReport {
    CaseResult horizontal_cross;
    CaseResult vertical_cross;
    CaseResult same_direction;
    CaseResult orthogonal_motion;
    CaseResult deadzone_flip;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool both_axes_exit_ads = false;
    bool both_axes_enter_bodylock = false;
    bool both_axes_retain_authority = false;
    bool both_axes_publish_terminal_reason = false;
    bool overall_pass = false;
};

IncidentReport evaluate() {
    IncidentReport report{
        run_case("horizontal_cross", {8.0f, 0.0f}, {-6.0f, 0.0f}, 1),
        run_case("vertical_cross", {0.0f, 8.0f}, {0.0f, -6.0f}, 2),
        run_case("same_direction", {12.0f, 0.0f}, {7.0f, 0.0f}, 3),
        run_case("orthogonal_motion", {8.0f, 8.0f}, {-5.0f, 8.0f}, 4),
        run_case("deadzone_flip", {6.0f, 0.0f}, {-1.0f, 0.0f}, 5),
    };

    report.trigger_executed =
        report.horizontal_cross.first_ads_active &&
        report.vertical_cross.first_ads_active &&
        report.horizontal_cross.radial_cross_detected &&
        report.vertical_cross.radial_cross_detected &&
        report.horizontal_cross.first_acquisition_id != 0 &&
        report.vertical_cross.first_acquisition_id != 0 &&
        report.horizontal_cross.second_acquisition_id ==
            report.horizontal_cross.first_acquisition_id &&
        report.vertical_cross.second_acquisition_id ==
            report.vertical_cross.first_acquisition_id;
    report.counterfactuals_valid =
        report.same_direction.first_ads_active &&
        report.same_direction.second_ads_active &&
        !report.same_direction.radial_cross_detected &&
        report.orthogonal_motion.first_ads_active &&
        report.orthogonal_motion.second_ads_active &&
        !report.orthogonal_motion.radial_cross_detected &&
        report.deadzone_flip.first_ads_active &&
        report.deadzone_flip.second_ads_active &&
        !report.deadzone_flip.radial_cross_detected;
    report.both_axes_exit_ads =
        !report.horizontal_cross.second_ads_active &&
        !report.vertical_cross.second_ads_active;
    report.both_axes_enter_bodylock =
        report.horizontal_cross.entered_bodylock &&
        report.vertical_cross.entered_bodylock;
    report.both_axes_retain_authority =
        report.horizontal_cross.bodylock_authoritative &&
        report.vertical_cross.bodylock_authoritative;
    report.both_axes_publish_terminal_reason =
        report.horizontal_cross.terminal_center_cross &&
        report.vertical_cross.terminal_center_cross;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        report.both_axes_exit_ads &&
        report.both_axes_enter_bodylock &&
        report.both_axes_retain_authority &&
        report.both_axes_publish_terminal_reason;
    return report;
}

void write_vec(
    std::ostream& stream,
    pipeline_contract::Vec2f value) {
    stream << "{\"x\":" << value.x << ",\"y\":" << value.y << "}";
}

void write_case(std::ostream& stream, const CaseResult& value) {
    stream << std::boolalpha
           << "{\"first_error_px\":";
    write_vec(stream, value.first_error);
    stream << ",\"second_error_px\":";
    write_vec(stream, value.second_error);
    stream << ",\"first_acquisition_id\":"
           << value.first_acquisition_id
           << ",\"second_acquisition_id\":"
           << value.second_acquisition_id
           << ",\"first_ads_active\":" << value.first_ads_active
           << ",\"radial_cross_detected\":"
           << value.radial_cross_detected
           << ",\"second_ads_active\":" << value.second_ads_active
           << ",\"entered_bodylock\":" << value.entered_bodylock
           << ",\"bodylock_authoritative\":"
           << value.bodylock_authoritative
           << ",\"terminal_center_cross\":"
           << value.terminal_center_cross << "}";
}

void write_report(
    const std::filesystem::path& output,
    const IncidentReport& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"ADS remains the high-authority owner after a meaningful radial center crossing, allowing repeated corrective reversals\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"two consecutive fresh selector-owned person frames 6 ms apart\",\n"
           << "    \"target_generation\": 817,\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"zero\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"disabled and not firing\",\n"
           << "    \"controller_mode\": \"TargetCoordinator ADS lifecycle\",\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"cases\": {\n"
           << "    \"horizontal_cross\": ";
    write_case(stream, report.horizontal_cross);
    stream << ",\n    \"vertical_cross\": ";
    write_case(stream, report.vertical_cross);
    stream << ",\n    \"same_direction_counterfactual\": ";
    write_case(stream, report.same_direction);
    stream << ",\n    \"orthogonal_counterfactual\": ";
    write_case(stream, report.orthogonal_motion);
    stream << ",\n    \"deadzone_counterfactual\": ";
    write_case(stream, report.deadzone_flip);
    stream << "\n  },\n"
           << "  \"trigger_executed\": "
           << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"both_axes_exit_ads_on_meaningful_radial_cross\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.both_axes_exit_ads << ",\"pass\":" << report.both_axes_exit_ads << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"both_axes_handoff_to_bodylock\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.both_axes_enter_bodylock << ",\"pass\":" << report.both_axes_enter_bodylock << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"bodylock_retains_nonzero_authority\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.both_axes_retain_authority << ",\"pass\":" << report.both_axes_retain_authority << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"terminal_reason_is_center_cross\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.both_axes_publish_terminal_reason << ",\"pass\":" << report.both_axes_publish_terminal_reason << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_center_cross_late_exit_incident_regression(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc,
                argv,
                "ads_center_cross_late_exit_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " trigger=" << report.trigger_executed
                  << " counterfactuals=" << report.counterfactuals_valid
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 3;
    }
}

void register_ads_center_cross_late_exit_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_ads_center_cross_late_exit", "ads_center_cross_late_exit_incident.json", run_ads_center_cross_late_exit_incident_regression);
}
