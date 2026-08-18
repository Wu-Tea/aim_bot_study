#include "ads_acquisition_controller.h"
#include "aim_dynamics_shaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kIncidentId =
    "ads-close-acquisition-pacing-20260818";
constexpr int kDurationMs = 240;
constexpr int kVisionDelayMs = 3;
constexpr int kControlEffectDelayMs = 9;
constexpr float kPlantResponse = 500.0f;
constexpr float kInitialErrorPx = 25.0f;
constexpr float kSettleRadiusPx = 8.0f;
constexpr float kNominalHorizonSeconds = 0.135f;
constexpr float kCloseNormalizedSize = 0.42f;
constexpr float kMidFarNormalizedSize = 0.12f;

constexpr int kMinimumCloseSettleMs = 90;
constexpr int kMaximumCloseSettleMs = 125;
constexpr float kMinimumClosePeakOutput = 0.45f;
constexpr float kMaximumClosePeakOutput = 0.62f;
constexpr float kMaximumCloseFinalErrorPx = 2.0f;
constexpr float kMaximumCloseOvershootPx = 1.0f;
constexpr int kMinimumMidFarSettleMs = 130;
constexpr int kMaximumMidFarSettleMs = 180;

struct PendingObservation {
    int ready_ms = 0;
    float error_px = 0.0f;
};

struct ScenarioSpec {
    const char* name = "";
    float normalized_size = 0.0f;
    bool vertical = false;
};

struct ScenarioResult {
    const char* name = "";
    float normalized_size = 0.0f;
    bool vertical = false;
    int fresh_observations = 0;
    int settle_latency_ms = -1;
    float peak_output = 0.0f;
    float final_error_px = 0.0f;
    float overshoot_px = 0.0f;
};

struct Report {
    float configured_close_horizon_ms = 0.0f;
    ScenarioResult close_x{};
    ScenarioResult close_y{};
    ScenarioResult mid_far_x{};
    bool trigger_executed = false;
    bool counterfactual_valid = false;
    bool close_pacing_oracle = false;
    bool close_output_oracle = false;
    bool close_accuracy_oracle = false;
    bool overall_pass = false;
};

controller_native::AdsAcquisitionControllerConfig controller_config() {
    controller_native::AdsAcquisitionControllerConfig config{};
    config.arrival_horizon_seconds = kNominalHorizonSeconds;
    config.response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    return config;
}

pipeline_contract::TargetPlan admitted_ads_plan(
    float normalized_size) {
    pipeline_contract::TargetPlan plan{};
    plan.target_id = 1818;
    plan.target_acquisition_id = 1;
    plan.ads_acquisition_exists = true;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    plan.aim_authority = 1.0f;
    plan.reliability = 1.0f;
    plan.confidence = 1.0f;
    plan.response_scale = kPlantResponse;
    plan.response_confidence = 1.0f;
    plan.normalized_size = normalized_size;
    return plan;
}

