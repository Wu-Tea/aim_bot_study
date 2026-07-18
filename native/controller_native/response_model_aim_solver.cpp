#include "response_model_aim_solver.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

ResponseModelAimOutput solve_response_model_aim(
    const ResponseModelAimRequest& request) noexcept {
    ResponseModelAimOutput output;
    const float response = std::max(
        1.0f, std::fabs(request.response_px_per_stick_second));
    const float horizon = std::clamp(
        request.arrival_horizon_seconds, 0.005f, 1.0f);
    const float horizon_y = std::clamp(
        request.arrival_horizon_y_seconds > 0.0f
            ? request.arrival_horizon_y_seconds
            : request.arrival_horizon_seconds,
        0.005f, 1.0f);
    const float authority = std::clamp(request.authority, 0.0f, 1.0f);
    const float motion_weight = std::clamp(request.motion_weight, 0.0f, 2.0f);
    const pipeline_contract::Vec2f control_error{
        request.error_px.x, -request.error_px.y};
    const pipeline_contract::Vec2f control_velocity{
        request.relative_velocity_px_per_sec.x,
        -request.relative_velocity_px_per_sec.y};
    output.position_stick = {
        control_error.x / (horizon * response),
        control_error.y / (horizon_y * response),
    };
    output.motion_stick = {
        control_velocity.x / response * motion_weight,
        control_velocity.y / response * motion_weight,
    };
    output.unclamped_stick = {
        (output.position_stick.x + output.motion_stick.x) * authority,
        (output.position_stick.y + output.motion_stick.y) * authority,
    };

    const float max_x = std::max(0.0f, request.max_force.x);
    const float max_y = std::max(0.0f, request.max_force.y);
    if (max_x <= 0.0f) output.unclamped_stick.x = 0.0f;
    if (max_y <= 0.0f) output.unclamped_stick.y = 0.0f;
    const float normalized_x = max_x > 0.0f
        ? output.unclamped_stick.x / max_x : 0.0f;
    const float normalized_y = max_y > 0.0f
        ? output.unclamped_stick.y / max_y : 0.0f;
    const float ellipse_length = std::hypot(normalized_x, normalized_y);
    const float vector_scale = ellipse_length > 1.0f
        ? 1.0f / ellipse_length : 1.0f;
    output.stick = {
        output.unclamped_stick.x * vector_scale,
        output.unclamped_stick.y * vector_scale,
    };
    output.limited = ellipse_length > 1.0f;
    return output;
}

}  // namespace controller_native
