#include "ego_motion_buffer.h"

#include <algorithm>

namespace tracking_native {

void EgoMotionBuffer::reset() {
    camera_motion_px_ = {};
}

void EgoMotionBuffer::record_stick(
    float right_x,
    float right_y,
    double dt_seconds,
    float px_per_second) {
    if (dt_seconds <= 0.0) {
        return;
    }
    const float pixels_per_stick =
        std::max(0.0f, px_per_second) * static_cast<float>(dt_seconds);
    camera_motion_px_.x += right_x * pixels_per_stick;
    camera_motion_px_.y += -right_y * pixels_per_stick;
}

common_native::Vec2f EgoMotionBuffer::camera_motion_px() const {
    return camera_motion_px_;
}

}  // namespace tracking_native