ScenarioResult run_scenario(const ScenarioSpec& spec) {
    controller_native::AdsAcquisitionController controller(
        controller_config());
    controller_native::AimDynamicsShaper shaper;
    pipeline_contract::TargetPlan plan{};
    pipeline_contract::IntentState neutral_intent{};

    float true_error_px = kInitialErrorPx;
    float minimum_error_px = true_error_px;
    std::deque<PendingObservation> pending;
    std::deque<float> delayed_controls(
        static_cast<std::size_t>(kControlEffectDelayMs), 0.0f);
    bool use_six_ms_interval = true;
    int next_capture_ms = 0;

    ScenarioResult result;
    result.name = spec.name;
    result.normalized_size = spec.normalized_size;
    result.vertical = spec.vertical;

    for (int now_ms = 0; now_ms < kDurationMs; ++now_ms) {
        if (now_ms == next_capture_ms) {
            pending.push_back({now_ms + kVisionDelayMs, true_error_px});
            next_capture_ms += use_six_ms_interval ? 6 : 5;
            use_six_ms_interval = !use_six_ms_interval;
        }

        if (!pending.empty() && pending.front().ready_ms == now_ms) {
            plan = admitted_ads_plan(spec.normalized_size);
            if (spec.vertical) {
                plan.error_px.y = pending.front().error_px;
            } else {
                plan.error_px.x = pending.front().error_px;
            }
            pending.pop_front();
            ++result.fresh_observations;
        }

        const auto requested = controller.compute(
            plan, neutral_intent, 0.001f);
        const auto shaped = shaper.shape(
            requested, neutral_intent, plan, 0.001f);
        const float delivered = spec.vertical ? shaped.y : shaped.x;
        result.peak_output = std::max(
            result.peak_output, std::fabs(delivered));

        delayed_controls.push_back(delivered);
        const float applied = delayed_controls.front();
        delayed_controls.pop_front();
        if (spec.vertical) {
            // Positive screen Y error requires negative stick Y.
            true_error_px += applied * kPlantResponse * 0.001f;
        } else {
            true_error_px -= applied * kPlantResponse * 0.001f;
        }
        minimum_error_px = std::min(minimum_error_px, true_error_px);
        if (result.settle_latency_ms < 0 &&
            std::fabs(true_error_px) <= kSettleRadiusPx) {
            result.settle_latency_ms = now_ms + 1;
        }
    }

    result.final_error_px = true_error_px;
    result.overshoot_px = std::max(0.0f, -minimum_error_px);
    return result;
}

bool close_pacing_ok(const ScenarioResult& value) {
    return value.settle_latency_ms >= kMinimumCloseSettleMs &&
        value.settle_latency_ms <= kMaximumCloseSettleMs;
}

bool close_output_ok(const ScenarioResult& value) {
    return value.peak_output >= kMinimumClosePeakOutput &&
        value.peak_output <= kMaximumClosePeakOutput;
}

bool close_accuracy_ok(const ScenarioResult& value) {
    return std::fabs(value.final_error_px) <= kMaximumCloseFinalErrorPx &&
        value.overshoot_px <= kMaximumCloseOvershootPx;
}

Report evaluate() {
    Report report;
    const auto config = controller_config();
    report.configured_close_horizon_ms =
        config.close_arrival_horizon_seconds * 1000.0f;
    report.close_x = run_scenario(
        {"close_horizontal", kCloseNormalizedSize, false});
    report.close_y = run_scenario(
        {"close_vertical_below", kCloseNormalizedSize, true});
    report.mid_far_x = run_scenario(
        {"mid_far_horizontal", kMidFarNormalizedSize, false});
    report.trigger_executed =
        report.close_x.fresh_observations >= 40 &&
        report.close_y.fresh_observations >= 40 &&
        report.close_x.normalized_size >= 0.40f &&
        report.close_y.normalized_size >= 0.40f;
    report.counterfactual_valid =
        report.mid_far_x.fresh_observations >= 40 &&
        report.mid_far_x.settle_latency_ms >= kMinimumMidFarSettleMs &&
        report.mid_far_x.settle_latency_ms <= kMaximumMidFarSettleMs;
    report.close_pacing_oracle =
        close_pacing_ok(report.close_x) && close_pacing_ok(report.close_y);
    report.close_output_oracle =
        close_output_ok(report.close_x) && close_output_ok(report.close_y);
    report.close_accuracy_oracle =
        close_accuracy_ok(report.close_x) && close_accuracy_ok(report.close_y);
    report.overall_pass = report.trigger_executed &&
        report.counterfactual_valid && report.close_pacing_oracle &&
        report.close_output_oracle && report.close_accuracy_oracle;
    return report;
}

