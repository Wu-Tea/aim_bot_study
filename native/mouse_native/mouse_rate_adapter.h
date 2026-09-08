#pragma once

#include "mouse_native/mouse_control_types.h"

namespace mouse_native {

struct MouseRateAdapterConfig {
    float maximum_abs_normalized = 1.0f;
    float minimum_dt_seconds = 0.0001f;
    float maximum_dt_seconds = 0.0500f;
};

class MouseRateAdapter {
public:
    explicit MouseRateAdapter(MouseRateAdapterConfig config = {});

    MouseNormalizedInput adapt(
        MouseSourceCounts source,
        float dt_seconds,
        const MouseResponseProfile& profile) const noexcept;

private:
    MouseRateAdapterConfig config_{};
};

}  // namespace mouse_native
