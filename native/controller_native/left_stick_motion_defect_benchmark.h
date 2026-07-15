#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace controller_native::left_stick_defect {

struct PhaseEvent {
    std::string phase;
    int tick = 0;
    double time_seconds = 0.0;
    float left_x = 0.0f;
    float manual_right_x = 0.0f;
    float ai_right_x = 0.0f;
    float final_right_x = 0.0f;
    float oracle_right_x = 0.0f;
    double observed_error_px = 0.0;
    double true_error_px = 0.0;
    double vision_age_ms = 0.0;
    double player_velocity_body_s = 0.0;
    double target_velocity_body_s = 0.0;
    std::string aim_mode;
};

struct IntentInvarianceMetrics {
    double max_ai_trace_delta = 0.0;
    double max_final_right_trace_delta = 0.0;
    double max_left_passthrough_error = 0.0;
    int phase_event_samples = 0;
    bool left_intent_ignored = false;
    bool desired_gate_pass = true;
};

struct FrequencyRunMetrics {
    int vision_hz = 0;
    int delivered_vision_sequences = 0;
    int fresh_sequences_consumed = 0;
    double baseline_fast_mean_error_px = 0.0;
    double fast_mean_error_px = 0.0;
    double baseline_fast_p95_error_px = 0.0;
    double fast_p95_error_px = 0.0;
    double fast_mean_improvement_ratio = 0.0;
    double fast_p95_improvement_ratio = 0.0;
    double same_direction_regression_ratio = 0.0;
    double max_lifecycle_ai_delta = 0.0;
    double max_relative_lead_px = 0.0;
    double max_strafe_gain = 0.0;
    int warm_relative_motion_frames = 0;
    int rejected_relative_motion_frames = 0;
    int large_sign_flip_count = 0;
    bool primary_rate = false;
    bool desired_gate_pass = false;
};

struct PrimaryFrequencyMetrics {
    double fast_mean_improvement_ratio = 0.0;
    double fast_p95_improvement_ratio = 0.0;
    double same_direction_regression_ratio = 0.0;
    double max_lifecycle_ai_delta = 0.0;
    int large_sign_flip_count = 0;
    bool desired_gate_pass = false;
};

struct AdsHandoffMetrics {
    double stationary_transition_overshoot_px = 0.0;
    double moving_transition_overshoot_px = 0.0;
    double max_transition_overshoot_px = 0.0;
    double max_transition_ai_delta = 0.0;
    bool desired_gate_pass = false;
};

struct ScenarioMetrics {
    std::string name;
    std::string mobility;
    std::string target_relation;
    double body_height_px = 0.0;
    double mean_abs_error_px = 0.0;
    double p95_abs_error_px = 0.0;
    double max_abs_error_px = 0.0;
    double final_abs_error_px = 0.0;
    double max_ai_output = 0.0;
    double max_final_output = 0.0;
    double max_final_output_delta = 0.0;
    double onset_response_latency_ms = -1.0;
    double reversal_response_latency_ms = -1.0;
    double release_response_latency_ms = -1.0;
    double onset_peak_error_px = 0.0;
    double reversal_peak_error_px = 0.0;
    double release_peak_error_px = 0.0;
    int ai_opposes_manual_frames = 0;
    int final_opposes_oracle_frames = 0;
    int high_ai_output_frames = 0;
    int output_spike_frames = 0;
    int settled_frames = 0;
    bool behavior_populated = false;
    bool defect_reproduced = false;
    bool desired_gate_pass = true;
    std::vector<std::string> defect_reasons;
    std::vector<PhaseEvent> events;
};

struct ProductionChainEvent {
    std::string phase;
    int tick = 0;
    bool detector_candidates_present = false;
    bool production_target_present = false;
    std::uint64_t selected_track_id = 0;
    float manual_right_x = 0.0f;
    float requested_ai_x = 0.0f;
    float final_right_x = 0.0f;
    double target_error_px = 0.0;
    std::string aim_mode;
    std::string lifecycle;
    std::string limit_reason;
};

struct ProductionChainMetrics {
    std::string name = "production_chain_strafe_reacquire";
    int total_frames = 0;
    int body_lock_frames = 0;
    int ads_snap_frames = 0;
    int manual_frames = 0;
    int mode_transitions = 0;
    int detector_candidate_gap_frames = 0;
    int production_target_missing_frames = 0;
    int target_present_bodylock_unavailable_frames = 0;
    int drift_manual_correction_frames = 0;
    int drift_only_final_frames = 0;
    int requested_suppressed_frames = 0;
    int selected_track_changes = 0;
    double max_abs_manual_right = 0.0;
    double max_continuous_drift_only_ms = 0.0;
    double reacquire_latency_ms = -1.0;
    double reacquire_useful_latency_ms = -1.0;
    double reacquire_max_output_delta = 0.0;
    double pre_loss_error_px = 0.0;
    double post_reacquire_error_px = 0.0;
    bool behavior_populated = false;
    bool short_gap_coast_pass = false;
    bool long_loss_release_pass = false;
    bool reacquire_bumpless_pass = false;
    bool no_blind_candidate_follow_pass = false;
    bool defect_reproduced = false;
    bool desired_gate_pass = true;
    std::vector<std::string> defect_reasons;
    std::vector<ProductionChainEvent> events;
};

struct BenchmarkReport {
    int schema_version = 3;
    int controller_hz = 1000;
    int vision_hz = 100;
    int vision_delay_ms = 30;
    int ticks_per_scenario = 3600;
    int evaluation_start_tick = 700;
    IntentInvarianceMetrics intent_invariance;
    std::vector<FrequencyRunMetrics> frequency_runs;
    PrimaryFrequencyMetrics primary;
    std::vector<ScenarioMetrics> scenarios;
    ProductionChainMetrics production_chain;
    AdsHandoffMetrics ads_handoff;
    int defect_count = 0;
    bool desired_gate_pass = true;
};

BenchmarkReport run_benchmark();
bool validate_report(const BenchmarkReport& report, std::string* reason);
void write_json(std::ostream& output, const BenchmarkReport& report);

}  // namespace controller_native::left_stick_defect
