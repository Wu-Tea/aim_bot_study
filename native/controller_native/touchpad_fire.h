#pragma once

#include "xinput_reader.h"
#include "touchpad_regions.h"

#include <cmath>

namespace controller_native {

struct TouchpadFireDecision {
    bool requested = false;
    bool pressed = false;
};

inline constexpr float kTouchpadFireRpm = 1200.0f;
inline constexpr float kTouchpadFirePeriodMs = 60000.0f / kTouchpadFireRpm;
inline constexpr float kTouchpadFireWidthMs = 30.0f;

// A held touch is an explicit human fire request, independent of Vision/ADS.
// The contact also owns an aim lease; the pulse gaps must not release it.
// Cadence belongs to this macro, independently of target AutoFire settings.
class TouchpadFire {
public:
    void reset() noexcept { pulse_started_ = -1.0; }

    static bool requested(const PhysicalGamepadState& physical) noexcept {
        if (physical.connected) {
            for (const auto& finger : physical.touchpad_fingers) {
                if (finger.active &&
                    std::isfinite(finger.x) && std::isfinite(finger.y) &&
                    finger.x >= 0.75f && finger.x <= 1.0f &&
                    finger.y >= 0.0f && finger.y <= kTouchpadMacroSplitY) return true;
            }
        }
        return false;
    }

    TouchpadFireDecision update(const PhysicalGamepadState& physical, double now) noexcept {
        TouchpadFireDecision result;
        result.requested = requested(physical) && std::isfinite(now) && now >= 0;
        if (!result.requested) {
            reset();
            return result;
        }
        const double period = static_cast<double>(kTouchpadFirePeriodMs) / 1000.0;
        if (pulse_started_ < 0 || now < pulse_started_ ||
            now + 1e-9 >= pulse_started_ + period) {
            // Start at the current sample; never replay pulses missed in a stall.
            pulse_started_ = now;
        }
        result.pressed = now - pulse_started_ + 1e-9 <
            static_cast<double>(kTouchpadFireWidthMs) / 1000.0;
        return result;
    }

private:
    double pulse_started_ = -1.0;
};

}  // namespace controller_native
