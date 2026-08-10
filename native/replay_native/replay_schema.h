#pragma once

#include "../common_native/screen_geometry.h"
#include "../common_native/stick_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace replay_native {

struct ReplayDetection {
    common_native::Box2f box_px;
    float confidence = 0.0f;
    std::string class_name;
};

struct ReplayFrameTiming {
    double capture_time_seconds = 0.0;
    double vision_ready_time_seconds = 0.0;
    double controller_tick_ms = 0.0;
    double cpu_tick_ms = 0.0;
};

struct ReplayTargetState {
    bool has_target = false;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    std::string tier = "none";
    std::string source = "none";
    float confidence = 0.0f;
    double age_ms = 0.0;
};

struct ReplayControllerState {
    bool aiming = false;
    bool fire_requested = false;
    bool fire_allowed = false;
    bool fire_blocked = false;
    common_native::StickComponents sticks;
};

struct NativeReplayFrame {
    std::uint64_t frame_id = 0;
    ReplayFrameTiming timing;
    common_native::Box2f roi_px;
    std::vector<ReplayDetection> detections;
    ReplayTargetState selected_target;
    ReplayControllerState controller;
    std::string weapon_id;
};

}  // namespace replay_native
