#pragma once

#include "xinput_reader.h"

#include <algorithm>

namespace controller_native {

constexpr float kAimLeftTriggerPressThreshold = 0.05f;
constexpr float kAimLeftTriggerIdleThreshold = 0.03f;
// The native controller normally samples near 1 kHz. Preserve protection from
// a one-sample USB/input read dropout without retaining AI ownership after a
// real physical release. Press remains immediate; release needs three
// consecutive idle samples (roughly 3 ms at the ordinary control cadence).
constexpr unsigned int kAimLeftTriggerIdleDebounceSamples = 3;

class AimActivationTracker {
public:
    void reset() {
        active_ = false;
        idle_samples_ = 0;
    }

    bool update(const PhysicalGamepadState& physical, bool rb_counts_as_aiming) {
        const float left_trigger =
            std::max(0.0f, std::min(1.0f, physical.left_trigger));
        const bool rb_aiming = rb_counts_as_aiming && physical.rb;
        if (rb_aiming) {
            active_ = true;
            idle_samples_ = 0;
            return true;
        }

        if (left_trigger <= kAimLeftTriggerIdleThreshold) {
            if (!active_) return false;
            ++idle_samples_;
            if (idle_samples_ >= kAimLeftTriggerIdleDebounceSamples) {
                active_ = false;
                idle_samples_ = 0;
            }
            return active_;
        }
        idle_samples_ = 0;

        if (!active_ && left_trigger > kAimLeftTriggerPressThreshold) {
            active_ = true;
        }
        return active_;
    }

private:
    bool active_ = false;
    unsigned int idle_samples_ = 0;
};

}  // namespace controller_native
