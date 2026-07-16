#include "partial_occlusion_benchmark.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>

namespace {

using controller_native::partial_occlusion::HumanErrorKind;
using controller_native::partial_occlusion::ManualProfileSample;
using controller_native::partial_occlusion::ScenarioKind;

void expect_true(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " actual=" << actual
                  << " expected=" << expected << "\n";
        std::exit(1);
    }
}

void test_combat_schedule_uses_log_derived_occlusion_envelope() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::Combat,
        1337);

    expect_true(scenario.name == "partial_occlusion_combat", "combat scenario name");
    expect_true(scenario.cases.size() == 4, "combat should contain four diagonal cases");

    const std::set<int> expected_gaps{30, 60, 110, 160};
    std::set<int> actual_gaps;
    std::set<std::pair<int, int>> directions;
    for (const auto& value : scenario.cases) {
        actual_gaps.insert(value.observation_gap_ms);
        directions.insert({value.direction_x, value.direction_y});
        expect_true(value.error_kind == HumanErrorKind::None, "combat must not inject classic errors");
        expect_true(value.partial_body_height_px < value.full_body_height_px, "partial body must be clipped");
        expect_near(value.truth_aim_height_ratio, 0.365, 0.0001, "truth must use canonical chest ratio");
    }
    expect_true(actual_gaps == expected_gaps, "combat gap envelope");
    expect_true(directions.size() == 4, "combat should cover four diagonal directions");
}

void test_human_error_schedule_covers_all_classic_errors() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::HumanErrors,
        1337);

    expect_true(
        scenario.name == "partial_occlusion_human_errors",
        "human-error scenario name");
    std::set<HumanErrorKind> errors;
    for (const auto& value : scenario.cases) errors.insert(value.error_kind);
    expect_true(errors.count(HumanErrorKind::StaleDirection) == 1, "stale direction case");
    expect_true(errors.count(HumanErrorKind::WrongX) == 1, "wrong X case");
    expect_true(errors.count(HumanErrorKind::WrongY) == 1, "wrong Y case");
    expect_true(errors.count(HumanErrorKind::CrossingInertia) == 1, "crossing inertia case");
    expect_true(scenario.cases[1].error_onset_ms < scenario.cases[1].full_observed_ms,
                "wrong-X must be exercised while vision is stable");
    expect_true(scenario.cases[2].error_onset_ms < scenario.cases[2].full_observed_ms,
                "wrong-Y must be exercised while vision is stable");
    expect_true(scenario.cases[0].error_onset_ms < 0 && scenario.cases[3].error_onset_ms < 0,
                "stale/crossing errors must remain tied to occlusion");
    expect_true(scenario.cases[1].manual_magnitude_cap < 0.45 &&
                scenario.cases[2].manual_magnitude_cap < 0.45,
                "wrong-axis mistakes must stay below explicit escape authority");
    expect_true(scenario.cases[0].manual_magnitude_cap > 0.45 &&
                scenario.cases[3].manual_magnitude_cap > 0.45,
                "stale/crossing cases must retain strong takeover coverage");
}

void test_stale_direction_holds_history_before_decaying_to_ideal() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::HumanErrors,
        1337);
    const auto& value = scenario.cases[0];
    const ManualProfileSample historical{0.42, -0.18};
    const ManualProfileSample ideal{-0.20, 0.30};
    const auto onset = controller_native::partial_occlusion::sample_manual_profile(
        value, historical, ideal, 0);
    const auto recovered = controller_native::partial_occlusion::sample_manual_profile(
        value, historical, ideal, value.error_hold_ms);

    expect_near(onset.x, historical.x, 0.0001, "stale X must hold historical input");
    expect_near(onset.y, historical.y, 0.0001, "stale Y must hold historical input");
    expect_near(recovered.x, ideal.x, 0.0001, "stale X must decay to ideal");
    expect_near(recovered.y, ideal.y, 0.0001, "stale Y must decay to ideal");
}

