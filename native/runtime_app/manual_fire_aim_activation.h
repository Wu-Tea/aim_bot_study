#pragma once

namespace runtime_app {

struct ManualFireAimActivation {
    bool scope_active = false;
    bool fire_pressed_now = false;
    bool force_acquisition_rearm = false;
};

class ManualFireAimActivationTracker {
public:
    ManualFireAimActivation update(
        bool enabled,
        bool fire_pressed,
        bool target_present) {
        const bool fire_pressed_now = fire_pressed && !previous_fire_pressed_;
        if (!enabled || !fire_pressed) {
            scope_active_ = false;
        } else if (fire_pressed_now) {
            scope_active_ = true;
        }
        previous_fire_pressed_ = fire_pressed;
        // An already selected idle target must not block fire-aim activation.
        // It only removes the need to force a second acquisition reset.
        return {
            scope_active_,
            fire_pressed_now,
            enabled && fire_pressed_now && !target_present,
        };
    }

private:
    bool scope_active_ = false;
    bool previous_fire_pressed_ = false;
};

}  // namespace runtime_app
