#include "partial_occlusion_benchmark.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace controller_native::partial_occlusion {
namespace {

constexpr double kPi = 3.14159265358979323846;

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

void write_metrics_json(
    std::ostringstream& out,
    const PartialOcclusionMetrics& m,
    const std::string& indent) {
    out << indent << "{\n"
        << indent << "  \"cases\": " << m.cases << ",\n"
        << indent << "  \"measured_frames\": " << m.measured_frames << ",\n"
        << indent << "  \"vision_samples\": " << m.vision_samples << ",\n"
        << indent << "  \"missing_vision_samples\": " << m.missing_vision_samples << ",\n"
        << indent << "  \"ads_snap_frames\": " << m.ads_snap_frames << ",\n"
        << indent << "  \"body_lock_frames\": " << m.body_lock_frames << ",\n"
        << indent << "  \"manual_frames\": " << m.manual_frames << ",\n"
        << indent << "  \"mode_changes\": " << m.mode_changes << ",\n"
        << indent << "  \"output_spikes\": " << m.output_spikes << ",\n"
        << indent << "  \"correct_manual_opposition_frames\": "
        << m.correct_manual_opposition_frames << ",\n"
        << indent << "  \"wrong_manual_high_force_frames\": "
        << m.wrong_manual_high_force_frames << ",\n"
        << indent << "  \"axis_intervention_x_frames\": "
        << m.axis_intervention_x_frames << ",\n"
        << indent << "  \"axis_intervention_y_frames\": "
        << m.axis_intervention_y_frames << ",\n"
        << indent << "  \"mean_error_px\": " << m.mean_error_px << ",\n"
        << indent << "  \"p95_error_px\": " << m.p95_error_px << ",\n"
        << indent << "  \"final_error_px\": " << m.final_error_px << ",\n"
        << indent << "  \"peak_error_px\": " << m.peak_error_px << ",\n"
        << indent << "  \"max_overshoot_x_px\": " << m.max_overshoot_x_px << ",\n"
        << indent << "  \"max_overshoot_y_px\": " << m.max_overshoot_y_px << ",\n"
        << indent << "  \"occlusion_peak_error_px\": " << m.occlusion_peak_error_px << ",\n"
        << indent << "  \"mean_recovery_ms\": " << m.mean_recovery_ms << ",\n"
        << indent << "  \"p95_output_delta\": " << m.p95_output_delta << ",\n"
        << indent << "  \"peak_geometry_bias_px\": " << m.peak_geometry_bias_px << ",\n"
        << indent << "  \"error_window_mean_px\": " << m.error_window_mean_px << ",\n"
        << indent << "  \"error_window_p95_px\": " << m.error_window_p95_px << ",\n"
        << indent << "  \"error_window_peak_px\": " << m.error_window_peak_px << ",\n"
        << indent << "  \"error_window_recovery_ms\": "
        << m.error_window_recovery_ms << ",\n"
        << indent << "  \"max_observation_offset_px\": "
        << m.max_observation_offset_px << ",\n"
        << indent << "  \"peak_manual_error_x\": " << m.peak_manual_error_x << ",\n"
        << indent << "  \"peak_manual_error_y\": " << m.peak_manual_error_y << ",\n"
        << indent << "  \"manual_error_active_frames\": "
        << m.manual_error_active_frames << "\n"
        << indent << "}";
}

void write_score_json(
    std::ostringstream& out,
    const PartialOcclusionScore& s,
    const std::string& indent) {
    out << indent << "{\n"
        << indent << "  \"formula_version\": " << s.formula_version << ",\n"
        << indent << "  \"tracking\": " << s.tracking << ",\n"
        << indent << "  \"overshoot\": " << s.overshoot << ",\n"
        << indent << "  \"recovery\": " << s.recovery << ",\n"
        << indent << "  \"smoothness\": " << s.smoothness << ",\n"
        << indent << "  \"intent\": " << s.intent << ",\n"
        << indent << "  \"overall\": " << s.overall << "\n"
        << indent << "}";
}

}  // namespace