void test_wrong_axis_profiles_preserve_the_unaffected_axis() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::HumanErrors,
        1337);
    const ManualProfileSample historical{0.35, 0.25};
    const ManualProfileSample ideal{0.20, -0.30};
    const auto wrong_x = controller_native::partial_occlusion::sample_manual_profile(
        scenario.cases[1], historical, ideal, 0);
    const auto wrong_y = controller_native::partial_occlusion::sample_manual_profile(
        scenario.cases[2], historical, ideal, 0);

    expect_true(wrong_x.x * ideal.x < 0.0, "wrong-X must reverse only X");
    expect_true(std::fabs(wrong_x.x) >= 0.28, "wrong-X must cover observed P25 magnitude");
    expect_near(wrong_x.y, ideal.y, 0.0001, "wrong-X must preserve Y");
    expect_near(wrong_y.x, ideal.x, 0.0001, "wrong-Y must preserve X");
    expect_true(wrong_y.y * ideal.y < 0.0, "wrong-Y must reverse only Y");
    expect_true(std::fabs(wrong_y.y) >= 0.28, "wrong-Y must cover observed P25 magnitude");
}

void test_crossing_inertia_releases_smoothly() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::HumanErrors,
        1337);
    const auto& value = scenario.cases[3];
    const ManualProfileSample historical{0.55, -0.25};
    const ManualProfileSample ideal{-0.20, 0.15};
    const auto onset = controller_native::partial_occlusion::sample_manual_profile(
        value, historical, ideal, 0);
    const auto middle = controller_native::partial_occlusion::sample_manual_profile(
        value, historical, ideal, value.error_hold_ms / 2);
    const auto recovered = controller_native::partial_occlusion::sample_manual_profile(
        value, historical, ideal, value.error_hold_ms);

    expect_near(onset.x, historical.x, 0.0001, "crossing must start from historical X");
    expect_true(middle.x < onset.x && middle.x > recovered.x,
                "crossing X must decay continuously");
    expect_near(recovered.x, ideal.x, 0.0001, "crossing X must recover to ideal");
    expect_near(recovered.y, ideal.y, 0.0001, "crossing Y must recover to ideal");
}

void test_clipped_observation_does_not_move_world_truth() {
    const auto scenario = controller_native::partial_occlusion::build_scenario(
        ScenarioKind::Combat,
        1337);
    const auto& value = scenario.cases.front();
    const double full_truth = controller_native::partial_occlusion::truth_chest_y_px(
        value.full_body_top_y_px,
        value.full_body_height_px,
        value.truth_aim_height_ratio);
    const double partial_truth = controller_native::partial_occlusion::truth_chest_y_px(
        value.full_body_top_y_px,
        value.full_body_height_px,
        value.truth_aim_height_ratio);
    const double observed_partial = controller_native::partial_occlusion::observed_chest_y_px(
        value.full_body_top_y_px,
        value.partial_body_height_px,
        value.truth_aim_height_ratio);

    expect_near(full_truth, partial_truth, 0.0001, "truth must retain full body geometry");
    expect_true(std::fabs(observed_partial - full_truth) > 10.0, "clipped observation should expose geometry bias");
}

void test_perfect_metrics_score_one_hundred() {
    controller_native::partial_occlusion::PartialOcclusionMetrics metrics;
    metrics.mean_error_px = 0.0;
    metrics.p95_error_px = 0.0;
    metrics.max_overshoot_x_px = 0.0;
    metrics.max_overshoot_y_px = 0.0;
    metrics.mean_recovery_ms = 0.0;
    metrics.p95_output_delta = 0.0;

    const auto score = controller_native::partial_occlusion::score_metrics(metrics);
    expect_near(score.tracking, 100.0, 0.0001, "perfect tracking score");
    expect_near(score.overshoot, 100.0, 0.0001, "perfect overshoot score");
    expect_near(score.recovery, 100.0, 0.0001, "perfect recovery score");
    expect_near(score.smoothness, 100.0, 0.0001, "perfect smoothness score");
    expect_near(score.intent, 100.0, 0.0001, "perfect intent score");
    expect_near(score.overall, 100.0, 0.0001, "perfect overall score");
}

