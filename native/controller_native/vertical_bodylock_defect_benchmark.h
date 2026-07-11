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

Metrics run_prone_air_lock();
Metrics run_stairs_air_lock();
Metrics run_cooperative_overshoot_occlusion();

} // namespace controller_native::vertical_defect
