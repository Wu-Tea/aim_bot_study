#include "assist_control_state_machine.h"
#include "target_coordinator.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

constexpr const char* kIncidentId = "ads-snap-core-contract-20260817";
constexpr float kTolerance = 1.0e-4f;

void require_finite(float value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(name);
    }
}

pipeline_contract::VisionObservationBatch selected_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float aim_x,
    float aim_y = 208.0f,
    std::uint64_t generation = 817) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = generation;
    batch.preferred_source_id = 81;
    batch.count = 1;
    auto& candidate = batch.candidates[0];
    candidate.source_id = 81;
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

pipeline_contract::VisionObservationBatch fresh_miss(
    std::uint64_t frame_id,
    double capture_seconds,
    std::uint64_t generation = 817) {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = generation;
    return batch;
}

pipeline_contract::VisionObservationBatch no_source_tick() {
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    return batch;
}

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent;
    intent.ads = true;
    return intent;
}

controller_native::AssistControlStateMachineInput arbitration_input(
    pipeline_contract::ControlMode mode,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f desired,
    bool target_authoritative) {
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = target_authoritative;
    input.fresh_observation = target_authoritative;
    input.target_id = target_authoritative ? 1 : 0;
    input.selector_target_generation = target_authoritative ? 817 : 0;
    input.now_seconds = target_authoritative ? 10.005 : 10.0;
    input.target_error_px = {48.0f, -24.0f};
    input.mode = mode;
    input.visual_authority = 1.0f;
    input.manual_stick = manual;
    input.centered_manual_stick = manual;
    input.centered_manual_available = true;
    input.filtered_manual_stick = manual;
    input.ai_stick = desired;
    return input;
}

struct ArbitrationReport {
    pipeline_contract::Vec2f desired{0.40f, -0.25f};
    pipeline_contract::Vec2f carry_manual{-0.30f, 0.20f};
    pipeline_contract::Vec2f carry_final{};
    pipeline_contract::Vec2f brake_manual{0.30f, 0.0f};
    pipeline_contract::Vec2f brake_desired{0.0f, 0.25f};
    pipeline_contract::Vec2f brake_final{};
    float maximum_ads_target_error = 0.0f;
    float inactive_axis_output = 0.0f;
    bool bodylock_intentional_d_retained = false;
    bool explicit_exit_passthrough = false;
    bool trigger_executed = false;
};

ArbitrationReport evaluate_arbitration() {
    ArbitrationReport report;

    controller_native::AssistControlStateMachine carry;
    (void)carry.update(arbitration_input(
        pipeline_contract::ControlMode::Manual,
        report.carry_manual,
        {},
        false));
    report.carry_final = carry.update(arbitration_input(
        pipeline_contract::ControlMode::AdsAcquire,
        report.carry_manual,
        report.desired,
        true)).stick;
    report.maximum_ads_target_error = std::max(
        std::fabs(report.carry_final.x - report.desired.x),
        std::fabs(report.carry_final.y - report.desired.y));

    controller_native::AssistControlStateMachine brake;
    (void)brake.update(arbitration_input(
        pipeline_contract::ControlMode::Manual,
        report.brake_manual,
        {},
        false));
    report.brake_final = brake.update(arbitration_input(
        pipeline_contract::ControlMode::AdsAcquire,
        report.brake_manual,
        report.brake_desired,
        true)).stick;
    report.inactive_axis_output = std::fabs(report.brake_final.x);

    controller_native::AssistControlStateMachine bodylock;
    (void)bodylock.update(arbitration_input(
        pipeline_contract::ControlMode::Manual,
        report.carry_manual,
        {},
        false));
    auto bodylock_input = arbitration_input(
        pipeline_contract::ControlMode::BodyLockFollow,
        report.carry_manual,
        report.desired,
        true);
    bodylock_input.manual_correction_x = true;
    const auto bodylock_output = bodylock.update(bodylock_input);
    report.bodylock_intentional_d_retained =
        bodylock_output.stick.x * report.carry_manual.x > 0.0f;

    controller_native::AssistControlStateMachine handover;
    (void)handover.update(arbitration_input(
        pipeline_contract::ControlMode::AdsAcquire,
        {},
        report.desired,
        true));
    auto exit_input = arbitration_input(
        pipeline_contract::ControlMode::AdsAcquire,
        {-0.55f, 0.0f},
        report.desired,
        true);
    exit_input.manual_exit_requested = true;
    exit_input.now_seconds += 0.005;
    const auto exit_output = handover.update(exit_input);
    report.explicit_exit_passthrough =
        exit_output.handover_requested &&
        exit_output.phase ==
            controller_native::AssistControlPhase::HandoverSeek &&
        std::fabs(exit_output.stick.x + 0.55f) <= kTolerance;

    require_finite(report.maximum_ads_target_error, "non-finite ADS error");
    require_finite(report.inactive_axis_output, "non-finite brake output");
    report.trigger_executed =
        report.carry_manual.x * report.desired.x < 0.0f &&
        report.carry_manual.y * report.desired.y < 0.0f &&
        report.bodylock_intentional_d_retained &&
        report.explicit_exit_passthrough;
    return report;
}