void test_degraded_metrics_reduce_every_component() {
    controller_native::partial_occlusion::PartialOcclusionMetrics metrics;
    metrics.mean_error_px = 42.0;
    metrics.p95_error_px = 60.0;
    metrics.max_overshoot_x_px = 30.0;
    metrics.max_overshoot_y_px = 24.0;
    metrics.mean_recovery_ms = 300.0;
    metrics.p95_output_delta = 0.20;
    metrics.output_spikes = 4;
    metrics.correct_manual_opposition_frames = 30;
    metrics.wrong_manual_high_force_frames = 40;
    metrics.measured_frames = 100;

    const auto score = controller_native::partial_occlusion::score_metrics(metrics);
    expect_true(score.tracking < 100.0, "error should reduce tracking");
    expect_true(score.overshoot < 100.0, "crossing should reduce overshoot");
    expect_true(score.recovery < 100.0, "slow recovery should reduce recovery");
    expect_true(score.smoothness < 100.0, "delta and spikes should reduce smoothness");
    expect_true(score.intent < 100.0, "manual fight should reduce intent");
    expect_true(score.overall < 70.0, "combined defects should materially reduce overall score");
}

void test_json_report_contains_provenance_metrics_and_scores() {
    controller_native::partial_occlusion::BenchmarkMetadata metadata;
    metadata.seed = 1337;
    metadata.config_path = "config.toml";
    metadata.aim_height_ratio = 0.365;
    metadata.ads_range_px = 150.0;
    metadata.ads_snap_duration_ms = 160;
    metadata.bodylock_strength = 0.45;
    metadata.bodylock_activation_range_px = 120.0;
    metadata.bodylock_tolerance_px = 16.0;

    controller_native::partial_occlusion::ScenarioReport report;
    report.name = "partial_occlusion_combat";
    report.metrics.measured_frames = 100;
    report.metrics.mean_error_px = 12.5;
    report.score = controller_native::partial_occlusion::score_metrics(report.metrics);
    controller_native::partial_occlusion::CaseReport case_report;
    case_report.name = "wrong_x";
    case_report.metrics.mean_error_px = 18.0;
    case_report.score = controller_native::partial_occlusion::score_metrics(case_report.metrics);
    report.cases.push_back(case_report);

    const std::string json = controller_native::partial_occlusion::render_report_json(
        metadata,
        {report});
    expect_true(json.find("\"seed\": 1337") != std::string::npos, "JSON seed provenance");
    expect_true(json.find("\"aim_height_ratio\": 0.365000") != std::string::npos, "JSON geometry config");
    expect_true(json.find("\"mean_error_px\": 12.500000") != std::string::npos, "JSON raw metric");
    expect_true(json.find("\"formula_version\": 1") != std::string::npos, "JSON score formula");
    expect_true(json.find("\"overall\"") != std::string::npos, "JSON overall score");
    expect_true(json.find("\"cases\"") != std::string::npos, "JSON case array");
    expect_true(json.find("\"wrong_x\"") != std::string::npos, "JSON case name");
}

}  // namespace

int main() {
    test_combat_schedule_uses_log_derived_occlusion_envelope();
    test_human_error_schedule_covers_all_classic_errors();
    test_stale_direction_holds_history_before_decaying_to_ideal();
    test_wrong_axis_profiles_preserve_the_unaffected_axis();
    test_crossing_inertia_releases_smoothly();
    test_clipped_observation_does_not_move_world_truth();
    test_perfect_metrics_score_one_hundred();
    test_degraded_metrics_reduce_every_component();
    test_json_report_contains_provenance_metrics_and_scores();
    std::cout << "Partial occlusion benchmark tests PASS\n";
    return 0;
}
