#include "assist_control_state_machine.h"
#include "target_coordinator.h"
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

constexpr const char* kIncidentId =
    "ads-snap-deadline-release-20260821";
constexpr float kWaitDeadlineMs = 220.0f;
constexpr std::uint64_t kSelectorGeneration = 901;

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent;
    intent.ads = true;
    return intent;
}

pipeline_contract::VisionObservationBatch selected_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float aim_x = 320.0f,
    float aim_y = 140.0f) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = kSelectorGeneration;
    batch.preferred_source_id = 9011;
    batch.count = 1;

    auto& candidate = batch.candidates[0];
    candidate.source_id = batch.preferred_source_id;
    candidate.aim_px = {aim_x, aim_y};
    candidate.has_aim_point = true;
    candidate.aim_region_px = {
        aim_x - 24.0f, aim_y - 40.0f, 48.0f, 80.0f};
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

pipeline_contract::VisionObservationBatch no_selection_frame(
    std::uint64_t frame_id,
    double capture_seconds) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = kSelectorGeneration;
    return batch;
}

controller_native::TargetCoordinatorConfig deadline_config() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 1000.0f;
    config.settle_radius_px = 1.0f;
    config.settle_frames = 100;
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_max_acquisition_ms = kWaitDeadlineMs;
    return config;
}

struct IncidentReport {
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool owner_active_before_execution_deadline = false;
    bool owner_active_after_execution_deadline = false;
    bool execution_deadline_handoff = false;
    bool waiting_before_deadline = false;
    bool wait_deadline_released = false;
    int late_after_expiry_ads_admissions = 0;
    int in_window_ads_admissions = 0;
    int settled_before_deadline_handoffs = 0;
    pipeline_contract::Vec2f recovery_manual_input{-0.72f, 0.68f};
    pipeline_contract::Vec2f recovery_output_before{};
    pipeline_contract::Vec2f recovery_output_after{};
    float manual_retention_before_deadline = 0.0f;
    float manual_retention_after_deadline = 0.0f;
    bool recovery_trigger_executed = false;
    bool manual_suppressed_before_deadline = false;
    bool manual_restored_after_deadline = false;
    float execution_elapsed_before_ms = 0.0f;
    float execution_elapsed_after_ms = 0.0f;
    float epoch_elapsed_before_ms = 0.0f;
    float epoch_elapsed_after_ms = 0.0f;
    bool overall_pass = false;
};

