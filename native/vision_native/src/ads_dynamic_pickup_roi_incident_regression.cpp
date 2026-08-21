#include "vision_native/target_selector.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kIncidentId =
    "ads-dynamic-pickup-roi-20260821";
constexpr float kFrameWidth = 640.0f;
constexpr float kFrameHeight = 512.0f;
constexpr float kBasePickupRadiusPx = 150.0f;
constexpr float kTargetSizeRadiusScale = 0.75f;

struct CaseSpec {
    const char* name = "";
    float dx = 0.0f;
    float dy = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool expected_target = false;
};

struct CaseResult {
    const char* name = "";
    float target_distance_px = 0.0f;
    float normalized_height = 0.0f;
    float effective_pickup_radius_px = 0.0f;
    bool inside_pickup_envelope = false;
    bool selected_target = false;
    bool expected_target = false;
    bool oracle_pass = false;
};

struct Report {
    std::vector<CaseResult> cases;
    bool active_continuation_retained = false;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool overall_pass = false;
};

float pickup_radius(float height) {
    const float normalized_height = std::clamp(height / kFrameHeight, 0.0f, 1.0f);
    return kBasePickupRadiusPx *
        (1.0f + kTargetSizeRadiusScale * normalized_height);
}

vision_native::Detection detection_for(const CaseSpec& spec) {
    vision_native::Detection detection;
    const float target_x = kFrameWidth * 0.5f + spec.dx;
    const float target_y = kFrameHeight * 0.5f + spec.dy;
    detection.x1 = target_x - spec.width * 0.5f;
    detection.x2 = target_x + spec.width * 0.5f;
    detection.y1 = target_y - spec.height * 0.40f;
    detection.y2 = detection.y1 + spec.height;
    detection.conf = 0.92f;
    return detection;
}

vision_native::DetectionBatch batch_for(const CaseSpec& spec) {
    vision_native::DetectionBatch batch;
    batch.frame_width = static_cast<int>(kFrameWidth);
    batch.frame_height = static_cast<int>(kFrameHeight);
    batch.detections.push_back(detection_for(spec));
    return batch;
}

CaseResult run_case(const CaseSpec& spec) {
    vision_native::VisionTargetSelector selector(
        static_cast<int>(kFrameWidth),
        static_cast<int>(kFrameHeight));
    const auto batch = batch_for(spec);
    (void)selector.select(batch);
    const auto result = selector.select(batch);

    CaseResult value;
    value.name = spec.name;
    value.target_distance_px = std::hypot(spec.dx, spec.dy);
    value.normalized_height = spec.height / kFrameHeight;
    value.effective_pickup_radius_px = pickup_radius(spec.height);
    value.inside_pickup_envelope =
        value.target_distance_px <= value.effective_pickup_radius_px;
    value.selected_target = result.has_target;
    value.expected_target = spec.expected_target;
    value.oracle_pass = value.selected_target == value.expected_target;
    return value;
}

bool active_continuation_retained() {
    vision_native::VisionTargetSelector selector(
        static_cast<int>(kFrameWidth),
        static_cast<int>(kFrameHeight));
    CaseSpec moving{
        "active_continuation", 60.0f, 0.0f, 60.0f, 140.0f, true};
    auto batch = batch_for(moving);
    (void)selector.select(batch);
    if (!selector.select(batch).has_target) return false;

    for (float dx = 90.0f; dx <= 210.0f; dx += 30.0f) {
        moving.dx = dx;
        if (!selector.select(batch_for(moving)).has_target) return false;
    }
    return true;
}

Report evaluate() {
    // Values are copied from the exact telemetry joins for the two supplied
    // videos, with two positive counterfactuals for the requested product rule.
    const std::vector<CaseSpec> specs{
        {"video2_far_target", 290.667f, -27.6667f, 59.3334f, 148.333f, false},
        {"video1_edge_small_target", 122.167f, -55.2f, 31.0f, 65.3333f, true},
        {"video1_near_target", -67.1667f, -6.46666f, 53.0f, 112.167f, true},
        {"close_large_target", 165.0f, 0.0f, 120.0f, 300.0f, true},
        {"extreme_close_edge_target", 250.0f, 0.0f, 220.0f, 480.0f, true},
    };

    Report report;
    for (const auto& spec : specs) report.cases.push_back(run_case(spec));
    report.active_continuation_retained = active_continuation_retained();
    report.trigger_executed =
        !report.cases[0].inside_pickup_envelope &&
        report.cases[1].inside_pickup_envelope;
    report.counterfactuals_valid =
        report.cases[2].inside_pickup_envelope &&
        report.cases[3].inside_pickup_envelope &&
        report.cases[4].inside_pickup_envelope &&
        report.active_continuation_retained;
    report.overall_pass = report.trigger_executed && report.counterfactuals_valid;
    for (const auto& value : report.cases) {
        report.overall_pass = report.overall_pass && value.oracle_pass;
    }
    return report;
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--output") return argv[index + 1];
    }
    return "ads_dynamic_pickup_roi_incident.json";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open incident report");
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"thresholds\": {\"base_pickup_radius_px\": "
           << kBasePickupRadiusPx
           << ", \"target_size_radius_scale\": "
           << kTargetSizeRadiusScale << "},\n"
           << "  \"cases\": {\n";
    for (std::size_t index = 0; index < report.cases.size(); ++index) {
        const auto& value = report.cases[index];
        stream << "    \"" << value.name << "\": {"
               << "\"target_distance_px\":" << value.target_distance_px
               << ",\"normalized_height\":" << value.normalized_height
               << ",\"effective_pickup_radius_px\":"
               << value.effective_pickup_radius_px
               << ",\"inside_pickup_envelope\":"
               << value.inside_pickup_envelope
               << ",\"selected_target\":" << value.selected_target
               << ",\"expected_target\":" << value.expected_target
               << ",\"oracle_pass\":" << value.oracle_pass << "}"
               << (index + 1 < report.cases.size() ? "," : "") << "\n";
    }
    stream << "  },\n"
           << "  \"active_continuation_retained\": "
           << report.active_continuation_retained << ",\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_dynamic_pickup_roi_incident_regression(int argc, char** argv) {
    try {
        const Report report = evaluate();
        write_report(output_path_from_args(argc, argv), report);
        std::cout << "incident=" << kIncidentId
                  << " trigger=" << report.trigger_executed
                  << " counterfactuals=" << report.counterfactuals_valid
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_ads_dynamic_pickup_roi_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseVisionSelection", "incident_ads_dynamic_pickup_roi", "ads_dynamic_pickup_roi_incident.json", run_ads_dynamic_pickup_roi_incident_regression);
}