ScenarioDefinition build_scenario(ScenarioKind kind, std::uint32_t seed) {
    ScenarioDefinition result;
    switch (kind) {
        case ScenarioKind::Combat:
            result.name = "partial_occlusion_combat";
            break;
        case ScenarioKind::HumanErrors:
            result.name = "partial_occlusion_human_errors";
            break;
        case ScenarioKind::PracticalStress:
            result.name = "partial_occlusion_practical_stress";
            break;
        case ScenarioKind::DestructiveStress:
            result.name = "partial_occlusion_destructive_stress";
            break;
    }
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
    constexpr std::array<HumanErrorKind, 4> kStressErrors{
        HumanErrorKind::WrongX,
        HumanErrorKind::WrongY,
        HumanErrorKind::WrongBoth,
        HumanErrorKind::MixedAxes,
    };
    const bool practical = kind == ScenarioKind::PracticalStress;
    const bool destructive = kind == ScenarioKind::DestructiveStress;
    const bool stress = practical || destructive;

    result.cases.reserve(4);
    for (int index = 0; index < 4; ++index) {
        ScenarioCase value;
        value.index = index;
        value.direction_x = kDirectionX[index];
        value.direction_y = kDirectionY[index];
        value.observation_gap_ms = kGapMs[index];
        value.error_kind = kind == ScenarioKind::Combat
            ? HumanErrorKind::None
            : (stress ? kStressErrors[index] : kErrors[index]);
        value.error_hold_ms = kind == ScenarioKind::Combat
            ? 0
            : (practical ? 180 : (destructive ? 320 : kErrorHoldMs[index]));
        if (kind == ScenarioKind::HumanErrors &&
            (value.error_kind == HumanErrorKind::WrongX ||
             value.error_kind == HumanErrorKind::WrongY)) {
            value.error_onset_ms = 70;
        }
        if (stress) value.error_onset_ms = 70;
        value.manual_magnitude_cap = kind == ScenarioKind::Combat ? 0.45 : 0.60;
        if (kind == ScenarioKind::HumanErrors &&
            (value.error_kind == HumanErrorKind::WrongX ||
             value.error_kind == HumanErrorKind::WrongY)) {
            value.manual_magnitude_cap = 0.40;
        }
        if (practical) {
            value.manual_magnitude_cap = 0.70;
            value.stress_tier = StressTier::Practical;
            value.truth_motion_amplitude_x_px_per_sec = 260.0 + index * 20.0;
            value.truth_motion_amplitude_y_px_per_sec = 150.0 + index * 15.0;
            value.observation_jitter_x_px = 12.0;
            value.observation_jitter_y_px = 9.0;
            value.observation_jump_px = 20.0;
        } else if (destructive) {
            value.manual_magnitude_cap = 0.95;
            value.stress_tier = StressTier::Destructive;
            value.truth_motion_amplitude_x_px_per_sec = 520.0 + index * 30.0;
            value.truth_motion_amplitude_y_px_per_sec = 320.0 + index * 20.0;
            value.observation_jitter_x_px = 32.0;
            value.observation_jitter_y_px = 26.0;
            value.observation_jump_px = 55.0;
        }
        value.target_velocity_x_px_per_sec = value.direction_x * (205.0 + index * 18.0);
        value.target_velocity_y_px_per_sec = value.direction_y * (105.0 + index * 15.0);
        value.left_stick_x = value.direction_x * (index % 2 == 0 ? -0.42 : 0.42);
        result.cases.push_back(value);
    }
    return result;
}

ManualProfileSample sample_manual_profile(
    const ScenarioCase& value,
    ManualProfileSample historical,
    ManualProfileSample ideal,
    int elapsed_in_error_ms) noexcept {
    if (value.error_kind == HumanErrorKind::None || value.error_hold_ms <= 0) {
        return ideal;
    }
    ManualProfileSample onset = ideal;
    const auto wrong_way = [&](double axis) {
        const double magnitude = value.stress_tier == StressTier::None
            ? std::min(value.manual_magnitude_cap, std::max(0.28, std::fabs(axis)))
            : value.manual_magnitude_cap;
        return axis == 0.0 ? 0.0 : -std::copysign(magnitude, axis);
    };
    switch (value.error_kind) {
        case HumanErrorKind::None:
            break;
        case HumanErrorKind::StaleDirection:
        case HumanErrorKind::CrossingInertia:
            onset = historical;
            break;
        case HumanErrorKind::WrongX:
            onset.x = wrong_way(ideal.x);
            break;
        case HumanErrorKind::WrongY:
            onset.y = wrong_way(ideal.y);
            break;
        case HumanErrorKind::WrongBoth:
            onset.x = wrong_way(ideal.x);
            onset.y = wrong_way(ideal.y);
            break;
        case HumanErrorKind::MixedAxes:
            onset.x = wrong_way(ideal.x);
            break;
    }
    const double linear = std::clamp(
        static_cast<double>(elapsed_in_error_ms) / value.error_hold_ms,
        0.0,
        1.0);
    const double blend = linear * linear * (3.0 - 2.0 * linear);
    ManualProfileSample result{
        onset.x + (ideal.x - onset.x) * blend,
        onset.y + (ideal.y - onset.y) * blend,
    };
    if (value.error_kind == HumanErrorKind::MixedAxes && linear >= 0.5) {
        const double local = (linear - 0.5) * 2.0;
        const double pulse = std::pow(std::sin(3.14159265358979323846 * local), 2.0);
        const double wrong_y = wrong_way(ideal.y);
        result.y = ideal.y + (wrong_y - ideal.y) * pulse;
    }
    return result;
}

