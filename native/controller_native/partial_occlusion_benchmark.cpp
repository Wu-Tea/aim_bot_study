#include "partial_occlusion_benchmark.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace controller_native::partial_occlusion {
namespace {

double clamp_score(double value) noexcept {
    return std::clamp(value, 0.0, 100.0);
}

double decreasing_band(double value, double good, double bad) noexcept {
    if (value <= good) return 100.0;
    if (value >= bad) return 0.0;
    return 100.0 * (bad - value) / (bad - good);
}

std::string escape_json(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += ch; break;
        }
    }
    return result;
}

}  // namespace

ScenarioDefinition build_scenario(ScenarioKind kind, std::uint32_t seed) {
    ScenarioDefinition result;
    result.name = kind == ScenarioKind::Combat
        ? "partial_occlusion_combat"
        : "partial_occlusion_human_errors";
    result.seed = seed;

    constexpr std::array<int, 4> kDirectionX{1, 1, -1, -1};
    constexpr std::array<int, 4> kDirectionY{1, -1, 1, -1};
    constexpr std::array<int, 4> kGapMs{30, 60, 110, 160};
    constexpr std::array<HumanErrorKind, 4> kErrors{
        HumanErrorKind::StaleDirection,
        HumanErrorKind::WrongX,
        HumanErrorKind::WrongY,
        HumanErrorKind::CrossingInertia,
    };
    constexpr std::array<int, 4> kErrorHoldMs{120, 100, 100, 160};

    result.cases.reserve(4);
    for (int index = 0; index < 4; ++index) {
        ScenarioCase value;
        value.index = index;
        value.direction_x = kDirectionX[index];
        value.direction_y = kDirectionY[index];
        value.observation_gap_ms = kGapMs[index];
        value.error_kind = kind == ScenarioKind::Combat
            ? HumanErrorKind::None
            : kErrors[index];
        value.error_hold_ms = kind == ScenarioKind::Combat ? 0 : kErrorHoldMs[index];
        value.manual_magnitude_cap = kind == ScenarioKind::Combat ? 0.45 : 0.60;
        value.target_velocity_x_px_per_sec = value.direction_x * (205.0 + index * 18.0);
        value.target_velocity_y_px_per_sec = value.direction_y * (105.0 + index * 15.0);
        value.left_stick_x = value.direction_x * (index % 2 == 0 ? -0.42 : 0.42);
        result.cases.push_back(value);
    }
    return result;
}

double truth_chest_y_px(
    double full_body_top_y_px,
    double full_body_height_px,
    double aim_height_ratio) noexcept {
    return full_body_top_y_px +
        full_body_height_px * std::clamp(aim_height_ratio, 0.0, 1.0);
}

double observed_chest_y_px(
    double observed_body_top_y_px,
    double observed_body_height_px,
    double aim_height_ratio) noexcept {
    return observed_body_top_y_px +
        observed_body_height_px * std::clamp(aim_height_ratio, 0.0, 1.0);
}

PartialOcclusionScore score_metrics(const PartialOcclusionMetrics& metrics) noexcept {
    PartialOcclusionScore score;
    const double mean_component = decreasing_band(metrics.mean_error_px, 8.0, 50.0);
    const double p95_component = decreasing_band(metrics.p95_error_px, 18.0, 75.0);
    score.tracking = clamp_score(mean_component * 0.45 + p95_component * 0.55);

    const double axis_overshoot = std::max(
        metrics.max_overshoot_x_px,
        metrics.max_overshoot_y_px);
    score.overshoot = clamp_score(decreasing_band(axis_overshoot, 2.0, 35.0));
    score.recovery = clamp_score(decreasing_band(metrics.mean_recovery_ms, 80.0, 350.0));

    const double delta_score = decreasing_band(metrics.p95_output_delta, 0.04, 0.18);
    score.smoothness = clamp_score(delta_score - metrics.output_spikes * 5.0);

    const double denominator = std::max(1, metrics.measured_frames);
    const double correct_fight_rate =
        metrics.correct_manual_opposition_frames / denominator;
    const double wrong_full_force_rate =
        metrics.wrong_manual_high_force_frames / denominator;
    score.intent = clamp_score(
        100.0 - correct_fight_rate * 180.0 - wrong_full_force_rate * 110.0);

    score.overall = clamp_score(
        score.tracking * 0.30 +
        score.overshoot * 0.20 +
        score.recovery * 0.20 +
        score.smoothness * 0.15 +
        score.intent * 0.15);
    return score;
}

