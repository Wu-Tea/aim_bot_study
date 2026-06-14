#pragma once

#include "../common_native/screen_geometry.h"

namespace tracking_native {

common_native::Vec2f project_aim_error(
    common_native::Vec2f observed_error,
    common_native::Vec2f velocity_px_per_sec,
    common_native::Vec2f camera_motion_px,
    double age_seconds);

common_native::Box2f translate_box(common_native::Box2f box, common_native::Vec2f delta);

}  // namespace tracking_native
