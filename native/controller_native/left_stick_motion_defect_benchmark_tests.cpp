#include "left_stick_motion_defect_benchmark.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require_true(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "[NativeLeftStickMotionBenchmarkTests] FAIL: " << message << '\n';
    std::exit(1);
}

void test_production_chain_behavior_is_populated() {
    const auto report = controller_native::left_stick_defect::run_benchmark();
    const auto& chain = report.production_chain;
    require_true(report.schema_version == 2, "extended JSON contract must use schema 2");
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
        chain.max_continuous_drift_only_ms >= 650.0,
        "evidence-matched drift-only output gap must be reproduced");
    require_true(
        chain.reacquire_latency_ms >= 0.0,
        "reacquisition latency must be populated");
    require_true(
        chain.body_lock_frames > 0 && chain.ads_snap_frames > 0 &&
            chain.manual_frames > 0,
        "body-lock, ADS-snap, and manual phases must all occur");
    require_true(
        chain.selected_track_changes > 0,
        "selected target must rebind after the evidence-matched gap");
}

void test_production_chain_defect_is_folded_into_report_gate() {
    const auto report = controller_native::left_stick_defect::run_benchmark();
    const auto& chain = report.production_chain;
    require_true(chain.defect_reproduced, "production-chain defect must be recorded");
    require_true(!chain.desired_gate_pass, "production-chain desired gate must remain RED");
    require_true(
        report.defect_count == 7,
        "production-chain defect must increment the existing six-defect baseline");
}

void require_invalid(
    const controller_native::left_stick_defect::BenchmarkReport& report,
    const char* expected_reason_fragment) {
    std::string reason;
    require_true(
        !controller_native::left_stick_defect::validate_report(report, &reason),
        "invalid production-chain report must be rejected");
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
        "\"detector_candidates_present\"",
        "JSON events must include detector-candidate state");
    require_contains(
        json,
        "\"limit_reason\"",
        "JSON events must include the final-output limit reason");
}

}  // namespace

int main() {
    test_production_chain_behavior_is_populated();
    test_production_chain_defect_is_folded_into_report_gate();
    test_production_chain_validation_rejects_missing_contract_fields();
    test_production_chain_json_contract();
    std::cout << "[NativeLeftStickMotionBenchmarkTests] PASS\n";
    return 0;
}
