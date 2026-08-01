#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"

#include <cstdint>

namespace controller_native {

struct AimDynamicsShaperConfig {
    float rise_slew_per_second = 64.0f;
    float decay_slew_per_second = 48.0f;
    float max_step_per_tick = 0.08f;
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
    AimDynamicsShaperConfig config_{};
    pipeline_contract::Vec2f current_{};
    std::uint64_t previous_target_id_ = 0;
    pipeline_contract::ControlMode previous_mode_ =
        pipeline_contract::ControlMode::Manual;
    bool context_initialized_ = false;
};

}  // namespace controller_native