struct LifecycleReport {
    bool trigger_executed = false;
    bool settled_enters_bodylock = false;
    bool timeout_aborts_manual = false;
    int ceiling_bodylock_transitions = 0;
    int center_cross_terminal_handoffs = 0;
    int same_acquisition_resume_count = 0;
    std::uint64_t before_gap_acquisition_id = 0;
    std::uint64_t after_gap_acquisition_id = 0;
};

LifecycleReport evaluate_lifecycle() {
    LifecycleReport report;
    const auto intent = ads_intent();

    controller_native::TargetCoordinatorConfig ceiling_config;
    ceiling_config.settle_radius_px = 1.0f;
    ceiling_config.settle_frames = 100;
    ceiling_config.ads_nominal_acquisition_ms = 10.0f;
    ceiling_config.ads_max_acquisition_ms = 20.0f;
    controller_native::TargetCoordinator ceiling(ceiling_config);
    ceiling.begin_ads_epoch(1, 20.0);
    const auto ceiling_start = ceiling.update(
        selected_frame(1, 20.0, 280.0f), intent, 20.0);
    const auto ceiling_end = ceiling.update(
        selected_frame(2, 20.025, 280.0f), intent, 20.025);
    report.ceiling_bodylock_transitions =
        ceiling_end.mode == pipeline_contract::ControlMode::BodyLockFollow
        ? 1 : 0;

    controller_native::TargetCoordinatorConfig cross_config;
    cross_config.settle_radius_px = 1.0f;
    cross_config.settle_frames = 100;
    cross_config.ads_nominal_acquisition_ms = 5.0f;
    cross_config.ads_max_acquisition_ms = 100.0f;
    controller_native::TargetCoordinator cross(cross_config);
    cross.begin_ads_epoch(2, 30.0);
    const auto cross_start = cross.update(
        selected_frame(10, 30.0, 280.0f), intent, 30.0);
    const auto cross_end = cross.update(
        selected_frame(11, 30.010, 200.0f), intent, 30.010);
    // The original incident treated every non-settled crossing as unsafe.
    // Later matched live evidence (2026-08-17) showed the strict radial
    // crossing predicate repeatedly retaining full ADS authority after the
    // reticle had already passed center. CenterCross is now a distinct valid
    // terminal reason. The later 2026-08-21 product clarification also makes
    // the acquisition deadline terminal rather than diagnostic-only.
    report.center_cross_terminal_handoffs =
        cross_end.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        !cross_end.ads_acquisition_active &&
        cross_end.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::CenterCross
        ? 1 : 0;

    controller_native::TargetCoordinatorConfig gap_config;
    gap_config.settle_frames = 100;
    gap_config.ads_max_acquisition_ms = 500.0f;
    gap_config.max_observation_age_ms = 50.0f;
    controller_native::TargetCoordinator gap(gap_config);
    gap.begin_ads_epoch(3, 40.0);
    const auto before_gap = gap.update(
        selected_frame(20, 40.0, 280.0f), intent, 40.0);
    const auto gap_sample = gap.update(
        fresh_miss(21, 40.006), intent, 40.006);
    const auto waiting_sample = gap.update(
        no_source_tick(), intent, 40.020);
    const auto after_gap = gap.update(
        selected_frame(22, 40.030, 275.0f), intent, 40.030);
    report.before_gap_acquisition_id = before_gap.target_acquisition_id;
    report.after_gap_acquisition_id = after_gap.target_acquisition_id;
    report.same_acquisition_resume_count =
        gap_sample.mode == pipeline_contract::ControlMode::Manual &&
        gap_sample.target_id == 0 && gap_sample.aim_authority == 0.0f &&
        gap_sample.ads_acquisition_active &&
        waiting_sample.mode == pipeline_contract::ControlMode::Manual &&
        waiting_sample.target_id == 0 &&
        waiting_sample.ads_acquisition_active &&
        after_gap.mode == pipeline_contract::ControlMode::AdsAcquire &&
        after_gap.ads_acquisition_active &&
        after_gap.target_acquisition_id == before_gap.target_acquisition_id
        ? 1 : 0;

    controller_native::TargetCoordinator timeout(gap_config);
    timeout.begin_ads_epoch(4, 50.0);
    (void)timeout.update(selected_frame(30, 50.0, 280.0f), intent, 50.0);
    (void)timeout.update(fresh_miss(31, 50.006), intent, 50.006);
    const auto timed_out = timeout.update(
        no_source_tick(), intent, 50.060);
    report.timeout_aborts_manual =
        timed_out.mode == pipeline_contract::ControlMode::Manual &&
        timed_out.target_id == 0 && timed_out.aim_authority == 0.0f &&
        !timed_out.ads_acquisition_active &&
        timed_out.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::TargetLost;

    controller_native::TargetCoordinatorConfig settle_config;
    settle_config.settle_radius_px = 8.0f;
    settle_config.settle_frames = 2;
    settle_config.ads_max_acquisition_ms = 500.0f;
    controller_native::TargetCoordinator settle(settle_config);
    settle.begin_ads_epoch(5, 60.0);
    (void)settle.update(selected_frame(40, 60.0, 240.0f), intent, 60.0);
    const auto settled = settle.update(
        selected_frame(41, 60.005, 240.0f), intent, 60.005);
    report.settled_enters_bodylock =
        settled.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        !settled.ads_acquisition_active &&
        settled.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::Settled;

    report.trigger_executed =
        ceiling_start.mode == pipeline_contract::ControlMode::AdsAcquire &&
        cross_start.mode == pipeline_contract::ControlMode::AdsAcquire &&
        before_gap.mode == pipeline_contract::ControlMode::AdsAcquire &&
        before_gap.target_acquisition_id != 0 &&
        report.settled_enters_bodylock &&
        report.timeout_aborts_manual;
    return report;
}

