#include "sustained_aimlab_learning.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace controller_native::sustained_aimlab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

RoundControllerFactory proportional_factory() {
    return [] {
        return [](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            output.bodylock_mode = input.target_present;
            if (input.target_present) {
                output.final_stick.x = std::clamp(
                    input.observed_error_px.x * 0.018, -1.0, 1.0);
                output.final_stick.y = std::clamp(
                    -input.observed_error_px.y * 0.018, -1.0, 1.0);
                output.shaped_assist_stick = output.final_stick;
                output.requested_assist_stick = output.final_stick;
                output.predicted_terminal_error_px = input.observed_error_px;
            }
            return output;
        };
    };
}

void test_retained_learning_is_causal_and_becomes_actionable() {
    LearningExperimentConfig config;
    config.rounds = 3;
    config.round_duration_ms = 8'000;
    config.control_response_delay_ms = 45;
    config.policy = LearningPolicy::RetainAcrossRounds;
    const auto result = run_learning_experiment(
        config, 1337, ManualProfile::Pure, BenchmarkCohort::BodyLockFollow,
        proportional_factory());
    require(!result.ground_truth_used_for_policy,
            "learned policy must never consume planted response truth");
    require(result.rounds.size() == 3,
            "experiment must retain every requested round");
    require(result.rounds[0].script_hash != 0,
            "each learning round must retain scenario identity");
    require(result.rounds[1].accepted_updates > 0,
            "warm round must keep accepting causal response evidence");
    require(result.rounds[1].first_accepted_update_ms >= 10,
            "warm round must not accept evidence before the delay bank begins");
    require(result.rounds[1].valid_rollout_decisions > 0,
            "warm learner must become actionable instead of staying fallback-only");
    auto report = result;
    report.config_path = "C:\\bench\\config.toml";
    const std::string json = learning_experiment_to_json(report);
    require(json.find("\"schema\":\"sustained_aimlab_learning_v2\"") !=
                std::string::npos,
            "learning report must carry a versioned schema");
    require(json.find("\"ground_truth_used_for_policy\":false") !=
                std::string::npos,
            "learning report must make causal policy provenance explicit");
    require(json.find("\"plant_delay_ms\":45") != std::string::npos &&
                json.find("\"round_duration_ms\":8000") != std::string::npos,
            "learning report must retain the plant and duration contract");
    require(json.find("\"dirty\":false") != std::string::npos,
            "learning report must retain source-tree provenance");
    require(json.find("\"control_history_reset_each_round\":true") !=
                std::string::npos,
            "learning report must disclose round-boundary history isolation");
    require(json.find("C:\\\\bench\\\\config.toml") != std::string::npos,
            "learning report must JSON-escape Windows paths");
}

void test_reset_control_does_not_inherit_previous_round_state() {
    LearningExperimentConfig config;
    config.rounds = 2;
    config.round_duration_ms = 4'000;
    config.control_response_delay_ms = 45;
    config.policy = LearningPolicy::ResetEachRound;
    const auto result = run_learning_experiment(
        config, 7331, ManualProfile::Pure, BenchmarkCohort::BodyLockFollow,
        proportional_factory());
    require(result.rounds.size() == 2, "reset experiment must run twice");
    require(result.rounds[1].started_with_response_confidence == 0.0,
            "reset control must start every round cold");
}
}

int main() {
    try {
        test_retained_learning_is_causal_and_becomes_actionable();
        test_reset_control_does_not_inherit_previous_round_state();
        std::cout << "cod_native_sustained_aimlab_learning_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_learning_tests FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