DisturbanceSample sample_truth_velocity_disturbance(
    const ScenarioCase& value,
    std::uint32_t seed,
    int elapsed_ms) noexcept {
    if (value.stress_tier == StressTier::None) return {};
    const double t = std::max(0, elapsed_ms) * 0.001;
    const double phase = std::fmod(
        static_cast<double>(seed) * 0.0137 + value.index * 0.731,
        2.0 * kPi);
    DisturbanceSample result{
        value.truth_motion_amplitude_x_px_per_sec *
            (0.62 * std::sin(2.0 * kPi * 2.7 * t + phase) +
             0.38 * std::sin(2.0 * kPi * 6.1 * t + phase * 1.7)),
        value.truth_motion_amplitude_y_px_per_sec *
            (0.68 * std::sin(2.0 * kPi * 2.1 * t + phase * 0.8) +
             0.32 * std::sin(2.0 * kPi * 5.3 * t + phase * 1.3)),
    };

    const int reversal_period = value.stress_tier == StressTier::Destructive ? 150 : 230;
    const int reversal_phase = static_cast<int>((seed + value.index * 37u) % reversal_period);
    const int local = (std::max(0, elapsed_ms) + reversal_phase) % reversal_period;
    const int reversal_width = value.stress_tier == StressTier::Destructive ? 42 : 26;
    if (local < reversal_width) {
        const double envelope = std::sin(kPi * local / std::max(1, reversal_width));
        result.x -= value.direction_x * value.truth_motion_amplitude_x_px_per_sec * envelope;
        result.y += value.direction_y * value.truth_motion_amplitude_y_px_per_sec *
            envelope * 0.65;
    }
    return result;
}

DisturbanceSample sample_observation_disturbance(
    const ScenarioCase& value,
    std::uint32_t seed,
    int elapsed_ms) noexcept {
    if (value.stress_tier == StressTier::None) return {};
    const double t = std::max(0, elapsed_ms) * 0.001;
    const double phase = std::fmod(
        static_cast<double>(seed) * 0.0091 + value.index * 0.613,
        2.0 * kPi);
    DisturbanceSample result{
        value.observation_jitter_x_px *
            (0.68 * std::sin(2.0 * kPi * 11.0 * t + phase) +
             0.32 * std::sin(2.0 * kPi * 23.0 * t + phase * 1.4)),
        value.observation_jitter_y_px *
            (0.70 * std::sin(2.0 * kPi * 13.0 * t + phase * 0.9) +
             0.30 * std::sin(2.0 * kPi * 29.0 * t + phase * 1.6)),
    };

    const int jump_period = value.stress_tier == StressTier::Destructive ? 140 : 210;
    const int offset = static_cast<int>((seed + value.index * 43u) % jump_period);
    const int local = (std::max(0, elapsed_ms) + offset) % jump_period;
    const int center = jump_period / 2;
    const int half_width = value.stress_tier == StressTier::Destructive ? 10 : 7;
    const double pulse = std::max(
        0.0,
        1.0 - static_cast<double>(std::abs(local - center)) / half_width);
    result.x += value.direction_x * value.observation_jump_px * 0.8 * pulse;
    result.y -= value.direction_y * value.observation_jump_px * 0.6 * pulse;

    const double magnitude = std::hypot(result.x, result.y);
    if (magnitude > value.observation_jump_px && magnitude > 0.0) {
        const double scale = value.observation_jump_px / magnitude;
        result.x *= scale;
        result.y *= scale;
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
        out << "    {\n"
            << "      \"name\": \"" << escape_json(report.name) << "\",\n"
            << "      \"metrics\": ";
        write_metrics_json(out, report.metrics, "      ");
        out << ",\n      \"score\": ";
        write_score_json(out, report.score, "      ");
        out << ",\n      \"cases\": [\n";
        for (std::size_t case_index = 0; case_index < report.cases.size(); ++case_index) {
            const auto& case_report = report.cases[case_index];
            out << "        {\n"
                << "          \"name\": \"" << escape_json(case_report.name) << "\",\n"
                << "          \"metrics\": ";
            write_metrics_json(out, case_report.metrics, "          ");
            out << ",\n          \"score\": ";
            write_score_json(out, case_report.score, "          ");
            out << "\n        }" << (case_index + 1 < report.cases.size() ? "," : "") << "\n";
        }
        out << "      ]\n"
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
        case HumanErrorKind::WrongBoth: return "wrong_both";
        case HumanErrorKind::MixedAxes: return "mixed_axes";
    }
    return "unknown";
}

}  // namespace controller_native::partial_occlusion
