#pragma once

#include "xinput_reader.h"

#include <algorithm>

namespace controller_native {

constexpr float kAimLeftTriggerPressThreshold = 0.05f;
constexpr float kAimLeftTriggerIdleThreshold = 0.03f;
constexpr float kAimLeftTriggerReleaseDropThreshold = 0.08f;
constexpr float kAimLeftTriggerRepressRiseThreshold = 0.08f;

class AimActivationTracker {
public:
    void reset() {
        active_ = false;
        has_last_left_trigger_ = false;
        last_left_trigger_ = 0.0f;
        left_trigger_release_latched_ = false;
    }

    bool update(const PhysicalGamepadState& physical, bool rb_counts_as_aiming) {
        const float left_trigger =
            std::max(0.0f, std::min(1.0f, physical.left_trigger));
        const bool rb_aiming = rb_counts_as_aiming && physical.rb;
        if (rb_aiming) {
            remember_left_trigger(left_trigger);
            active_ = true;
            left_trigger_release_latched_ = false;
            return true;
        }

        if (!has_last_left_trigger_) {
            remember_left_trigger(left_trigger);
            active_ = left_trigger > kAimLeftTriggerPressThreshold;
            left_trigger_release_latched_ = false;
            return active_;
        }

        const float previous_left_trigger = last_left_trigger_;
        const float trigger_delta = left_trigger - previous_left_trigger;
        remember_left_trigger(left_trigger);

        if (left_trigger <= kAimLeftTriggerIdleThreshold) {
            active_ = false;
            left_trigger_release_latched_ = false;
            return false;
        }

        if (active_ &&
            previous_left_trigger - left_trigger >= kAimLeftTriggerReleaseDropThreshold) {
            active_ = false;
            left_trigger_release_latched_ = true;
            return false;
        }

        if (left_trigger_release_latched_) {
            if (trigger_delta >= kAimLeftTriggerRepressRiseThreshold) {
                active_ = true;
                left_trigger_release_latched_ = false;
            }
            return active_;
        }

        if (!active_ && left_trigger > kAimLeftTriggerPressThreshold) {
            active_ = true;
        }
        return active_;
    }

private:
    void remember_left_trigger(float left_trigger) {
        last_left_trigger_ = left_trigger;
        has_last_left_trigger_ = true;
    }

    bool active_ = false;
    bool has_last_left_trigger_ = false;
    float last_left_trigger_ = 0.0f;
    bool left_trigger_release_latched_ = false;
};

}  // namespace controller_native
