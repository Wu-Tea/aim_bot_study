#pragma once

#include "xinput_reader.h"
#include "touchpad_regions.h"

#include <cmath>

namespace controller_native {

struct TouchpadFireDecision {
    bool requested = false;
    bool pressed = false;
};

// A held touch is an explicit human fire request, independent of Vision/ADS.
// No toggle, release grace, aim activation or recoil contribution is generated.
class TouchpadFire {
public:
    void reset() noexcept { pulse_started_ = -1.0; }

    TouchpadFireDecision update(const PhysicalGamepadState& physical,
                               double now, float width_ms, float period_ms) noexcept {
        TouchpadFireDecision result;
        if (physical.connected && std::isfinite(now) && now >= 0 &&
            std::isfinite(width_ms) && std::isfinite(period_ms) &&
            width_ms > 0 && period_ms > width_ms) {
            for (const auto& finger : physical.touchpad_fingers) {
                result.requested |= finger.active &&
                    std::isfinite(finger.x) && std::isfinite(finger.y) &&
                    finger.x >= 0.75f && finger.x <= 1.0f &&
                    finger.y >= 0.0f && finger.y <= kTouchpadMacroSplitY;
            }
        }
        if (!result.requested) {
            reset();
            return result;
        }
        const double period = static_cast<double>(period_ms) / 1000.0;
        if (pulse_started_ < 0 || now < pulse_started_ ||
            now + 1e-9 >= pulse_started_ + period) {
            // Start at the current sample; never replay pulses missed in a stall.
            pulse_started_ = now;
        }
        result.pressed = now - pulse_started_ + 1e-9 <
            static_cast<double>(width_ms) / 1000.0;
        return result;
    }

private:
    double pulse_started_ = -1.0;
};

}  // namespace controller_native