IncidentReport evaluate_incident() {
    IncidentReport report;
    const auto intent = ads_intent();

    // Primary incident: a selected target is admitted immediately, never
    // settles, and remains on the same side of center. At 219 ms the snap is
    // still valid; at 221 ms its execution owner must release to BodyLock.
    controller_native::TargetCoordinator active(deadline_config());
    active.begin_ads_epoch(1, 10.0);
    const auto admitted = active.update(
        selected_frame(100, 10.0), intent, 10.0);
    const auto active_before = active.update(
        selected_frame(101, 10.219), intent, 10.219);
    const auto active_after = active.update(
        selected_frame(102, 10.221), intent, 10.221);
    report.owner_active_before_execution_deadline =
        active_before.mode == pipeline_contract::ControlMode::AdsAcquire &&
        active_before.ads_acquisition_active;
    report.owner_active_after_execution_deadline =
        active_after.mode == pipeline_contract::ControlMode::AdsAcquire &&
        active_after.ads_acquisition_active;
    report.execution_deadline_handoff =
        active_after.mode ==
            pipeline_contract::ControlMode::BodyLockFollow &&
        !active_after.ads_acquisition_active &&
        active_after.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::AcquisitionCeiling;
    report.execution_elapsed_before_ms =
        active_before.acquisition_elapsed_ms;
    report.execution_elapsed_after_ms = active_after.acquisition_elapsed_ms;

    // User-visible symptom oracle: while ADS owns the target, an otherwise
    // idle target proposal may intentionally brake both manual axes. Once the
    // bounded ADS job expires, the same strong two-axis physical input must be
    // observable again on the very first BodyLock sample. No D-correction or
    // explicit-exit tag is supplied, matching the captured incident.
    controller_native::AssistControlStateMachine authority;
    const auto authority_input = [&report](
        const pipeline_contract::TargetPlan& plan,
        double now_seconds) {
        controller_native::AssistControlStateMachineInput input;
        input.aiming = true;
        input.target_authoritative = plan.target_id != 0;
        input.fresh_observation = true;
        input.target_id = plan.target_id;
        input.selector_target_generation = plan.selector_target_generation;
        input.now_seconds = now_seconds;
        input.target_error_px = plan.error_px;
        input.mode = plan.mode;
        input.visual_authority = 1.0f;
        input.manual_stick = report.recovery_manual_input;
        input.centered_manual_stick = report.recovery_manual_input;
        input.centered_manual_available = true;
        input.filtered_manual_stick = report.recovery_manual_input;
        input.ai_stick = {};
        return input;
    };
    const auto before_authority = authority.update(
        authority_input(active_before, 10.219));
    const auto after_authority = authority.update(
        authority_input(active_after, 10.221));
    report.recovery_output_before = before_authority.stick;
    report.recovery_output_after = after_authority.stick;
    const float manual_magnitude = std::hypot(
        report.recovery_manual_input.x, report.recovery_manual_input.y);
    report.manual_retention_before_deadline = std::hypot(
        report.recovery_output_before.x,
        report.recovery_output_before.y) / manual_magnitude;
    report.manual_retention_after_deadline = std::hypot(
        report.recovery_output_after.x,
        report.recovery_output_after.y) / manual_magnitude;
    report.recovery_trigger_executed =
        active_before.mode == pipeline_contract::ControlMode::AdsAcquire &&
        active_after.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        manual_magnitude > 0.9f;
    report.manual_suppressed_before_deadline =
        report.manual_retention_before_deadline <= 0.05f;
    report.manual_restored_after_deadline =
        report.manual_retention_after_deadline >= 0.95f &&
        before_authority.manual_passthrough_x == false &&
        before_authority.manual_passthrough_y == false &&
        after_authority.manual_passthrough_x &&
        after_authority.manual_passthrough_y &&
        report.recovery_output_after.x * report.recovery_manual_input.x > 0.0f &&
        report.recovery_output_after.y * report.recovery_manual_input.y > 0.0f;

    // The same 220 ms value is an independent pre-admission deadline. A late
    // target after expiry may enter ordinary BodyLock, but it must not mint the
    // one-LT ADS snap that has already been consumed.
    controller_native::TargetCoordinator expired_wait(deadline_config());
    expired_wait.begin_ads_epoch(2, 20.0);
    (void)expired_wait.update(
        no_selection_frame(200, 20.0), intent, 20.0);
    const auto wait_before = expired_wait.update(
        no_selection_frame(201, 20.219), intent, 20.219);
    const auto wait_after = expired_wait.update(
        no_selection_frame(202, 20.221), intent, 20.221);
    const auto late_after_expiry = expired_wait.update(
        selected_frame(203, 20.230), intent, 20.230);
    report.waiting_before_deadline =
        wait_before.mode == pipeline_contract::ControlMode::Manual &&
        wait_before.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
    report.wait_deadline_released =
        wait_after.mode == pipeline_contract::ControlMode::Manual &&
        wait_after.ads_acquisition_state ==
            pipeline_contract::AdsAcquisitionState::Completed &&
        wait_after.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::NoTarget &&
        !wait_after.ads_acquisition_active;
    report.late_after_expiry_ads_admissions =
        late_after_expiry.ads_plan_admitted ||
            late_after_expiry.mode ==
                pipeline_contract::ControlMode::AdsAcquire ||
            late_after_expiry.ads_acquisition_active
        ? 1
        : 0;
    report.epoch_elapsed_before_ms = wait_before.ads_epoch_elapsed_ms;
    report.epoch_elapsed_after_ms = wait_after.ads_epoch_elapsed_ms;

    // Counterfactual: a target arriving just inside the wait deadline still
    // starts a full, independently timed ADS positioning job.
    controller_native::TargetCoordinator in_window(deadline_config());
    in_window.begin_ads_epoch(3, 30.0);
    (void)in_window.update(
        no_selection_frame(300, 30.0), intent, 30.0);
    (void)in_window.update(
        no_selection_frame(301, 30.100), intent, 30.100);
    const auto admitted_in_window = in_window.update(
        selected_frame(302, 30.219), intent, 30.219);
    report.in_window_ads_admissions =
        admitted_in_window.ads_plan_admitted &&
            admitted_in_window.mode ==
                pipeline_contract::ControlMode::AdsAcquire &&
            admitted_in_window.ads_acquisition_active &&
            admitted_in_window.acquisition_elapsed_ms < 1.0f
        ? 1
        : 0;

    // Counterfactual: ordinary successful completion remains stronger than
    // either deadline and hands off with the Settled terminal reason.
    auto settle_config = deadline_config();
    settle_config.settle_radius_px = 8.0f;
    settle_config.settle_frames = 2;
    controller_native::TargetCoordinator settled(settle_config);
    settled.begin_ads_epoch(4, 40.0);
    (void)settled.update(
        selected_frame(400, 40.0, 240.0f, 208.0f), intent, 40.0);
    const auto settled_end = settled.update(
        selected_frame(401, 40.006, 240.0f, 208.0f), intent, 40.006);
    report.settled_before_deadline_handoffs =
        settled_end.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            !settled_end.ads_acquisition_active &&
            settled_end.acquisition_terminal_reason ==
                pipeline_contract::AdsDecisionReason::Settled
        ? 1
        : 0;

    const bool admitted_two_axis_target =
        admitted.ads_plan_admitted &&
        admitted.mode == pipeline_contract::ControlMode::AdsAcquire &&
        admitted.ads_acquisition_active &&
        admitted.ads_candidate_count == 1 &&
        std::fabs(admitted.ads_raw_error_px.x) >= 60.0f &&
        std::fabs(admitted.ads_raw_error_px.y) >= 60.0f;
    report.trigger_executed =
        admitted_two_axis_target && report.waiting_before_deadline &&
        report.recovery_trigger_executed &&
        report.execution_elapsed_before_ms >= 218.0f &&
        report.execution_elapsed_after_ms >= 220.0f &&
        report.epoch_elapsed_before_ms >= 218.0f &&
        report.epoch_elapsed_after_ms >= 220.0f;
    report.counterfactuals_valid =
        report.owner_active_before_execution_deadline &&
        report.manual_suppressed_before_deadline &&
        report.in_window_ads_admissions == 1 &&
        report.settled_before_deadline_handoffs == 1;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        !report.owner_active_after_execution_deadline &&
        report.execution_deadline_handoff &&
        report.wait_deadline_released &&
        report.late_after_expiry_ads_admissions == 0 &&
        report.manual_restored_after_deadline;
    return report;
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return argv[2];
    }
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

