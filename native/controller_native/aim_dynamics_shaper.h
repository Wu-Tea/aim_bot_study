#pragma once

#include "pipeline_contract/target_plan.h"
#include "aim_response_curve_plugin.h"

#include <cstdint>

namespace controller_native {

struct AimDynamicsShaperConfig {
    float rise_slew_per_second = 64.0f;
    float decay_slew_per_second = 48.0f;
    float max_step_per_tick = 0.08f;
    // Optional BodyLock temporal ramp, used by calibrated mouse adapters.
    // Durations describe zero <-> configured per-axis maximum force. Zero
    // retains the ordinary controller envelope for that direction of change.
    float bodylock_accel_ms = 0.0f;
    float bodylock_decel_ms = 0.0f;
    pipeline_contract::Vec2f bodylock_max_force{};
    float bodylock_authority_budget_scale = 1.0f;
};

class AimDynamicsShaper {
public:
    explicit AimDynamicsShaper(AimDynamicsShaperConfig config = {});

    pipeline_contract::Vec2f shape(
        pipeline_contract::Vec2f requested_ai,
        const pipeline_contract::TargetPlan& plan,
        float dt_seconds) noexcept;

    pipeline_contract::Vec2f current() const noexcept;
    // Arbitration may hand authority back faster than the already-shaped AI
    // changes. Bound only that additional AI share, leaving raw manual motion
    // and immediate authority revocation outside the temporal envelope.
    pipeline_contract::Vec2f shape_ownership(
        pipeline_contract::Vec2f authorized_output,
        pipeline_contract::Vec2f manual,
        pipeline_contract::Vec2f shaped_ai,
        float dt_seconds,
        bool active,
        bool target_changed) noexcept;
    pipeline_contract::Vec2f stopping_work(
        pipeline_contract::Vec2f sustaining,
        const AimResponseCurveConfig& curve) const noexcept;
    void adopt(pipeline_contract::Vec2f output) noexcept;
    void reset() noexcept;

private:
    AimDynamicsShaperConfig config_{};
    pipeline_contract::Vec2f current_{};
    std::uint64_t previous_target_id_ = 0;
    pipeline_contract::ControlMode previous_mode_ =
        pipeline_contract::ControlMode::Manual;
    bool context_initialized_ = false;
    pipeline_contract::Vec2f ai_share_{};
    bool ownership_initialized_ = false;
};

}  // namespace controller_native
