#pragma once

#include "runtime_config.h"

namespace controller_native {

struct NativeAimAssistDynamicsInput {
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    float assisted_right_x = 0.0f;
    float assisted_right_y = 0.0f;
    bool recoil_active = false;
    bool manual_fire_active = false;
    bool auto_fire_active = false;
    double now_seconds = 0.0;
};

struct NativeAimAssistDynamicsOutput {
    float right_x = 0.0f;
    float right_y = 0.0f;
};

class NativeAimAssistDynamics {
public:
    explicit NativeAimAssistDynamics(GamepadAimAssistDynamicsConfig config = {});

    void reset();
    NativeAimAssistDynamicsOutput apply(const NativeAimAssistDynamicsInput& input);

private:
    float guard_recoil_axis_jitter(float raw_assist, float previous_assist, double now_seconds) const;
    bool within_memory_window(double now_seconds) const;

    GamepadAimAssistDynamicsConfig config_;
    bool has_last_raw_assist_ = false;
    float last_raw_assist_x_ = 0.0f;
    float last_raw_assist_y_ = 0.0f;
    double last_timestamp_seconds_ = 0.0;
};

}  // namespace controller_native
