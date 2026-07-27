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
    bool vision_occluded = false;
    bool fresh_vision = false;
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
    Vec2d predicted_terminal_error_px;
    double radial_closing_velocity_px_per_sec = 0.0;
    bool ads_to_bodylock_transition = false;
};

struct TargetResult {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    int deadline_ms = 0;
    double visible_radius_px = 24.0;
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
    bool settled = false;
    int center_cross_events = 0;
    double max_post_cross_error_px = 0.0;
    double overshoot_area_px_ms = 0.0;
    double maximum_vertical_overshoot_px = 0.0;
    double post_cross_error_area_px_ms = 0.0;
    double post_cross_wrong_way_output_integral = 0.0;
    int continued_push_after_cross_ms = 0;
    double brake_start_distance_px = -1.0;
    int time_to_zero_radial_speed_ms = -1;
    int first_entry_to_settle_ms = -1;
    int correction_reversal_events = 0;
    bool handoff_observed = false;
    double handoff_residual_px = -1.0;
    double handoff_closing_speed_px_per_sec = 0.0;
    int post_handoff_local_samples = 0;
    int post_handoff_tail_samples = 0;
    double post_handoff_local_error_area_px_ms = 0.0;
    double post_handoff_tail_error_area_px_ms = 0.0;
    double post_handoff_max_error_px = 0.0;
    double post_handoff_min_error_px = 0.0;
    double post_handoff_rebound_px = 0.0;
    double post_handoff_wrong_way_output_integral = 0.0;
    int post_handoff_wrong_way_ms = 0;
    int post_handoff_circle_exit_events = 0;
    int post_handoff_settle_ms = -1;
    bool handoff_defect = false;
    int occlusion_episodes = 0;
    int post_occlusion_samples = 0;
    double post_occlusion_error_area_px_ms = 0.0;
    double max_post_occlusion_error_px = 0.0;
    int reveal_to_stable_ms = -1;
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
    void mark_ads_to_bodylock_handoff(
        int entry_ms,
        Vec2d error_px,
        double radial_closing_velocity_px_per_sec);
    void mark_bodylock_entry_failed();
    TargetResult finish();

private:
    void end_brake_episode() noexcept;

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
    bool brake_episode_active_ = false;
    Vec2d brake_axis_;
    bool positive_side_seen_ = false;
    bool center_cross_latched_ = false;
    bool crossed_center_ = false;
    int brake_start_tick_ = -1;
    int first_circle_tick_ = -1;
    int settle_stable_ticks_ = 0;
    double previous_ai_radial_projection_ = 0.0;
    bool has_previous_ai_radial_projection_ = false;
    int handoff_tick_ = -1;
    double handoff_min_error_px_ = 0.0;
    bool handoff_outside_circle_ = false;
    bool was_vision_occluded_ = false;
    bool awaiting_fresh_reveal_ = false;
    bool reveal_recovery_active_ = false;
    int post_occlusion_ticks_remaining_ = 0;
    int reveal_recovery_elapsed_ms_ = 0;
    int reveal_stable_ticks_ = 0;
};

struct BenchmarkResult {
    std::uint32_t seed = 0;
    std::uint64_t script_hash = 0;
    ManualProfile manual_profile = ManualProfile::Pure;
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire;
    PlayerStrafeMode player_strafe_mode = PlayerStrafeMode::Off;
    int ticks = 0;
    int left_strafe_active_ms = 0;
    int left_strafe_reversals = 0;
    double max_abs_left_x = 0.0;
    double min_sampled_player_top_speed_px_per_second = 0.0;
    double max_sampled_player_top_speed_px_per_second = 0.0;
    double max_abs_player_speed_px_per_second = 0.0;
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
    int settled_targets = 0;
    int unsettled_targets = 0;
    int center_cross_events = 0;
    double max_post_cross_error_px = 0.0;
    double p95_post_cross_error_px = 0.0;
    double overshoot_area_px_ms = 0.0;
    double maximum_vertical_overshoot_px = 0.0;
    double post_cross_error_area_px_ms = 0.0;
    double post_cross_wrong_way_output_integral = 0.0;
    int continued_push_after_cross_ms = 0;
    int correction_reversal_events = 0;
    int circle_exit_events = 0;
    int stall_ring_ms = 0;
    int direction_discontinuities = 0;
    double max_error_px = 0.0;
    double median_first_entry_to_settle_ms = -1.0;
    double p95_first_entry_to_settle_ms = -1.0;
    int handoff_count = 0;
    double max_handoff_residual_px = 0.0;
    double max_abs_handoff_closing_speed_px_per_sec = 0.0;
    int handoff_episodes = 0;
    int handoff_defect_episodes = 0;
    double handoff_defect_rate = 0.0;
    double post_handoff_local_error_area_px_ms = 0.0;
    double post_handoff_tail_error_area_px_ms = 0.0;
    double p50_post_handoff_rebound_px = 0.0;
    double p95_post_handoff_rebound_px = 0.0;
    double max_post_handoff_rebound_px = 0.0;
    double p95_post_handoff_wrong_way_output_integral = 0.0;
    int occlusion_episodes = 0;
    int post_occlusion_samples = 0;
    double post_occlusion_error_area_px_ms = 0.0;
    double max_post_occlusion_error_px = 0.0;
    double p95_reveal_to_stable_ms = -1.0;
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
