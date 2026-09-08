#pragma once

#include "mouse_native/mouse_control_types.h"

#include <cstdint>

namespace mouse_native {

struct MouseActuatorAdapterConfig {
    std::int32_t maximum_abs_counts_per_report = 32'767;
    float minimum_dt_seconds = 0.0001f;
    float maximum_dt_seconds = 0.0500f;
};

class MouseActuatorAdapter {
public:
    explicit MouseActuatorAdapter(MouseActuatorAdapterConfig config = {});

    MouseActuationResult adapt(
        float final_u_x,
        float final_u_y,
        float dt_seconds,
        const MouseResponseProfile& profile) noexcept;

    MouseActuationResult passthrough(MouseSourceCounts source) noexcept;
    MouseActuationResult adapt_with_native_axes(
        float final_u_x, float final_u_y, float dt_seconds,
        const MouseResponseProfile& profile, MouseSourceCounts source,
        bool native_x, bool native_y) noexcept;
    void reset() noexcept;

    double residual_x() const noexcept;
    double residual_y() const noexcept;

private:
    MouseActuatorAdapterConfig config_{};
    double residual_x_ = 0.0;
    double residual_y_ = 0.0;
    std::uint64_t profile_generation_ = 0;
};

}  // namespace mouse_native
