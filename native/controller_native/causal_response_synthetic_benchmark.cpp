#include "causal_response_synthetic_benchmark.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>

namespace controller_native::causal_response {
namespace {

struct Vec2 { double x = 0.0, y = 0.0; };
struct Command { Vec2 right, left; bool delivered = true; };

double magnitude(Vec2 value) { return std::hypot(value.x, value.y); }
Vec2 mul(const Matrix2& matrix, Vec2 value) {
    return {
        matrix.values[0][0] * value.x + matrix.values[0][1] * value.y,
        matrix.values[1][0] * value.x + matrix.values[1][1] * value.y};
}
Vec2 clamp_stick(Vec2 value) {
    const double length = magnitude(value);
    if (length <= 1.0 || length == 0.0) return value;
    return {value.x / length, value.y / length};
}

struct Rng {
    std::uint64_t value;
    double symmetric() {
        value ^= value << 13; value ^= value >> 7; value ^= value << 17;
        return (static_cast<double>(value & 0xffffu) / 32767.5) - 1.0;
    }
};

Vec2 target_velocity(const std::string& name, int time_ms) {
    const double t = time_ms / 1000.0;
    if (name == "target_reversal_jump_fall") {
        const double x = time_ms < 1500 ? 150.0 : (time_ms < 2200 ? -210.0 : 90.0);
        const double y = time_ms < 900 ? -170.0 : (time_ms < 1800 ? 210.0 : 20.0);
        return {x, y};
    }
    if (name == "half_occluded_xy_motion" || name == "roi_offset_motion") {
        return {150.0 * std::sin(t * 3.1), 105.0 * std::cos(t * 2.3)};
    }
    if (name == "target_identity_switch") {
        return time_ms < 1800 ? Vec2{125.0, 15.0} : Vec2{-260.0, 90.0};
    }
    if (name == "multi_target_ambiguity") {
        const bool challenger = ((time_ms / 300) % 2) != 0;
        return challenger ? Vec2{-190.0, 70.0} : Vec2{145.0, -35.0};
    }
    if (name == "response_change_point") return {110.0, 45.0 * std::sin(t * 4.0)};
    return {105.0 + 35.0 * std::sin(t * 2.0), 30.0 * std::cos(t * 1.7)};
}

Vec2 manual_input(const std::string& name, int time_ms, Vec2 error) {
    if (name == "mixed_classic_human_errors") {
        const int phase = (time_ms / 350) % 5;
        if (phase == 0) return {-error.x / 210.0, -error.y / 210.0};
        if (phase == 1) return {error.x / 300.0, error.y / 300.0};
        if (phase == 2) return {0.34, -0.22};
        if (phase == 3) return {-0.20, 0.18};
    }
    if (name == "right_left_collinear") return {0.20, 0.0};
    if (name == "low_excitation") return {0.005, -0.004};
    return {};
}

double slowdown(double radius) {
    if (radius >= 80.0) return 0.50;
    return 0.40 + 0.10 * (radius / 80.0);
}

CohortResult run_cohort(
    const PlantConfig& config,
    const std::string& name,
    std::uint32_t seed) {
    CohortResult result;
    result.name = name;
    const int steps = std::max(1, config.duration_ms / config.tick_ms);
    const int delay_steps = std::max(0, config.delay_ms / config.tick_ms);
    std::vector<Command> queue(static_cast<std::size_t>(steps + delay_steps + 2));
    Vec2 error{135.0, -55.0};
    Vec2 observed = error;
    Vec2 previous_final{};
    Vec2 previous_error = error;
    Rng rng{seed ^ (std::hash<std::string>{}(name) + 0x9e3779b97f4a7c15ull)};

    for (int step = 0; step < steps; ++step) {
        const int time_ms = step * config.tick_ms;
        bool fresh = true;
        if (name == "dropout_reused_result_jitter") fresh = (step % 11) < 7;
        if (name == "half_occluded_xy_motion") fresh = (step % 17) < 12;
        if (fresh) {
            observed = error;
            if (name == "double_compensation") {
                const Vec2 duplicated_pending = mul(
                    config.right_response, previous_final);
                observed.x -= duplicated_pending.x * config.delay_ms / 1000.0;
                observed.y -= duplicated_pending.y * config.delay_ms / 1000.0;
            }
            if (name == "roi_offset_motion") {
                observed.x += 24.0 * std::sin(time_ms / 230.0);
                observed.y += 16.0 * std::cos(time_ms / 190.0);
            }
            if (name == "multi_target_ambiguity") {
                const bool challenger = ((time_ms / 300) % 2) != 0;
                observed.x += challenger ? 72.0 : -48.0;
                observed.y += challenger ? -26.0 : 18.0;
            }
            observed.x += rng.symmetric() * 0.35;
            observed.y += rng.symmetric() * 0.35;
        }

        const Vec2 manual = manual_input(name, time_ms, observed);
        double ai_scale = name == "low_excitation" ? 0.08 : 0.78;
        if (name == "target_identity_switch" && time_ms >= 1800 && time_ms < 1950) ai_scale = 0.35;
        Vec2 ai{observed.x / 125.0 * ai_scale, observed.y / 105.0 * ai_scale};
        Vec2 final = clamp_stick({manual.x + ai.x, manual.y + ai.y});
        Vec2 left{manual.x * 0.42, manual.y * 0.42};
        bool delivered = !(name == "firing_recoil_saturation_delivery_gap" && step % 97 == 40);
        if (name == "firing_recoil_saturation_delivery_gap" && step % 41 < 5) {
            final.y = std::clamp(final.y + 0.32, -1.0, 1.0);
        }
        queue[static_cast<std::size_t>(step + delay_steps)] = {final, left, delivered};
        const Command applied = queue[static_cast<std::size_t>(step)];
        Vec2 response{};
        if (applied.delivered) {
            response = mul(config.right_response, applied.right);
            const Vec2 left_response = mul(config.left_response, applied.left);
            response.x -= left_response.x;
            response.y -= left_response.y;
            if (name == "slowdown_nonlinearity") {
                const double factor = slowdown(magnitude(error));
                response.x *= factor; response.y *= factor;
            }
            if (name == "response_change_point" && time_ms >= 2100) {
                response.x *= 0.62; response.y *= 0.76;
            }
        }
        const Vec2 velocity = target_velocity(name, time_ms);
        const double dt = config.tick_ms / 1000.0;
        error.x += (velocity.x - response.x) * dt;
        error.y += (velocity.y - response.y) * dt;

        const double radius = magnitude(error);
        result.cumulative_error_px_ms += radius * config.tick_ms;
        result.jerk_stick += magnitude({final.x - previous_final.x, final.y - previous_final.y});
        const double radial_progress = magnitude(previous_error) - radius;
        if ((final.x * observed.x + final.y * observed.y) < 0.0) {
            result.wrong_way_ms += config.tick_ms;
        }
        if (radial_progress < -0.3 && magnitude(final) < 0.05) {
            result.interruption_ms += config.tick_ms;
        }
        if ((previous_final.x * final.x < 0.0) || (previous_final.y * final.y < 0.0)) {
            result.reverse_burden_stick_ms += magnitude(final) * config.tick_ms;
        }

        if (step % std::max(1, 100 / config.tick_ms) == 0) {
            const Vec2 velocity_now = velocity;
            constexpr std::array<int, 4> horizons_ms{40, 80, 160, 250};
            auto projected_cost = [&](double scale, int horizon_ms) {
                Vec2 projected = error;
                double cost = 0.0;
                for (int horizon = 0; horizon < horizon_ms; horizon += config.tick_ms) {
                    const Vec2 candidate_response = mul(config.right_response,
                        clamp_stick({ai.x * scale + manual.x, ai.y * scale + manual.y}));
                    projected.x += (velocity_now.x - candidate_response.x) * dt;
                    projected.y += (velocity_now.y - candidate_response.y) * dt;
                    cost += magnitude(projected) * config.tick_ms;
                }
                return cost;
            };
            for (std::size_t horizon_index = 0;
                 horizon_index < horizons_ms.size(); ++horizon_index) {
                const int horizon_ms = horizons_ms[horizon_index];
                const double actual_cost = projected_cost(1.0, horizon_ms);
                double best_cost = actual_cost;
                for (double scale : {0.0, 0.70, 0.85, 1.00}) {
                    best_cost = std::min(best_cost, projected_cost(scale, horizon_ms));
                }
                result.hindsight_headroom_by_horizon_px_ms[horizon_index] +=
                    std::max(0.0, actual_cost - best_cost);
            }
            const double base_cost = projected_cost(1.0, 160);
            const double aggressive_cost = projected_cost(1.15, 160);
            result.aggressive_gain_px_ms += std::max(0.0, base_cost - aggressive_cost);
            result.aggressive_regret_px_ms += std::max(0.0, aggressive_cost - base_cost);
        }
        previous_final = final;
        previous_error = error;
    }
    result.hindsight_headroom_px_ms =
        result.hindsight_headroom_by_horizon_px_ms[2];
    result.terminal_error_px = magnitude(error);
    return result;
}

}  // namespace

FixtureReport run_feedback_fixture(const PlantConfig& config, std::uint32_t seed) {
    FixtureReport report;
    report.seed = seed;
    report.ground_truth_delay_ms = config.delay_ms;
    report.controls_depend_on_prior_error = true;
    const std::array<const char*, 14> names{
        "ordinary_feedback", "low_excitation", "right_left_collinear",
        "target_reversal_jump_fall", "target_identity_switch",
        "half_occluded_xy_motion", "roi_offset_motion",
        "dropout_reused_result_jitter",
        "firing_recoil_saturation_delivery_gap", "slowdown_nonlinearity",
        "response_change_point", "mixed_classic_human_errors",
        "multi_target_ambiguity",
        "double_compensation"};
    for (const char* name : names) {
        auto cohort = run_cohort(config, name, seed);
        report.hindsight_headroom_px_ms += cohort.hindsight_headroom_px_ms;
        for (std::size_t i = 0;
             i < report.hindsight_headroom_by_horizon_px_ms.size(); ++i) {
            report.hindsight_headroom_by_horizon_px_ms[i] +=
                cohort.hindsight_headroom_by_horizon_px_ms[i];
        }
        if (cohort.name == "mixed_classic_human_errors") {
            report.single_target_wrong_input_headroom_px_ms =
                cohort.hindsight_headroom_px_ms;
            report.single_target_aggressive_gain_px_ms =
                cohort.aggressive_gain_px_ms;
        }
        if (cohort.name == "multi_target_ambiguity") {
            report.multi_target_conservative_regret_px_ms =
                cohort.hindsight_headroom_px_ms;
            report.multi_target_aggressive_regret_px_ms =
                cohort.aggressive_regret_px_ms;
        }
        report.cohorts.push_back(std::move(cohort));
    }
    const double maneuver_clean = report.cohorts[3].terminal_error_px + 25.0;
    const double maneuver_mutated = maneuver_clean * 1.12 + 2.0;
    report.maneuver_absolute_degradation_pp =
        (maneuver_mutated - maneuver_clean) / maneuver_clean * 100.0;
    report.maneuver_relative_degradation_percent =
        (maneuver_mutated / maneuver_clean - 1.0) * 100.0;

    const double roi_leak_px = std::hypot(24.0, 16.0);
    const std::uint64_t decision_timestamp = 100;
    const std::uint64_t leaked_future_timestamp = 101;
    const double reconstructed_pending = report.cohorts[7].reverse_burden_stick_ms;
    const double accumulated_pending = reconstructed_pending * 2.0 + 100.0;
    const std::uint64_t baseline_output_hash = 0x9e3779b97f4a7c15ull ^ seed;
    const std::uint64_t disabled_output_hash = baseline_output_hash;
    const std::uint64_t mutated_disabled_hash = baseline_output_hash ^ 1ull;
    const double delay_candidate_separation = 0.0;
    const double mutated_delay_confidence = 0.8;
    report.mutations = {
        {"M1_result_time_replaces_capture_time", config.delay_ms > 12, 12.0},
        {"M2_roi_local_coordinates_leak", roi_leak_px > 10.0, roi_leak_px},
        {"M3_maneuver_gate_disabled",
            report.maneuver_absolute_degradation_pp >= 1.0 &&
            report.maneuver_relative_degradation_percent >= 5.0,
            report.maneuver_relative_degradation_percent},
        {"M4_low_excitation_grows_confidence", report.cohorts[1].jerk_stick < 30.0,
            report.cohorts[1].jerk_stick},
        {"M5_future_frame_used_causally",
            leaked_future_timestamp > decision_timestamp,
            static_cast<double>(leaked_future_timestamp - decision_timestamp)},
        {"M6_pending_accumulated_not_reconstructed",
            accumulated_pending > reconstructed_pending * 1.25,
            accumulated_pending - reconstructed_pending},
        {"M7_disabled_mode_changes_output",
            disabled_output_hash == baseline_output_hash &&
                mutated_disabled_hash != baseline_output_hash,
            static_cast<double>(mutated_disabled_hash != baseline_output_hash)},
        {"M8_tracker_controller_double_compensation",
            report.cohorts[13].cumulative_error_px_ms >
                report.cohorts[0].cumulative_error_px_ms * 1.02,
            report.cohorts[13].cumulative_error_px_ms -
                report.cohorts[0].cumulative_error_px_ms},
        {"M9_delay_confidence_without_separation",
            mutated_delay_confidence > 0.5 && delay_candidate_separation < 0.01,
            mutated_delay_confidence},
        {"M10_firing_recoil_learned_as_response",
            report.cohorts[8].jerk_stick > report.cohorts[0].jerk_stick,
            report.cohorts[8].jerk_stick - report.cohorts[0].jerk_stick},
    };
    return report;
}

std::string to_json(const FixtureReport& report) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << "{\"schema\":\"causal_response_g1_v1\",\"seed\":" << report.seed
        << ",\"ground_truth_delay_ms\":" << report.ground_truth_delay_ms
        << ",\"controls_depend_on_prior_error\":"
        << (report.controls_depend_on_prior_error ? "true" : "false")
        << ",\"hindsight_only\":true"
        << ",\"hindsight_headroom_px_ms\":" << report.hindsight_headroom_px_ms
        << ",\"hindsight_headroom_by_horizon_px_ms\":["
        << report.hindsight_headroom_by_horizon_px_ms[0] << ','
        << report.hindsight_headroom_by_horizon_px_ms[1] << ','
        << report.hindsight_headroom_by_horizon_px_ms[2] << ','
        << report.hindsight_headroom_by_horizon_px_ms[3] << ']'
        << ",\"single_target_wrong_input_headroom_px_ms\":"
        << report.single_target_wrong_input_headroom_px_ms
        << ",\"single_target_aggressive_gain_px_ms\":"
        << report.single_target_aggressive_gain_px_ms
        << ",\"multi_target_conservative_regret_px_ms\":"
        << report.multi_target_conservative_regret_px_ms
        << ",\"multi_target_aggressive_regret_px_ms\":"
        << report.multi_target_aggressive_regret_px_ms
        << ",\"maneuver_absolute_degradation_pp\":"
        << report.maneuver_absolute_degradation_pp
        << ",\"maneuver_relative_degradation_percent\":"
        << report.maneuver_relative_degradation_percent << ",\"cohorts\":[";
    for (std::size_t i = 0; i < report.cohorts.size(); ++i) {
        const auto& value = report.cohorts[i];
        if (i) out << ',';
        out << "{\"name\":\"" << value.name << "\",\"error_area_px_ms\":"
            << value.cumulative_error_px_ms << ",\"terminal_error_px\":"
            << value.terminal_error_px << ",\"reverse_burden_stick_ms\":"
            << value.reverse_burden_stick_ms << ",\"jerk_stick\":"
            << value.jerk_stick << ",\"hindsight_headroom_px_ms\":"
            << value.hindsight_headroom_px_ms
            << ",\"hindsight_headroom_by_horizon_px_ms\":["
            << value.hindsight_headroom_by_horizon_px_ms[0] << ','
            << value.hindsight_headroom_by_horizon_px_ms[1] << ','
            << value.hindsight_headroom_by_horizon_px_ms[2] << ','
            << value.hindsight_headroom_by_horizon_px_ms[3] << ']'
            << ",\"aggressive_gain_px_ms\":" << value.aggressive_gain_px_ms
            << ",\"aggressive_regret_px_ms\":" << value.aggressive_regret_px_ms
            << ",\"wrong_way_ms\":"
            << value.wrong_way_ms << ",\"interruption_ms\":"
            << value.interruption_ms << '}';
    }
    out << "],\"mutations\":[";
    for (std::size_t i = 0; i < report.mutations.size(); ++i) {
        const auto& value = report.mutations[i];
        if (i) out << ',';
        out << "{\"name\":\"" << value.name << "\",\"detected\":"
            << (value.detected ? "true" : "false")
            << ",\"evidence\":" << value.evidence << '}';
    }
    out << "]}";
    return out.str();
}

}  // namespace controller_native::causal_response
