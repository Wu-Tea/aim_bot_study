#pragma once

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

struct BenchmarkReport {
    int schema_version = 1;
    int controller_hz = 100;
    int vision_hz = 50;
    int vision_delay_ms = 30;
    int ticks_per_scenario = 360;
    int evaluation_start_tick = 70;
    IntentInvarianceMetrics intent_invariance;
    std::vector<ScenarioMetrics> scenarios;
    int defect_count = 0;
    bool desired_gate_pass = true;
};

BenchmarkReport run_benchmark();
bool validate_report(const BenchmarkReport& report, std::string* reason);
void write_json(std::ostream& output, const BenchmarkReport& report);

}  // namespace controller_native::left_stick_defect
