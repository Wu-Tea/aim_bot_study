#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct AimDynamicsShaperConfig {
    float rise_slew_per_second = 64.0f;
    float decay_slew_per_second = 24.0f;
    float max_step_per_tick = 0.08f;
    float opposing_manual_scale = 0.0f;
    float cooperative_manual_scale = 0.65f;
};

class AimDynamicsShaper {
public:
    explicit AimDynamicsShaper(AimDynamicsShaperConfig config = {});

    pipeline_contract::Vec2f shape(
        pipeline_contract::Vec2f requested_ai,
        const pipeline_contract::IntentState& intent,
        const pipeline_contract::TargetPlan& plan,
        float dt_seconds,
        pipeline_contract::Vec2f confirmed_wrong_axis = {}) noexcept;

    pipeline_contract::Vec2f current() const noexcept;
    void adopt(pipeline_contract::Vec2f output) noexcept;
    void reset() noexcept;

private:
    float shape_axis(float requested, float manual, float manual_confidence, float dt) noexcept;

    AimDynamicsShaperConfig config_{};
    pipeline_contract::Vec2f current_{};
};

}  // namespace controller_native