struct IncidentReport {
    ArbitrationReport arbitration;
    LifecycleReport lifecycle;
    bool target_first_pass = false;
    bool inactive_axis_brake_pass = false;
    bool ceiling_pass = false;
    bool center_cross_pass = false;
    bool reacquire_pass = false;
    bool overall_pass = false;
};

IncidentReport evaluate_incident() {
    IncidentReport report;
    report.arbitration = evaluate_arbitration();
    report.lifecycle = evaluate_lifecycle();
    report.target_first_pass =
        report.arbitration.maximum_ads_target_error <= kTolerance;
    report.inactive_axis_brake_pass =
        report.arbitration.inactive_axis_output <= kTolerance;
    report.ceiling_pass =
        report.lifecycle.ceiling_bodylock_transitions == 1;
    report.center_cross_pass =
        report.lifecycle.center_cross_terminal_handoffs == 1;
    report.reacquire_pass =
        report.lifecycle.same_acquisition_resume_count == 1;
    report.overall_pass =
        report.arbitration.trigger_executed &&
        report.lifecycle.trigger_executed &&
        report.target_first_pass &&
        report.inactive_axis_brake_pass &&
        report.ceiling_pass &&
        report.center_cross_pass &&
        report.reacquire_pass;
    return report;
}

void write_vec(std::ostream& stream, pipeline_contract::Vec2f value) {
    stream << "{\"x\":" << value.x << ",\"y\":" << value.y << "}";
}

