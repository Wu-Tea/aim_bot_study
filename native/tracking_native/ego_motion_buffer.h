#pragma once

#include "../common_native/screen_geometry.h"

namespace tracking_native {

class EgoMotionBuffer {
public:
    void reset();
    void record_stick(float right_x, float right_y, double dt_seconds, float px_per_second);
    common_native::Vec2f camera_motion_px() const;

private:
    common_native::Vec2f camera_motion_px_;
};

}  // namespace tracking_native
