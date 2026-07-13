#pragma once

#include <string>
#include <vector>

namespace controller_native::vertical_defect {

struct FrameEvent {
    int frame = 0;
    double reticle_y = 0.0;
    double target_y = 0.0;
    double visible_body_top = 0.0;
    double visible_body_bottom = 0.0;
    float manual_y = 0.0f;
    float ai_y = 0.0f;
    float final_y = 0.0f;
    bool vision_authority = false;
    std::string mode;
};

struct Metrics {
    std::string name;
    double detection_top = 0.0;
    double detection_bottom = 0.0;
    double visible_body_top = 0.0;
    double visible_body_bottom = 0.0;
    double cue_y = 0.0;
    double selector_target_y = 0.0;
    double bodylock_target_y = 0.0;
    double target_distance_to_body_px = 0.0;
    double max_overshoot_px = 0.0;
    int outside_body_frames = 0;
    int ai_opposes_recovery_frames = 0;
    int recovery_start_frame = -1;
    int manual_escape_frame = -1;
    int reacquire_frame = -1;
    bool target_outside_visible_body = false;
    bool defect_reproduced = false;
    std::vector<FrameEvent> events;
};

struct ManualTakeoverMetrics {
    std::string name;
    double manual_direction_preservation_ratio = 0.0;
    double old_target_resistance_integral = 0.0;
    double max_continuous_reversal_ms = 0.0;
    double manual_stall_ms = 0.0;
    double manual_takeover_latency_ms = -1.0;
    int manual_reversal_frames = 0;
    int mode_transitions = 0;
    int body_lock_frames = 0;
    int downstream_brake_frames = 0;
    int ads_carry_brake_frames = 0;
    double min_committed_output = 1.0;
    bool cooperative_assist_preserved = false;
    bool short_noise_kept_body_lock = false;
    bool defect_reproduced = false;
};

Metrics run_prone_air_lock();
Metrics run_stairs_air_lock();
Metrics run_cooperative_overshoot_occlusion();
ManualTakeoverMetrics run_single_target_manual_takeover();
ManualTakeoverMetrics run_single_target_manual_takeover_legacy();
ManualTakeoverMetrics run_single_target_cooperative_tracking();
ManualTakeoverMetrics run_single_target_short_noise();
ManualTakeoverMetrics run_bodylock_crossing_continuity();

} // namespace controller_native::vertical_defect
