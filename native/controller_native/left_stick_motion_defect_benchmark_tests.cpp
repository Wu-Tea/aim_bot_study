#include "left_stick_motion_defect_benchmark.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <set>
#include <string>

namespace {

void require_true(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "[NativeLeftStickMotionBenchmarkTests] FAIL: " << message << '\n';
    std::exit(1);
}

const controller_native::left_stick_defect::BenchmarkReport& benchmark_report() {
    static const auto report = controller_native::left_stick_defect::run_benchmark();
    return report;
}

void test_live_frequency_matrix_contract() {
    const auto& report = benchmark_report();
    require_true(report.schema_version == 3, "live-rate JSON contract must use schema 3");
    require_true(report.controller_hz == 1000, "controller fixture must run at 1000 Hz");
    std::set<int> primary_rates;
    std::set<int> stress_rates;
    for (const auto& run : report.frequency_runs) {
        require_true(
            run.delivered_vision_sequences > 0 &&
                run.fresh_sequences_consumed == run.delivered_vision_sequences,
            "every delivered vision sequence must be consumed exactly once");
        (run.primary_rate ? primary_rates : stress_rates).insert(run.vision_hz);
        if (run.primary_rate) {
            require_true(
                run.interframe_left_transition_frames > 0,
                "primary rates must exercise left-input changes between vision frames");
            require_true(
                run.max_interframe_left_delta > 0.0,
                "primary rates must measure a non-zero inter-frame left transition");
        }
    }
    require_true(
        primary_rates == std::set<int>({80, 100}),
        "primary matrix must cover 80 Hz and 100 Hz vision");
    require_true(
        stress_rates == std::set<int>({50, 160}),
        "stress matrix must cover 50 Hz and 160 Hz vision");
}

void test_desired_acceptance_gates_are_closed() {
    const auto& report = benchmark_report();
    require_true(report.intent_invariance.desired_gate_pass, "left intent must affect AI planning");
    const bool relative_quality =
        report.primary.fast_mean_improvement_ratio >= 0.20 &&
        report.primary.fast_p95_improvement_ratio >= 0.20 &&
        report.primary.same_direction_regression_ratio <= 0.05;
    const bool absolute_quality =
        report.primary.max_fast_mean_error_px <= 4.0 &&
        report.primary.max_fast_p95_error_px <= 10.0 &&
        report.primary.max_same_direction_mean_error_px <= 5.0;
    require_true(
        relative_quality || absolute_quality,
        "primary strafe tracking must pass relative or strict absolute quality");
    require_true(
        report.primary.max_lifecycle_ai_delta <= 0.07,
        "BodyLock lifecycle AI delta must remain inside the envelope");
    require_true(
        report.primary.large_sign_flip_count == 0,
        "BodyLock must not produce large direct sign flips");
    require_true(
        report.production_chain.short_gap_coast_pass,
        "short same-track gap must preserve a bounded existing-assist release");
    require_true(report.production_chain.long_loss_release_pass, "long identity loss must release");
    require_true(report.production_chain.reacquire_bumpless_pass, "reacquire must be bumpless");
    require_true(
        report.production_chain.no_blind_candidate_follow_pass,
        "candidate-only gaps must not create blind assist");
    require_true(
        report.ads_handoff.max_transition_overshoot_px <= 2.0,
        "ADS handoff overshoot must remain within 2 px");
    require_true(report.desired_gate_pass, "the fixed benchmark gate must close");
}

void test_production_chain_behavior_is_populated() {
    const auto& report = benchmark_report();
    const auto& chain = report.production_chain;
    require_true(chain.behavior_populated, "production-chain behavior must run");
    require_true(
        chain.drift_manual_correction_frames == 0,
        "deadzone-sized right-stick noise must not count as correction");
    require_true(
        chain.detector_candidate_gap_frames > 0,
        "candidate-present selected-target gap must be exercised");
    require_true(
        chain.production_target_missing_frames > 0,
        "production target must become unavailable during the gap");
    require_true(
        chain.reacquire_latency_ms >= 0.0,
        "reacquisition latency must be populated");
    require_true(
        chain.reacquire_useful_latency_ms >= 0.0 &&
            chain.reacquire_useful_latency_ms <= 60.0,
        "reacquisition must restore useful assist within 60 ms");
    require_true(
        chain.reacquire_max_output_delta <= 0.07,
        "reacquisition must remain inside the sole delivery envelope");
    require_true(
        chain.body_lock_frames > 0 && chain.ads_snap_frames > 0 &&
            chain.manual_frames > 0,
        "body-lock, ADS-snap, and manual phases must all occur");
    require_true(
        chain.selected_track_changes > 0,
        "selected target must rebind after the evidence-matched gap");
}

void test_warm_left_intent_changes_ai_trace() {
    const auto& report = benchmark_report();
    if (report.intent_invariance.max_ai_trace_delta <= 1.0e-4) {
        std::cerr << "[NativeLeftStickMotionBenchmarkTests] intent delta="
                  << report.intent_invariance.max_ai_trace_delta
                  << " final_delta="
                  << report.intent_invariance.max_final_right_trace_delta << '\n';
    }
    require_true(
        report.intent_invariance.max_ai_trace_delta > 1.0e-4,
        "after mobility warm-up, different left intent must change the AI trace");
    require_true(
        !report.intent_invariance.left_intent_ignored,
        "the intent probe must no longer classify physical left intent as ignored");
}

void test_manual_correction_reports_axis_arbitration() {
    const auto& report = benchmark_report();
    bool found = false;
    for (const auto& scenario : report.scenarios) {
        if (scenario.name != "stationary_target_fast_ads_manual_correction") continue;
        found = true;
        require_true(
            scenario.axis_intent_intervention_frames >= 0,
            "manual correction scenario must report axis intervention count");
        require_true(
            scenario.minimum_manual_retention > 0.0 &&
                scenario.minimum_manual_retention <= 1.0,
            "manual correction scenario must report bounded retention");
    }
    require_true(found, "manual correction scenario must exist");
}

void test_fixed_production_chain_closes_report_gate() {
    const auto& report = benchmark_report();
    const auto& chain = report.production_chain;
    require_true(!chain.defect_reproduced, "fixed production chain must contain no defect");
    require_true(chain.desired_gate_pass, "production-chain desired gate must close");
    require_true(
        report.defect_count == 0,
        "all live-rate acceptance defects must be closed");
    require_true(report.desired_gate_pass, "report summary must close the fixed gate");
}

void require_invalid(
    const controller_native::left_stick_defect::BenchmarkReport& report,
    const char* expected_reason_fragment) {
    std::string reason;
    if (controller_native::left_stick_defect::validate_report(report, &reason)) {
        std::cerr << "[NativeLeftStickMotionBenchmarkTests] FAIL: mutation expecting '"
                  << expected_reason_fragment << "' was accepted\n";
        std::exit(1);
    }
    require_true(
        reason.find(expected_reason_fragment) != std::string::npos,
        "validation reason must identify the rejected production-chain contract");
}

void test_production_chain_validation_rejects_missing_contract_fields() {
    const auto report = controller_native::left_stick_defect::run_benchmark();

    auto missing_gap = report;
    missing_gap.production_chain.detector_candidate_gap_frames = 0;
    require_invalid(missing_gap, "candidate-present gap");

    auto misclassified_drift = report;
    misclassified_drift.production_chain.drift_manual_correction_frames = 1;
    require_invalid(misclassified_drift, "drift classification");

    auto missing_phase = report;
    missing_phase.production_chain.ads_snap_frames = 0;
    require_invalid(missing_phase, "production-chain mode coverage");

    auto missing_track_rebind = report;
    missing_track_rebind.production_chain.selected_track_changes = 0;
    require_invalid(missing_track_rebind, "selected-track rebind");

    auto sequence_mismatch = report;
    ++sequence_mismatch.frequency_runs.front().fresh_sequences_consumed;
    require_invalid(sequence_mismatch, "sequence consumption mismatch");

    auto invalid_reacquire = report;
    invalid_reacquire.production_chain.reacquire_max_output_delta = 0.08;
    require_invalid(invalid_reacquire, "reacquisition envelope");
}

void require_contains(
    const std::string& value,
    const char* expected,
    const char* message) {
    require_true(value.find(expected) != std::string::npos, message);
}

void test_production_chain_json_contract() {
    const auto report = controller_native::left_stick_defect::run_benchmark();
    std::ostringstream output;
    controller_native::left_stick_defect::write_json(output, report);
    const std::string json = output.str();
    require_contains(json, "\"production_chain\"", "JSON must include production chain");
    require_contains(json, "\"schema_version\": 3", "JSON must include schema 3");
    require_contains(json, "\"controller_hz\": 1000", "JSON must include 1000 Hz control");
    require_contains(json, "\"frequency_runs\"", "JSON must include frequency matrix");
    require_contains(json, "\"primary\"", "JSON must include primary acceptance summary");
    require_contains(json, "\"ads_handoff\"", "JSON must include ADS handoff metrics");
    require_contains(
        json,
        "\"interframe_left_transition_frames\"",
        "JSON must report inter-frame left transition coverage");
    require_contains(
        json,
        "\"max_interframe_left_delta\"",
        "JSON must report inter-frame left transition magnitude");
    require_contains(
        json,
        "\"max_continuous_drift_only_ms\"",
        "JSON must include drift-only duration");
    require_contains(
        json,
        "\"requested_suppressed_frames\"",
        "JSON must include requested-assist suppression count");
    require_contains(
        json,
        "\"selected_track_changes\"",
        "JSON must include selected-track changes");
    require_contains(
        json,
        "\"reacquire_max_output_delta\"",
        "JSON must include measured reacquisition smoothness");
    require_contains(
        json,
        "\"detector_candidates_present\"",
        "JSON events must include detector-candidate state");
    require_contains(
        json,
        "\"limit_reason\"",
        "JSON events must include the final-output limit reason");
}

}  // namespace

int main() {
    test_live_frequency_matrix_contract();
    test_desired_acceptance_gates_are_closed();
    test_production_chain_behavior_is_populated();
    test_warm_left_intent_changes_ai_trace();
    test_manual_correction_reports_axis_arbitration();
    test_fixed_production_chain_closes_report_gate();
    test_production_chain_validation_rejects_missing_contract_fields();
    test_production_chain_json_contract();
    std::cout << "[NativeLeftStickMotionBenchmarkTests] PASS\n";
    return 0;
}
