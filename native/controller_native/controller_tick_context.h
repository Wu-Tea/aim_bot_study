#pragma once

#include "../common_native/time_types.h"

#include <string>
#include <cstdint>

namespace controller_native {

struct NativeControllerVisionState {
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t selected_track_id = 0;
    bool fresh_observation = false;
    bool current_observed_target_present = false;
    bool has_target = false;
    bool auto_fire_requested = false;
    float dx = 0.0f;
    float dy = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float screen_center_x = 0.0f;
    float screen_center_y = 0.0f;
    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    bool aim_authority = false;
    bool fire_authority = false;
    std::string target_tier = "none";
    double observed_at_seconds = 0.0;
};

}  // namespace controller_native
