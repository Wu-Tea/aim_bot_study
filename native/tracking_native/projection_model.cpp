#include "projection_model.h"

namespace tracking_native {

common_native::Vec2f project_aim_error(
    common_native::Vec2f observed_error,
    common_native::Vec2f velocity_px_per_sec,
    common_native::Vec2f camera_motion_px,
    double age_seconds) {
    return {
        observed_error.x + (velocity_px_per_sec.x * static_cast<float>(age_seconds)) -
            camera_motion_px.x,
        observed_error.y + (velocity_px_per_sec.y * static_cast<float>(age_seconds)) -
            camera_motion_px.y};
}

common_native::Box2f translate_box(common_native::Box2f box, common_native::Vec2f delta) {
    box.x += delta.x;
    box.y += delta.y;
    return box;
}

}  // namespace tracking_native