void write_scenario(
    std::ostream& stream,
    const ScenarioResult& value) {
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\"normalized_size\":" << value.normalized_size
           << ",\"vertical\":" << value.vertical
           << ",\"fresh_observations\":" << value.fresh_observations
           << ",\"settle_latency_ms\":" << value.settle_latency_ms
           << ",\"peak_output\":" << value.peak_output
           << ",\"final_error_px\":" << value.final_error_px
           << ",\"overshoot_px\":" << value.overshoot_px << "}";
}

void write_report(
    const std::filesystem::path& output_path,
    const Report& report) {
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream stream(output_path, std::ios::trunc);
    if (!stream) throw std::runtime_error("failed to open report output");
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"Close-range ADS reaches the target with an unnecessarily abrupt initial transfer even though the same final accuracy can be retained with a slower close arrival horizon\",\n"
           << "  \"covariates\": {\"controller_hz\":1000,\"vision_hz\":180,\"vision_delay_ms\":"
           << kVisionDelayMs
           << ",\"control_effect_delay_ms\":" << kControlEffectDelayMs
           << ",\"plant_response_px_per_stick_second\":" << kPlantResponse
           << ",\"initial_error_px\":" << kInitialErrorPx
           << ",\"settle_radius_px\":" << kSettleRadiusPx
           << ",\"nominal_horizon_ms\":" << kNominalHorizonSeconds * 1000.0f
           << ",\"configured_close_horizon_ms\":"
           << report.configured_close_horizon_ms
           << ",\"right_stick_manual\":\"zero\",\"left_stick_manual\":\"zero\",\"recoil_firing\":\"disabled and not firing\",\"controller_mode\":\"admitted production AdsAcquisitionController plus AimDynamicsShaper\",\"logging_mode\":\"fixture JSON only\"},\n"
           << "  \"scenarios\": {\n"
           << "    \"close_horizontal\": ";
    write_scenario(stream, report.close_x);
    stream << ",\n    \"close_vertical_below\": ";
    write_scenario(stream, report.close_y);
    stream << ",\n    \"mid_far_horizontal\": ";
    write_scenario(stream, report.mid_far_x);
    stream << "\n  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactual_valid\": " << report.counterfactual_valid << ",\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"close_settle_latency_ms\",\"operator\":\"between_inclusive\",\"threshold\":["
           << kMinimumCloseSettleMs << ',' << kMaximumCloseSettleMs
           << "],\"observed_x\":" << report.close_x.settle_latency_ms
           << ",\"observed_y\":" << report.close_y.settle_latency_ms
           << ",\"pass\":" << report.close_pacing_oracle << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"close_peak_output\",\"operator\":\"between_inclusive\",\"threshold\":["
           << kMinimumClosePeakOutput << ',' << kMaximumClosePeakOutput
           << "],\"observed_x\":" << report.close_x.peak_output
           << ",\"observed_y\":" << report.close_y.peak_output
           << ",\"pass\":" << report.close_output_oracle << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"close_final_accuracy\",\"operator\":\"final_abs_error_and_overshoot_lte\",\"threshold\":["
           << kMaximumCloseFinalErrorPx << ',' << kMaximumCloseOvershootPx
           << "],\"observed_final_x\":" << std::fabs(report.close_x.final_error_px)
           << ",\"observed_final_y\":" << std::fabs(report.close_y.final_error_px)
           << ",\"observed_overshoot_x\":" << report.close_x.overshoot_px
           << ",\"observed_overshoot_y\":" << report.close_y.overshoot_px
           << ",\"pass\":" << report.close_accuracy_oracle << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return std::filesystem::path(argv[2]);
    }
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output_path = output_path_from_args(argc, argv);
        const auto report = evaluate();
        write_report(output_path, report);
        std::cout << "[AdsCloseAcquisitionPacingIncident] close_ms="
                  << report.close_x.settle_latency_ms
                  << " close_peak=" << report.close_x.peak_output
                  << " final_error=" << report.close_x.final_error_px
                  << " mid_far_ms=" << report.mid_far_x.settle_latency_ms
                  << " result=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactual_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[AdsCloseAcquisitionPacingIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}
