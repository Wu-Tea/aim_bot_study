#pragma once

#include "pipeline_contract/intent_state.h"

namespace controller_native {

struct IntentFilterConfig {
    float base_deadzone = 0.02f;
    float neutral_learning_limit = 0.03f;
    float bias_alpha = 0.04f;
    float noise_alpha = 0.04f;
    float noise_multiplier = 3.0f;
    float noise_margin = 0.003f;
};

class IntentFilter {
public:
    explicit IntentFilter(IntentFilterConfig config = {});

    pipeline_contract::IntentState update(
        pipeline_contract::Vec2f raw_left,
        pipeline_contract::Vec2f raw_right,
        bool ads,
        bool fire,
        double sample_time_seconds) noexcept;

    void reset() noexcept;

private:
    struct AxisState {
        float bias = 0.0f;
        float noise = 0.0f;
    };

    struct StickState {
        bool active = false;
        float signed_value = 0.0f;
    };

    pipeline_contract::AxisIntentState update_axis(AxisState& state, float raw) noexcept;
    static pipeline_contract::StickPhase phase_for(
        StickState& state,
        pipeline_contract::Vec2f filtered) noexcept;

    IntentFilterConfig config_{};
    AxisState left_x_{};
    AxisState left_y_{};
    AxisState right_x_{};
    AxisState right_y_{};
    StickState left_stick_{};
    StickState right_stick_{};
};

}  // namespace controller_native