void write_report(
    const std::filesystem::path& output_path,
    const IncidentReport& report) {
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("failed to open incident report");
    output << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"ADS can stop or reverse against an accepted target, while terminal ownership must remain bounded and explicit\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"fresh selected frames at 5-25 ms cadence; one same-generation fresh miss followed by no-source ticks and direct reacquisition\",\n"
           << "    \"target_generation\": 817,\n"
           << "    \"target_count\": 1,\n"
           << "    \"right_stick_manual\": \"pre-existing diagonal (-0.30,+0.20), orthogonal +0.30, zero lifecycle input, and explicit -0.55 exit counterfactual\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"disabled and not firing\",\n"
           << "    \"controller_mode\": \"production AssistControlStateMachine ADS/BodyLock plus TargetCoordinator ADS lifecycle\",\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"trigger_executed\": "
           << (report.arbitration.trigger_executed &&
               report.lifecycle.trigger_executed) << ",\n"
           << "  \"counterfactuals_valid\": "
           << (report.arbitration.bodylock_intentional_d_retained &&
               report.arbitration.explicit_exit_passthrough &&
               report.lifecycle.settled_enters_bodylock &&
               report.lifecycle.timeout_aborts_manual) << ",\n"
           << "  \"arbitration\": {\n"
           << "    \"desired\": ";
    write_vec(output, report.arbitration.desired);
    output << ",\n    \"carry_manual\": ";
    write_vec(output, report.arbitration.carry_manual);
    output << ",\n    \"carry_final\": ";
    write_vec(output, report.arbitration.carry_final);
    output << ",\n    \"brake_desired\": ";
    write_vec(output, report.arbitration.brake_desired);
    output << ",\n    \"brake_final\": ";
    write_vec(output, report.arbitration.brake_final);
    output << ",\n    \"maximum_ads_target_error\": "
           << report.arbitration.maximum_ads_target_error << ",\n"
           << "    \"inactive_axis_output\": "
           << report.arbitration.inactive_axis_output << ",\n"
           << "    \"bodylock_intentional_d_retained\": "
           << report.arbitration.bodylock_intentional_d_retained << ",\n"
           << "    \"explicit_exit_passthrough\": "
           << report.arbitration.explicit_exit_passthrough << "\n"
           << "  },\n"
           << "  \"lifecycle\": {\n"
           << "    \"ceiling_bodylock_transitions\": "
           << report.lifecycle.ceiling_bodylock_transitions << ",\n"
           << "    \"center_cross_terminal_handoffs\": "
           << report.lifecycle.center_cross_terminal_handoffs << ",\n"
           << "    \"same_acquisition_resume_count\": "
           << report.lifecycle.same_acquisition_resume_count << ",\n"
           << "    \"before_gap_acquisition_id\": "
           << report.lifecycle.before_gap_acquisition_id << ",\n"
           << "    \"after_gap_acquisition_id\": "
           << report.lifecycle.after_gap_acquisition_id << ",\n"
           << "    \"settled_enters_bodylock\": "
           << report.lifecycle.settled_enters_bodylock << ",\n"
           << "    \"timeout_aborts_manual\": "
           << report.lifecycle.timeout_aborts_manual << "\n"
           << "  },\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"maximum_ads_target_error\",\"operator\":\"<=\",\"threshold\":" << kTolerance << ",\"observed\":" << report.arbitration.maximum_ads_target_error << ",\"pass\":" << report.target_first_pass << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"inactive_axis_output\",\"operator\":\"<=\",\"threshold\":" << kTolerance << ",\"observed\":" << report.arbitration.inactive_axis_output << ",\"pass\":" << report.inactive_axis_brake_pass << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"ceiling_bodylock_transitions\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << report.lifecycle.ceiling_bodylock_transitions << ",\"pass\":" << report.ceiling_pass << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"center_cross_terminal_handoffs\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << report.lifecycle.center_cross_terminal_handoffs << ",\"pass\":" << report.center_cross_pass << "},\n"
           << "    {\"id\":\"O5\",\"metric\":\"same_acquisition_resume_count\",\"operator\":\"==\",\"threshold\":1,\"observed\":" << report.lifecycle.same_acquisition_resume_count << ",\"pass\":" << report.reacquire_pass << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--output") return argv[index + 1];
    }
    return "ads_snap_core_contract_incident.json";
}

}  // namespace

int run_ads_snap_core_contract_incident_regression(int argc, char** argv) {
    try {
        const auto report = evaluate_incident();
        write_report(output_path_from_args(argc, argv), report);
        std::cout << "[AdsSnapCoreContractIncident] target_error="
                  << report.arbitration.maximum_ads_target_error
                  << " inactive_axis="
                  << report.arbitration.inactive_axis_output
                  << " ceiling_to_bodylock="
                  << report.lifecycle.ceiling_bodylock_transitions
                  << " cross_terminal_handoff="
                  << report.lifecycle.center_cross_terminal_handoffs
                  << " reacquired="
                  << report.lifecycle.same_acquisition_resume_count
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.arbitration.trigger_executed ||
            !report.lifecycle.trigger_executed) {
            return 3;
        }
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[AdsSnapCoreContractIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_ads_snap_core_contract_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseAds", "incident_ads_snap_core_contract", "ads_snap_core_contract_incident.json", run_ads_snap_core_contract_incident_regression);
}
