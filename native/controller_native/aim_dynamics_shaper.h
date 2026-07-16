#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct AimDynamicsShaperConfig {
    float rise_slew_per_second = 8.0f;
    float decay_slew_per_second = 5.0f;
    float opposing_manual_scale = 0.2f;
};

class AimDynamicsShaper {
public:
    explicit AimDynamicsShaper(AimDynamicsShaperConfig config = {});

    pipeline_contract::Vec2f shape(
        pipeline_contract::Vec2f requested_ai,
        const pipeline_contract::IntentState& intent,
        const pipeline_contract::TargetPlan& plan,
        float dt_seconds) noexcept;

    pipeline_contract::Vec2f current() const noexcept;
    void reset() noexcept;

private:
    float shape_axis(float requested, float manual, float manual_confidence, float dt) noexcept;

    AimDynamicsShaperConfig config_{};
    pipeline_contract::Vec2f current_{};
};

}  // namespace controller_native
