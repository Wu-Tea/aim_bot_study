#include "incident_fixture_support.h"
#include "target_coordinator.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

constexpr const char* kIncidentId =
    "ads-single-press-single-snap-20260817";

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent{};
    intent.ads = true;
    return intent;
}

pipeline_contract::VisionObservationBatch selected_frame(
    std::uint64_t frame_id,
    double capture_seconds,
    float aim_x,
    std::uint64_t source_id,
    std::uint64_t selector_generation,
    bool replacement = false) {
    pipeline_contract::VisionObservationBatch batch{};
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = selector_generation;
    batch.selector_target_changed = replacement;
    batch.preferred_source_id = source_id;
    batch.count = 1;
    auto& candidate = batch.candidates[0];
    candidate.source_id = source_id;
    candidate.aim_px = {aim_x, 208.0f};
    candidate.has_aim_point = true;
    candidate.aim_region_px = {
        aim_x - 24.0f, 168.0f, 48.0f, 80.0f};
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

struct ReplacementCase {
    const char* name = "";
    std::uint64_t first_acquisition_id = 0;
    std::uint64_t replacement_acquisition_id = 0;
    bool replacement_observed = false;
    bool acquisition_identity_preserved = false;
    bool replacement_ads_inactive = false;
    bool replacement_bodylock_authoritative = false;
};

ReplacementCase run_replacement_case(bool settle_first_target) {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = settle_first_target ? 1u : 1000u;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(1, 1.0);
    const float first_x = settle_first_target ? 240.0f : 300.0f;
    const auto first = coordinator.update(
        selected_frame(1, 1.0, first_x, 41, 7), intent, 1.0);
    const auto replacement = coordinator.update(
        selected_frame(2, 1.006, 320.0f, 52, 8, true),
        intent,
        1.006);

    ReplacementCase result{};
    result.name = settle_first_target
        ? "replacement_after_snap_consumed"
        : "replacement_during_active_snap";
    result.first_acquisition_id = first.target_acquisition_id;
    result.replacement_acquisition_id = replacement.target_acquisition_id;
    result.replacement_observed = replacement.selector_target_changed &&
        replacement.target_id != 0 && replacement.target_id != first.target_id;
    result.acquisition_identity_preserved =
        first.target_acquisition_id != 0 &&
        replacement.target_acquisition_id == first.target_acquisition_id;
    result.replacement_ads_inactive =
        replacement.mode != pipeline_contract::ControlMode::AdsAcquire &&
        !replacement.ads_acquisition_active;
    result.replacement_bodylock_authoritative =
        replacement.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        replacement.aim_authority > 0.0f;
    return result;
}

struct RepressCounterfactual {
    bool first_snap_consumed = false;
    bool new_physical_epoch_started = false;
    bool new_acquisition_allocated = false;
    bool new_ads_active = false;
};

RepressCounterfactual run_repress_counterfactual() {
    controller_native::TargetCoordinatorConfig config{};
    config.settle_frames = 1;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    coordinator.begin_ads_epoch(1, 2.0);
    const auto first = coordinator.update(
        selected_frame(10, 2.0, 240.0f, 71, 17), intent, 2.0);

    // This explicit begin call represents a new physical LT rising edge after
    // release. It is the only counterfactual allowed to mint another snap.
    coordinator.begin_ads_epoch(2, 2.050);
    const auto second = coordinator.update(
        selected_frame(11, 2.050, 330.0f, 71, 17), intent, 2.050);

    return {
        first.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            !first.ads_acquisition_active,
        second.physical_ads_epoch == 2,
        first.target_acquisition_id != 0 &&
            second.target_acquisition_id != 0 &&
            second.target_acquisition_id != first.target_acquisition_id,
        second.mode == pipeline_contract::ControlMode::AdsAcquire &&
            second.ads_acquisition_active,
    };
}

struct Report {
    ReplacementCase active_replacement;
    ReplacementCase consumed_replacement;
    RepressCounterfactual repress;
    bool trigger_executed = false;
    bool counterfactual_valid = false;
    bool one_acquisition_during_active_replacement = false;
    bool one_acquisition_after_consumed_replacement = false;
    bool replacement_never_restarts_ads = false;
    bool replacement_keeps_bodylock_authority = false;
    bool overall_pass = false;
};

Report evaluate() {
    Report report{};
    report.active_replacement = run_replacement_case(false);
    report.consumed_replacement = run_replacement_case(true);
    report.repress = run_repress_counterfactual();
    report.trigger_executed =
        report.active_replacement.replacement_observed &&
        report.consumed_replacement.replacement_observed &&
        report.active_replacement.first_acquisition_id != 0 &&
        report.consumed_replacement.first_acquisition_id != 0;
    report.counterfactual_valid =
        report.repress.first_snap_consumed &&
        report.repress.new_physical_epoch_started &&
        report.repress.new_acquisition_allocated &&
        report.repress.new_ads_active;
    report.one_acquisition_during_active_replacement =
        report.active_replacement.acquisition_identity_preserved;
    report.one_acquisition_after_consumed_replacement =
        report.consumed_replacement.acquisition_identity_preserved;
    report.replacement_never_restarts_ads =
        report.active_replacement.replacement_ads_inactive &&
        report.consumed_replacement.replacement_ads_inactive;
    report.replacement_keeps_bodylock_authority =
        report.active_replacement.replacement_bodylock_authoritative &&
        report.consumed_replacement.replacement_bodylock_authoritative;
    report.overall_pass = report.trigger_executed &&
        report.counterfactual_valid &&
        report.one_acquisition_during_active_replacement &&
        report.one_acquisition_after_consumed_replacement &&
        report.replacement_never_restarts_ads &&
        report.replacement_keeps_bodylock_authority;
    return report;
}

void write_case(std::ostream& stream, const ReplacementCase& value) {
    stream << std::boolalpha
           << "{\"first_acquisition_id\":" << value.first_acquisition_id
           << ",\"replacement_acquisition_id\":"
           << value.replacement_acquisition_id
           << ",\"replacement_observed\":" << value.replacement_observed
           << ",\"acquisition_identity_preserved\":"
           << value.acquisition_identity_preserved
           << ",\"replacement_ads_inactive\":"
           << value.replacement_ads_inactive
           << ",\"replacement_bodylock_authoritative\":"
           << value.replacement_bodylock_authoritative << "}";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"One held physical LT can allocate multiple ADS acquisitions when selector identity changes\",\n"
           << "  \"covariates\": {\n"
           << "    \"freshness\": \"two consecutive fresh selector-owned frames 6 ms apart\",\n"
           << "    \"target_generation\": \"7 -> 8 selector-confirmed replacement\",\n"
           << "    \"target_count\": \"one selected target per frame; identity replacement at frame two\",\n"
           << "    \"right_stick_manual\": \"zero\",\n"
           << "    \"left_stick_manual\": \"zero\",\n"
           << "    \"recoil_firing\": \"disabled and not firing\",\n"
           << "    \"controller_mode\": \"TargetCoordinator ADS lifecycle\",\n"
           << "    \"refresh_rate_hz\": 180,\n"
           << "    \"logging_mode\": \"fixture JSON only\"\n"
           << "  },\n"
           << "  \"cases\": {\n"
           << "    \"active_replacement\": ";
    write_case(stream, report.active_replacement);
    stream << ",\n    \"consumed_replacement\": ";
    write_case(stream, report.consumed_replacement);
    stream << ",\n    \"new_physical_lt_counterfactual\": {"
           << "\"first_snap_consumed\":"
           << report.repress.first_snap_consumed
           << ",\"new_physical_epoch_started\":"
           << report.repress.new_physical_epoch_started
           << ",\"new_acquisition_allocated\":"
           << report.repress.new_acquisition_allocated
           << ",\"new_ads_active\":" << report.repress.new_ads_active
           << "}\n  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactual_valid\": "
           << report.counterfactual_valid << ",\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"one_acquisition_during_active_replacement\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.one_acquisition_during_active_replacement << ",\"pass\":" << report.one_acquisition_during_active_replacement << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"one_acquisition_after_consumed_replacement\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.one_acquisition_after_consumed_replacement << ",\"pass\":" << report.one_acquisition_after_consumed_replacement << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"handover_ads_inactive\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.replacement_never_restarts_ads << ",\"pass\":" << report.replacement_never_restarts_ads << "},\n"
           << "    {\"id\":\"O4\",\"metric\":\"handover_bodylock_authoritative\",\"operator\":\"==\",\"threshold\":true,\"observed\":" << report.replacement_keeps_bodylock_authority << ",\"pass\":" << report.replacement_keeps_bodylock_authority << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv, "ads_single_press_token_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " trigger=" << report.trigger_executed
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