std::string render_report_json(
    const BenchmarkMetadata& metadata,
    const std::vector<ScenarioReport>& scenarios) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\n"
        << "  \"benchmark\": \"partial_occlusion_combat\",\n"
        << "  \"seed\": " << metadata.seed << ",\n"
        << "  \"config_path\": \"" << escape_json(metadata.config_path) << "\",\n"
        << "  \"effective_config\": {\n"
        << "    \"aim_height_ratio\": " << metadata.aim_height_ratio << ",\n"
        << "    \"ads_strength_scale\": " << metadata.ads_strength_scale << ",\n"
        << "    \"ads_vertical_strength_scale\": " << metadata.ads_vertical_strength_scale << ",\n"
        << "    \"ads_range_px\": " << metadata.ads_range_px << ",\n"
        << "    \"ads_snap_duration_ms\": " << metadata.ads_snap_duration_ms << ",\n"
        << "    \"bodylock_strength\": " << metadata.bodylock_strength << ",\n"
        << "    \"bodylock_vertical_strength\": " << metadata.bodylock_vertical_strength << ",\n"
        << "    \"bodylock_activation_range_px\": " << metadata.bodylock_activation_range_px << ",\n"
        << "    \"bodylock_tolerance_px\": " << metadata.bodylock_tolerance_px << "\n"
        << "  },\n"
        << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
        const auto& report = scenarios[index];
        const auto& m = report.metrics;
        const auto& s = report.score;
        out << "    {\n"
            << "      \"name\": \"" << escape_json(report.name) << "\",\n"
            << "      \"metrics\": {\n"
            << "        \"cases\": " << m.cases << ",\n"
            << "        \"measured_frames\": " << m.measured_frames << ",\n"
            << "        \"vision_samples\": " << m.vision_samples << ",\n"
            << "        \"missing_vision_samples\": " << m.missing_vision_samples << ",\n"
            << "        \"ads_snap_frames\": " << m.ads_snap_frames << ",\n"
            << "        \"body_lock_frames\": " << m.body_lock_frames << ",\n"
            << "        \"manual_frames\": " << m.manual_frames << ",\n"
            << "        \"mode_changes\": " << m.mode_changes << ",\n"
            << "        \"output_spikes\": " << m.output_spikes << ",\n"
            << "        \"correct_manual_opposition_frames\": " << m.correct_manual_opposition_frames << ",\n"
            << "        \"wrong_manual_high_force_frames\": " << m.wrong_manual_high_force_frames << ",\n"
            << "        \"mean_error_px\": " << m.mean_error_px << ",\n"
            << "        \"p95_error_px\": " << m.p95_error_px << ",\n"
            << "        \"final_error_px\": " << m.final_error_px << ",\n"
            << "        \"peak_error_px\": " << m.peak_error_px << ",\n"
            << "        \"max_overshoot_x_px\": " << m.max_overshoot_x_px << ",\n"
            << "        \"max_overshoot_y_px\": " << m.max_overshoot_y_px << ",\n"
            << "        \"occlusion_peak_error_px\": " << m.occlusion_peak_error_px << ",\n"
            << "        \"mean_recovery_ms\": " << m.mean_recovery_ms << ",\n"
            << "        \"p95_output_delta\": " << m.p95_output_delta << ",\n"
            << "        \"peak_geometry_bias_px\": " << m.peak_geometry_bias_px << "\n"
            << "      },\n"
            << "      \"score\": {\n"
            << "        \"formula_version\": " << s.formula_version << ",\n"
            << "        \"tracking\": " << s.tracking << ",\n"
            << "        \"overshoot\": " << s.overshoot << ",\n"
            << "        \"recovery\": " << s.recovery << ",\n"
            << "        \"smoothness\": " << s.smoothness << ",\n"
            << "        \"intent\": " << s.intent << ",\n"
            << "        \"overall\": " << s.overall << "\n"
            << "      }\n"
            << "    }" << (index + 1 < scenarios.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

const char* to_string(HumanErrorKind value) noexcept {
    switch (value) {
        case HumanErrorKind::None: return "none";
        case HumanErrorKind::StaleDirection: return "stale_direction";
        case HumanErrorKind::WrongX: return "wrong_x";
        case HumanErrorKind::WrongY: return "wrong_y";
        case HumanErrorKind::CrossingInertia: return "crossing_inertia";
    }
    return "unknown";
}

}  // namespace controller_native::partial_occlusion
