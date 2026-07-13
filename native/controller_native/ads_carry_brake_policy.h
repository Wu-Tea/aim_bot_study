#pragma once

#include "runtime_config.h"
#include "virtual_gamepad.h"

namespace controller_native {

struct AdsCarryBrakeInput {
    GamepadOutputState output;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    float target_error_x = 0.0f;
    float target_error_y = 0.0f;
    bool ads_active = false;
    bool ads_acquisition_active = false;
    bool body_lock_active = false;
    bool has_fresh_target = false;
    bool candidate_output_hold_active = false;
    float reticle_speed_px_per_sec = 1.0f;
};

class AdsCarryBrakePolicy {
public:
    explicit AdsCarryBrakePolicy(GamepadAiAimConfig ai_config = {});

    void reset();
    GamepadOutputState apply(const AdsCarryBrakeInput& input) const;

private:
    float apply_unfresh_axis_limit(float output_axis, float manual_axis) const;

    GamepadAiAimConfig ai_config_;
};

}  // namespace controller_native
