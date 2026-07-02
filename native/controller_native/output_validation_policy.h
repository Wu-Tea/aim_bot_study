#pragma once

#include "controller_tick_context.h"
#include "runtime_config.h"
#include "virtual_gamepad.h"

namespace controller_native {

struct OutputValidationPolicyInput {
    NativeControllerVisionState vision_state;
    GamepadOutputState output;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    bool ads_active = false;
    bool candidate_output_hold_active = false;
    double now_seconds = 0.0;
};

class OutputValidationPolicy {
public:
    explicit OutputValidationPolicy(GamepadAiAimConfig ai_config = {});

    void reset();
    GamepadOutputState apply(const OutputValidationPolicyInput& input);

private:
    void apply_tracker_projection_guard(
        const OutputValidationPolicyInput& input,
        GamepadOutputState& output) const;
    void apply_observed_target_guard(
        const OutputValidationPolicyInput& input,
        GamepadOutputState& output);

    GamepadAiAimConfig ai_config_;
    bool has_last_error_ = false;
    float last_error_x_ = 0.0f;
    float last_error_y_ = 0.0f;
    double correction_x_until_seconds_ = 0.0;
    double correction_y_until_seconds_ = 0.0;
};

}  // namespace controller_native