void write_report(
    const std::filesystem::path& output_path,
    const IncidentReport& report) {
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open incident report");

    output << std::boolalpha << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema_version\": 2,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"ADS Snap remains the active owner after its 220 ms deadline, suppressing strong two-axis player input, and an expired no-target wait may still admit a late snap\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"fresh selector frames at 219 ms and 221 ms boundaries\",\n"
           << "    \"target_generation\": " << kSelectorGeneration << ",\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"strong two-axis (-0.72,+0.68) held across the 219/221 ms boundary without D-correction or explicit-exit tags\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"disabled and not firing\",\n"
           << "    \"controller_mode\": \"production TargetCoordinator Manual/AdsAcquire/BodyLockFollow\",\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"metrics\": {\n"
           << "    \"execution_elapsed_before_ms\": "
           << report.execution_elapsed_before_ms << ",\n"
           << "    \"execution_elapsed_after_ms\": "
           << report.execution_elapsed_after_ms << ",\n"
           << "    \"epoch_elapsed_before_ms\": "
           << report.epoch_elapsed_before_ms << ",\n"
           << "    \"epoch_elapsed_after_ms\": "
           << report.epoch_elapsed_after_ms << ",\n"
           << "    \"owner_active_before_execution_deadline\": "
           << report.owner_active_before_execution_deadline << ",\n"
           << "    \"owner_active_after_execution_deadline\": "
           << report.owner_active_after_execution_deadline << ",\n"
           << "    \"execution_deadline_handoff\": "
           << report.execution_deadline_handoff << ",\n"
           << "    \"waiting_before_deadline\": "
           << report.waiting_before_deadline << ",\n"
           << "    \"wait_deadline_released\": "
           << report.wait_deadline_released << ",\n"
           << "    \"late_after_expiry_ads_admissions\": "
           << report.late_after_expiry_ads_admissions << ",\n"
           << "    \"in_window_ads_admissions\": "
           << report.in_window_ads_admissions << ",\n"
           << "    \"settled_before_deadline_handoffs\": "
           << report.settled_before_deadline_handoffs << ",\n"
           << "    \"manual_input\": {\"x\":"
           << report.recovery_manual_input.x << ",\"y\":"
           << report.recovery_manual_input.y << "},\n"
           << "    \"output_before_deadline\": {\"x\":"
           << report.recovery_output_before.x << ",\"y\":"
           << report.recovery_output_before.y << "},\n"
           << "    \"output_after_deadline\": {\"x\":"
           << report.recovery_output_after.x << ",\"y\":"
           << report.recovery_output_after.y << "},\n"
           << "    \"manual_retention_before_deadline\": "
           << report.manual_retention_before_deadline << ",\n"
           << "    \"manual_retention_after_deadline\": "
           << report.manual_retention_after_deadline << ",\n"
           << "    \"manual_restored_after_deadline\": "
           << report.manual_restored_after_deadline << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"owner_active_before_execution_deadline\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << (report.owner_active_before_execution_deadline ? 1 : 0) << ",\"pass\":" << report.owner_active_before_execution_deadline << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"owner_active_after_execution_deadline\",\"operator\":\"==\",\"threshold\":0,\"observed\":" << (report.owner_active_after_execution_deadline ? 1 : 0) << ",\"pass\":" << !report.owner_active_after_execution_deadline << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"execution_deadline_handoff\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << (report.execution_deadline_handoff ? 1 : 0) << ",\"pass\":" << report.execution_deadline_handoff << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"wait_deadline_released\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << (report.wait_deadline_released ? 1 : 0) << ",\"pass\":" << report.wait_deadline_released << "},\n"
           << "    {\"id\":\"O5\",\"metric\":\"late_after_expiry_ads_admissions\",\"operator\":\"==\",\"threshold\":0,\"observed\":" << report.late_after_expiry_ads_admissions << ",\"pass\":" << (report.late_after_expiry_ads_admissions == 0) << "},\n"
           << "    {\"id\":\"O6\",\"metric\":\"in_window_ads_admissions\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << report.in_window_ads_admissions << ",\"pass\":" << (report.in_window_ads_admissions == 1) << "},\n"
           << "    {\"id\":\"O7\",\"metric\":\"settled_before_deadline_handoffs\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << report.settled_before_deadline_handoffs << ",\"pass\":" << (report.settled_before_deadline_handoffs == 1) << "},\n"
           << "    {\"id\":\"O8\",\"metric\":\"manual_retention_before_deadline\",\"operator\":\"<=\",\"threshold\":0.05,\"observed\":" << report.manual_retention_before_deadline << ",\"pass\":" << report.manual_suppressed_before_deadline << "},\n"
           << "    {\"id\":\"O9\",\"metric\":\"manual_retention_after_deadline\",\"operator\":\">=\",\"threshold\":0.95,\"observed\":" << report.manual_retention_after_deadline << ",\"pass\":" << report.manual_restored_after_deadline << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_snap_deadline_release_incident(int argc, char** argv) {
    try {
        const auto report = evaluate_incident();
        write_report(output_path_from_args(argc, argv), report);
        std::cout << "[AdsSnapDeadlineReleaseIncident] active_after="
                  << report.owner_active_after_execution_deadline
                  << " execution_handoff="
                  << report.execution_deadline_handoff
                  << " wait_released=" << report.wait_deadline_released
                  << " late_admissions="
                  << report.late_after_expiry_ads_admissions
                  << " in_window_admissions="
                  << report.in_window_ads_admissions
                  << " manual_retention="
                  << report.manual_retention_before_deadline << "->"
                  << report.manual_retention_after_deadline
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[AdsSnapDeadlineReleaseIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_ads_snap_deadline_release_incident(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseAds",
        "incident_ads_deadline_restores_two_axis_manual_input",
        "ads_snap_deadline_release_incident.json",
        run_ads_snap_deadline_release_incident);
}
