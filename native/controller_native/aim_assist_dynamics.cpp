#include "aim_assist_dynamics.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

float clamp(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

bool bodylock_owned(pipeline_contract::BodylockLifecycleState state) {
    return state == pipeline_contract::BodylockLifecycleState::Warm ||
        state == pipeline_contract::BodylockLifecycleState::Tracking ||
        state == pipeline_contract::BodylockLifecycleState::Coast;
}

}  // namespace

NativeAimAssistDynamics::NativeAimAssistDynamics(GamepadAimAssistDynamicsConfig config)
    : config_(std::move(config)) {}

void NativeAimAssistDynamics::reset() {
    previous_assist_ = {};
    previous_delta_ = {};
    has_history_ = false;
}

NativeAimAssistDynamicsOutput NativeAimAssistDynamics::apply(
    const NativeAimAssistDynamicsInput& input) {
    if (!config_.enabled) {
        return {input.requested_assist, "disabled"};
    }

    const bool authority_absent =
        input.authority == pipeline_contract::AssistAuthorityState::Reject ||
        input.authority == pipeline_contract::AssistAuthorityState::TrackOnly;
    if (authority_absent ||
        input.lifecycle == pipeline_contract::BodylockLifecycleState::Yield) {
        reset();
        return {{}, authority_absent ? "no_authority" : "yield"};
    }

    if (strong_opposing_manual(input)) {
        reset();
        return {{}, "manual_yield"};
    }

    // ADS and other modes retain their existing force curve after the global
    // current-tick user-yield invariant. The stateful envelope itself owns
    // bodylock assist only; it never shapes manual or recoil.
    if (!bodylock_owned(input.lifecycle)) {
        reset();
        return {input.requested_assist, "non_bodylock_passthrough"};
    }

    const double dt = std::max(0.0005, std::min(0.004, input.dt_seconds));
    const float tick_scale = static_cast<float>(dt / 0.001);
    const float error_radius = std::hypot(input.target_error_px.x, input.target_error_px.y);
    const bool boundary_limited =
        input.lifecycle == pipeline_contract::BodylockLifecycleState::Warm ||
        input.lifecycle == pipeline_contract::BodylockLifecycleState::Coast;
    const float nominal_step = (!boundary_limited && error_radius > 48.0f) ? 0.18f : 0.10f;
    const float step_cap = nominal_step * tick_scale;
    const float jerk_cap = 0.10f * tick_scale;

    const common_native::Vec2f previous = has_history_ ? previous_assist_ : common_native::Vec2f{};
    const common_native::Vec2f previous_delta =
        has_history_ ? previous_delta_ : common_native::Vec2f{};
    common_native::Vec2f delta;
    NativeAimAssistDynamicsOutput output;
    output.assist.x = shape_axis(
        input.requested_assist.x,
        previous.x,
        previous_delta.x,
        step_cap,
        jerk_cap,
        &delta.x);
    output.assist.y = shape_axis(
        input.requested_assist.y,
        previous.y,
        previous_delta.y,
        step_cap,
        jerk_cap,
        &delta.y);
    output.limit_reason =
        std::fabs(output.assist.x - input.requested_assist.x) > 0.000001f ||
            std::fabs(output.assist.y - input.requested_assist.y) > 0.000001f
        ? "assist_envelope"
        : "none";
    previous_assist_ = output.assist;
    previous_delta_ = delta;
    has_history_ = true;
    return output;
}

float NativeAimAssistDynamics::shape_axis(
    float requested,
    float previous,
    float previous_delta,
    float step_cap,
    float jerk_cap,
    float* out_delta) const {
    float target = requested;
    if (previous * requested < 0.0f) {
        target = 0.0f;
    }
    const float requested_delta = clamp(target - previous, -step_cap, step_cap);
    const float delta = clamp(
        requested_delta,
        previous_delta - jerk_cap,
        previous_delta + jerk_cap);
    float shaped = previous + delta;
    if ((target - previous) * (target - shaped) < 0.0f) {
        shaped = target;
    }
    if (out_delta != nullptr) {
        *out_delta = shaped - previous;
    }
    return shaped;
}

bool NativeAimAssistDynamics::strong_opposing_manual(
    const NativeAimAssistDynamicsInput& input) const {
    const float manual_magnitude = std::hypot(input.manual.x, input.manual.y);
    const float assist_magnitude = std::hypot(
        input.requested_assist.x,
        input.requested_assist.y);
    if (manual_magnitude < 0.55f || assist_magnitude < 0.02f) {
        return false;
    }
    const float alignment =
        ((input.manual.x * input.requested_assist.x) +
         (input.manual.y * input.requested_assist.y)) /
        (manual_magnitude * assist_magnitude);
    return alignment <= -0.35f;
}

}  // namespace controller_native
