#pragma once

#include "../common_native/screen_geometry.h"
#include "../pipeline_contract/assist_authority.h"

#include "runtime_config.h"

namespace controller_native {

struct NativeAimAssistDynamicsInput {
    common_native::Vec2f manual;
    common_native::Vec2f requested_assist;
    pipeline_contract::AssistAuthorityState authority =
        pipeline_contract::AssistAuthorityState::Reject;
    pipeline_contract::BodylockLifecycleState lifecycle =
        pipeline_contract::BodylockLifecycleState::Inactive;
    common_native::Vec2f target_error_px;
    double position_sigma = 0.0;
    double dt_seconds = 0.001;
    double now_seconds = 0.0;
};

struct NativeAimAssistDynamicsOutput {
    common_native::Vec2f assist;
    const char* limit_reason = "none";
};

class NativeAimAssistDynamics {
public:
    explicit NativeAimAssistDynamics(GamepadAimAssistDynamicsConfig config = {});

    void reset();
    [[nodiscard]] NativeAimAssistDynamicsOutput apply(
        const NativeAimAssistDynamicsInput& input);

private:
    [[nodiscard]] float shape_axis(
        float requested,
        float previous,
        float previous_delta,
        float step_cap,
        float jerk_cap,
        float* out_delta) const;
    [[nodiscard]] bool strong_opposing_manual(
        const NativeAimAssistDynamicsInput& input) const;

    GamepadAimAssistDynamicsConfig config_;
    common_native::Vec2f previous_assist_;
    common_native::Vec2f previous_delta_;
    bool has_history_ = false;
};

}  // namespace controller_native
