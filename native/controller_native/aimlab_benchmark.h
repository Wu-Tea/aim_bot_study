#pragma once

#include "common_native/screen_geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace controller_native::aimlab {

struct FrameScoreInput {
    int intended_target_id = -1;
    int selected_target_id = -1;
    bool has_selected_target = false;
    bool strong_snap_active = false;
    bool selected_is_corpse = false;
    bool selected_is_friendly_or_unknown = false;
    common_native::Vec2f aim_error_before_px;
    common_native::Vec2f aim_error_after_px;
    common_native::Vec2f controller_output;
    common_native::Vec2f user_input;
    double dt_seconds = 0.0;
};

struct ScoreReport {
    int frames = 0;
    int intended_selected_frames = 0;
    int wrong_strong_lock_frames = 0;
    int wrong_target_ads_snap_count = 0;
    int corpse_lock_frames = 0;
    int friendly_or_unknown_lock_frames = 0;
    int overshoot_over_50px_count = 0;
    int helpful_output_frames = 0;
    int harmful_output_frames = 0;
    int user_fight_frames = 0;
    double time_on_intended_target_ratio = 0.0;
    double helpful_output_ratio = 0.0;
    double selection_score = 100.0;
    double control_score = 100.0;
    double cooperation_score = 100.0;
    double smoothness_score = 100.0;
    double authority_safety_score = 100.0;
    double final_score = 100.0;
};

class ScoreAggregator {
public:
    void add_frame(const FrameScoreInput& frame);
    ScoreReport report() const;

private:
    ScoreReport report_;
};

double vector_length(common_native::Vec2f value);
double dot(common_native::Vec2f lhs, common_native::Vec2f rhs);
double clamp_score(double value);

}  // namespace controller_native::aimlab
