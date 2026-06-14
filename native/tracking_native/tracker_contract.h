#pragma once

#include "../common_native/authority_types.h"
#include "../common_native/screen_geometry.h"
#include "../common_native/stick_types.h"
#include "../common_native/time_types.h"

#include <string>

namespace tracking_native {

enum class TrackerSnapshotSource {
    Absent,
    Observed,
    Projected,
    Coast,
};

struct TrackerObservation {
    bool has_target = false;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    std::string target_tier = "none";
    common_native::TimeSeconds capture_time;
};

struct TrackerControlSample {
    common_native::TimeSeconds apply_time;
    common_native::DurationSeconds dt;
    common_native::StickComponents sticks;
};

struct TrackerQuery {
    common_native::TimeSeconds query_time;
};

struct TrackerSnapshot {
    bool has_target = false;
    TrackerSnapshotSource source = TrackerSnapshotSource::Absent;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    common_native::TimeSeconds observed_at;
    double projection_age_ms = 0.0;
    common_native::AssistAuthority assist_authority = common_native::AssistAuthority::None;
    common_native::FireAuthority fire_authority = common_native::FireAuthority::None;
};

}  // namespace tracking_native
