#pragma once

#include "controller_tick_context.h"
#include "runtime_config.h"
#include "virtual_gamepad.h"

namespace controller_native {

struct BodyLockShortPlanInput {
    NativeControllerVisionState vision_state;
    GamepadOutputState output;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    bool vertical_plan_allowed = false;
    bool body_lock_available = false;
    float lock_dx = 0.0f;
    float lock_dy = 0.0f;
    double now_seconds = 0.0;
};

class BodyLockShortPlanPolicy {
public:
    explicit BodyLockShortPlanPolicy(GamepadAiAimConfig ai_config = {});

    void reset();
    GamepadOutputState apply(const BodyLockShortPlanInput& input);

private:
    void apply_aim_error_manual_cross_brake(
        const BodyLockShortPlanInput& input,
        GamepadOutputState& output);
    void apply_active_manual_cross_brake(
        float manual_axis,
        float& output_axis,
        double& brake_until_seconds,
        int& brake_manual_sign,
        float current_error,
        bool y_axis,
        double now_seconds);
    void update_manual_cross_brake(
        float previous_error,
        float current_error,
        float manual_axis,
        float& output_axis,
        double& brake_until_seconds,
        int& brake_manual_sign,
        bool y_axis,
        double now_seconds);
    void reset_short_plan_state();

    GamepadAiAimConfig ai_config_;
    bool has_last_short_plan_x_ = false;
    bool has_last_short_plan_y_ = false;
    float last_short_plan_x_ = 0.0f;
    float last_short_plan_y_ = 0.0f;
    bool has_last_body_lock_error_ = false;
    float last_body_lock_error_x_ = 0.0f;
    float last_body_lock_error_y_ = 0.0f;
    bool has_last_aim_error_ = false;
    float last_aim_error_x_ = 0.0f;
    float last_aim_error_y_ = 0.0f;
    double short_plan_x_until_seconds_ = 0.0;
    double short_plan_y_until_seconds_ = 0.0;
    double manual_brake_x_until_seconds_ = 0.0;
    double manual_brake_y_until_seconds_ = 0.0;
    int manual_brake_x_sign_ = 0;
    int manual_brake_y_sign_ = 0;
};

}  // namespace controller_native
