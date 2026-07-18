#pragma once

#include "sustained_aimlab_types.h"

#include <cstdint>
#include <vector>

namespace controller_native::sustained_aimlab {

struct ScoreFrame {
    int absolute_ms = 0;
    int target_elapsed_ms = 0;
    bool in_tracking_window = false;
    bool target_observed = true;
    bool tracker_reliable = true;
    bool manual_escape = false;
    bool bodylock_mode = false;
    std::uint64_t target_id = 0;
    Vec2d error_px;
    Vec2d target_velocity_px_per_second;
    Vec2d manual_stick;
    Vec2d requested_assist_stick;
    Vec2d shaped_assist_stick;
    Vec2d final_stick;
};

struct TargetResult {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    int deadline_ms = 0;
    bool acquired = false;
    bool acquisition_timed_out = false;
    bool bodylock_entry_failed = false;
    bool first_pass_success = false;
    int first_entry_ms = -1;
    int bodylock_entry_ms = -1;
    int bodylock_active_ms = 0;
    int unexpected_mode_ms = 0;
    double acquire_points = 0.0;
    double tracking_points = 0.0;
    double smooth_bonus = 0.0;
    int over_events = 0;
    int undertrack_events = 0;
    int undertrack_total_ms = 0;
    int false_mode_exit_events = 0;
    int assist_dropout_events = 0;
    int interruption_total_ms = 0;
    int false_stop_events = 0;
    int false_stop_total_ms = 0;
    int stale_output_after_stop_events = 0;
    int circle_exit_events = 0;
    int stall_ring_ms = 0;
    int zero_output_while_demanded_ms = 0;
    int direction_discontinuities = 0;
    double max_error_px = 0.0;
    std::vector<double> tracking_errors_px;
    std::vector<double> output_deltas;
    std::vector<double> output_jerks;
};

class TargetScorer {
public:
    TargetScorer(TargetScript script, BenchmarkConfig config);

    void add_frame(const ScoreFrame& frame);
    void mark_acquired(int entry_ms);
    void mark_timed_out();
    void mark_bodylock_entered(int entry_ms);
    void mark_bodylock_entry_failed();
    TargetResult finish();

private:
    TargetScript script_;
    BenchmarkConfig config_;
    TargetResult result_;
    bool finished_ = false;
    bool has_previous_ = false;
    Vec2d previous_output_;
    Vec2d previous_shaped_assist_;
    double previous_distance_ = 0.0;
    double previous_output_delta_ = 0.0;
    int tracking_ticks_ = 0;
    bool outside_latched_ = false;
    bool overshoot_armed_ = false;
    bool overshoot_latched_ = false;
    Vec2d overshoot_arm_error_;
    int overshoot_arm_tick_ = 0;
    int undertrack_ticks_ = 0;
    bool undertrack_latched_ = false;
    int false_stop_ticks_ = 0;
    bool false_stop_latched_ = false;
    int false_mode_exit_ticks_ = 0;
    bool false_mode_exit_latched_ = false;
    bool bodylock_seen_ = false;
    bool assist_dropout_latched_ = false;
    int stale_output_ticks_ = 0;
    bool stale_output_latched_ = false;
};

struct BenchmarkResult {
    std::uint32_t seed = 0;
    std::uint64_t script_hash = 0;
    ManualProfile manual_profile = ManualProfile::Pure;
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire;
    int ticks = 0;
    double acquire_points = 0.0;
    double tracking_points = 0.0;
    double smooth_bonus = 0.0;
    int targets_spawned = 0;
    int targets_acquired = 0;
    int targets_missed = 0;
    int over_events = 0;
    int undertrack_events = 0;
    int false_interruption_events = 0;
    int false_stop_events = 0;
    int stale_output_after_stop_events = 0;
    int bodylock_entry_failures = 0;
    int bodylock_active_ms = 0;
    int unexpected_mode_ms = 0;
    double mean_error_px = 0.0;
    double p95_error_px = 0.0;
    double p95_output_delta = 0.0;
    double p95_jerk = 0.0;
    std::vector<TargetResult> targets;
};

BenchmarkResult aggregate(
    std::uint32_t seed,
    std::uint64_t script_hash,
    std::vector<TargetResult> targets);

double length(Vec2d value) noexcept;
double dot(Vec2d left, Vec2d right) noexcept;

}  // namespace controller_native::sustained_aimlab
