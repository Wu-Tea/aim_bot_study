#pragma once

#include "xinput_reader.h"
#include <cmath>

namespace controller_native {

// One double tap per entry into the lower-right region. Releasing contact
// does not truncate the gesture; disconnect/reset cancels it. Each transition
// runs on a controller tick, without sleeping or queuing overlapping gestures.
class TouchpadTriangle {
public:
    void reset() noexcept {
        was_inside_ = false;
        phase_ = 0;
        phase_started_ = -1;
    }

    bool update(const PhysicalGamepadState& physical, double now) noexcept {
        if (!physical.connected || !std::isfinite(now) || now < 0 ||
            (phase_started_ >= 0 && now < phase_started_)) {
            reset();
            return false;
        }
        bool inside = false;
        for (const auto& finger : physical.touchpad_fingers) {
            inside |= finger.active && std::isfinite(finger.x) && std::isfinite(finger.y) &&
                finger.x >= .75f && finger.x <= 1 && finger.y > .5f && finger.y <= 1;
        }
        const bool entered = inside && !was_inside_;
        was_inside_ = inside;
        if (phase_ != 0 && now + 1e-9 >= phase_started_ + .020) {
            // Preserve an observable release gap even after a late tick.
            phase_ = phase_ == 3 ? 0 : phase_ + 1;
            phase_started_ = now;
        }
        if (phase_ == 0 && entered) {
            phase_ = 1;
            phase_started_ = now;
        }
        return phase_ == 1 || phase_ == 3;
    }

private:
    bool was_inside_ = false;
    int phase_ = 0; // idle, first press, release gap, second press
    double phase_started_ = -1;
};

}  // namespace controller_native
