#pragma once

#include "pipeline_contract/target_plan.h"

namespace controller_native {

struct ResponseModelAimRequest {
    pipeline_contract::Vec2f error_px{};
    pipeline_contract::Vec2f relative_velocity_px_per_sec{};
    float response_px_per_stick_second = 500.0f;
    float arrival_horizon_seconds = 0.050f;
    float arrival_horizon_y_seconds = 0.0f;
    float motion_weight = 1.0f;
    pipeline_contract::Vec2f max_force{1.0f, 1.0f};
    float authority = 1.0f;
};

struct ResponseModelAimOutput {
    pipeline_contract::Vec2f position_stick{};
    pipeline_contract::Vec2f motion_stick{};
    pipeline_contract::Vec2f unclamped_stick{};
    pipeline_contract::Vec2f stick{};
    bool limited = false;
};

ResponseModelAimOutput solve_response_model_aim(
    const ResponseModelAimRequest& request) noexcept;

}  // namespace controller_native
