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
#include <vector>

namespace {

constexpr const char* kIncidentId =
    "ads-completion-geometry-continuity-20260825";
constexpr std::uint64_t kSourceId = 26001;
constexpr std::uint64_t kSelectorGeneration = 2601;

pipeline_contract::IntentState ads_intent() {
    pipeline_contract::IntentState intent;
    intent.ads = true;
    return intent;
}

pipeline_contract::VisionObservationBatch frame(
    std::uint64_t frame_id,
    double capture_seconds,
    pipeline_contract::Vec2f error) {
    constexpr float kCenterX = 240.0f;
    constexpr float kCenterY = 208.0f;
    pipeline_contract::VisionObservationBatch batch;
    batch.frame_id = frame_id;
    batch.source_time_seconds = capture_seconds;
    batch.publish_time_seconds = capture_seconds;
    batch.frame_width_px = 480.0f;
    batch.frame_height_px = 416.0f;
    batch.capture_fresh = true;
    batch.selector_identity_protocol = true;
    batch.selector_target_generation = kSelectorGeneration;
    batch.preferred_source_id = kSourceId;
    batch.count = 1;

    auto& candidate = batch.candidates[0];
    candidate.source_id = kSourceId;
    candidate.aim_px = {kCenterX + error.x, kCenterY + error.y};
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
    candidate.reliability = 0.95f;
    candidate.normalized_size = 0.22f;
    candidate.body_cue = true;
    return batch;
}

controller_native::TargetCoordinatorConfig base_config() {
    controller_native::TargetCoordinatorConfig config;
    config.max_observation_age_ms = 1000.0f;
    config.settle_radius_px = 8.0f;
    config.settle_frames = 1000;
    config.ads_nominal_acquisition_ms = 135.0f;
    config.ads_extension_budget_ms = 1000.0f;
    config.visual_authority_enabled = false;
    return config;
}

struct PairResult {
    pipeline_contract::Vec2f before{};
    pipeline_contract::Vec2f after{};
    float interval_ms = 0.0f;
    float geometry_travel_px = 0.0f;
    float terminal_error_px = 0.0f;
    bool same_target = false;
    bool ads_active = false;
    bool center_cross_terminal = false;
};

PairResult run_pair(
    pipeline_contract::Vec2f before,
    pipeline_contract::Vec2f after,
    double interval_seconds,
    std::uint64_t epoch) {
    auto config = base_config();
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    const double start = 10.0 + static_cast<double>(epoch);
    coordinator.begin_ads_epoch(epoch, start);
    const auto first = coordinator.update(
        frame(epoch * 10 + 1, start, before), intent, start);
    const auto second = coordinator.update(
        frame(epoch * 10 + 2, start + interval_seconds, after),
        intent,
        start + interval_seconds);

    PairResult result;
    result.before = before;
    result.after = after;
    result.interval_ms = static_cast<float>(interval_seconds * 1000.0);
    result.geometry_travel_px = std::hypot(
        after.x - before.x, after.y - before.y);
    result.terminal_error_px = std::hypot(after.x, after.y);
    result.same_target = first.target_id != 0 &&
        first.target_id == second.target_id &&
        second.selector_target_generation == kSelectorGeneration;
    result.ads_active =
        second.mode == pipeline_contract::ControlMode::AdsAcquire &&
        second.ads_acquisition_active;
    result.center_cross_terminal =
        second.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        !second.ads_acquisition_active &&
        second.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::CenterCross;
    return result;
}

struct SettleResult {
    std::vector<float> errors;
    float terminal_error_px = 0.0f;
    bool settled_terminal = false;
    bool ads_active = false;
};

SettleResult run_settle_path(
    const std::vector<float>& errors,
    std::uint64_t epoch) {
    auto config = base_config();
    config.settle_frames = 2;
    controller_native::TargetCoordinator coordinator(config);
    const auto intent = ads_intent();
    double now = 50.0 + static_cast<double>(epoch);
    coordinator.begin_ads_epoch(epoch, now);
    pipeline_contract::TargetPlan plan;
    std::uint64_t frame_id = epoch * 100;
    for (float error : errors) {
        plan = coordinator.update(
            frame(++frame_id, now, {error, 0.0f}), intent, now);
        now += 0.006;
    }
    SettleResult result;
    result.errors = errors;
    result.terminal_error_px = std::hypot(plan.error_px.x, plan.error_px.y);
    result.settled_terminal =
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        plan.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::Settled;
    result.ads_active =
        plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        plan.ads_acquisition_active;
    return result;
}

struct Report {
    PairResult logged_jump;
    PairResult genuine_cross;
    PairResult same_side;
    SettleResult inflated_settle;
    SettleResult genuine_settle;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool jump_rejected_as_completion = false;
    bool settle_requires_true_radius = false;
    bool overall_pass = false;
};

Report evaluate() {
    Report report;
    // Exact same-target geometry signature from COD22 track 1523.
    report.logged_jump = run_pair(
        {3.04f, -53.55f}, {-1.25f, 82.47f}, 0.005999, 1);
    // A small, plausible radial pass through the reticle remains a valid
    // early completion counterfactual.
    report.genuine_cross = run_pair(
        {8.0f, 0.0f}, {-6.0f, 0.0f}, 0.006, 2);
    report.same_side = run_pair(
        {12.0f, -2.0f}, {7.0f, -1.0f}, 0.006, 3);
    // The moving capture envelope may help reason about continuity, but it
    // must not redefine an 8 px completion radius into a 20+ px success.
    report.inflated_settle = run_settle_path(
        {23.0f, 22.0f, 21.0f}, 4);
    report.genuine_settle = run_settle_path(
        {7.0f, 6.0f}, 5);

    report.trigger_executed =
        report.logged_jump.same_target &&
        report.logged_jump.interval_ms >= 5.9f &&
        report.logged_jump.interval_ms <= 6.1f &&
        report.logged_jump.geometry_travel_px >= 130.0f &&
        report.logged_jump.terminal_error_px >= 80.0f &&
        report.inflated_settle.terminal_error_px >= 20.0f;
    report.counterfactuals_valid =
        report.genuine_cross.same_target &&
        report.genuine_cross.center_cross_terminal &&
        report.same_side.same_target && report.same_side.ads_active &&
        report.genuine_settle.settled_terminal;
    report.jump_rejected_as_completion =
        report.logged_jump.ads_active &&
        !report.logged_jump.center_cross_terminal;
    report.settle_requires_true_radius =
        report.inflated_settle.ads_active &&
        !report.inflated_settle.settled_terminal;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        report.jump_rejected_as_completion &&
        report.settle_requires_true_radius;
    return report;
}

void write_pair(
    std::ostream& stream,
    const char* name,
    const PairResult& value,
    bool comma) {
    stream << "    \"" << name << "\": {"
           << "\"before\":{" << "\"x\":" << value.before.x
           << ",\"y\":" << value.before.y << "},"
           << "\"after\":{" << "\"x\":" << value.after.x
           << ",\"y\":" << value.after.y << "},"
           << "\"interval_ms\":" << value.interval_ms << ","
           << "\"geometry_travel_px\":" << value.geometry_travel_px << ","
           << "\"terminal_error_px\":" << value.terminal_error_px << ","
           << "\"same_target\":" << value.same_target << ","
           << "\"ads_active\":" << value.ads_active << ","
           << "\"center_cross_terminal\":"
           << value.center_cross_terminal << "}"
           << (comma ? "," : "") << "\n";
}

void write_settle(
    std::ostream& stream,
    const char* name,
    const SettleResult& value,
    bool comma) {
    stream << "    \"" << name << "\": {\"errors_px\":[";
    for (std::size_t index = 0; index < value.errors.size(); ++index) {
        if (index != 0) stream << ',';
        stream << value.errors[index];
    }
    stream << "],\"terminal_error_px\":" << value.terminal_error_px
           << ",\"settled_terminal\":" << value.settled_terminal
           << ",\"ads_active\":" << value.ads_active << "}"
           << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output, std::ios::out | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open report output");
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"a detector geometry jump or velocity-expanded radius is mistaken for completed ADS positioning\",\n"
           << "  \"pairs\": {\n";
    write_pair(stream, "logged_geometry_jump", report.logged_jump, true);
    write_pair(stream, "genuine_center_cross", report.genuine_cross, true);
    write_pair(stream, "same_side_counterfactual", report.same_side, false);
    stream << "  },\n"
           << "  \"settle_paths\": {\n";
    write_settle(stream, "moving_21px_path", report.inflated_settle, true);
    write_settle(stream, "inside_8px_path", report.genuine_settle, false);
    stream << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed
           << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"geometry_jump_does_not_complete\": "
           << report.jump_rejected_as_completion << ",\n"
           << "    \"settle_uses_configured_radius\": "
           << report.settle_requires_true_radius << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return argv[2];
    }
    if (argc == 1) {
        return "ads_completion_geometry_continuity_incident.json";
    }
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

}  // namespace

int run_ads_completion_geometry_continuity_incident_regression(
    int argc,
    char** argv) {
    try {
        const auto output = output_path_from_args(argc, argv);
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " logged_jump_terminal="
                  << report.logged_jump.center_cross_terminal
                  << " moving_21px_settled="
                  << report.inflated_settle.settled_terminal
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_ads_completion_geometry_continuity_incident_regression(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseAds",
        "incident_ads_completion_geometry_continuity",
        "ads_completion_geometry_continuity_incident.json",
        run_ads_completion_geometry_continuity_incident_regression);
}
