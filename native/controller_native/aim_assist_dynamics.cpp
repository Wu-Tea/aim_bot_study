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

NativeAimAssistDynamics::NativeAimAssistDynamics(
    GamepadAimAssistDynamicsConfig config,
    float bodylock_damping)
    : config_(std::move(config)),
      bodylock_damping_(clamp(bodylock_damping, 0.0f, 0.95f)) {}

void NativeAimAssistDynamics::reset() {
    reset_envelope();
    reset_ads_crossing();
    assist_authority_was_absent_ = false;
    ads_reacquire_envelope_active_ = false;
    ads_handoff_assist_ = {};
    ads_handoff_track_id_ = 0;
    has_ads_handoff_assist_ = false;
}

void NativeAimAssistDynamics::observe_pre_recoil_output(
    common_native::Vec2f manual,
    common_native::Vec2f pre_recoil,
    bool ads_snap_active,
    std::uint64_t selected_track_id) {
    if (!ads_snap_active || !std::isfinite(pre_recoil.x) || !std::isfinite(pre_recoil.y) ||
        !std::isfinite(manual.x) || !std::isfinite(manual.y)) {
        return;
    }
    ads_handoff_assist_ = {
        clamp(pre_recoil.x - manual.x, -1.0f, 1.0f),
        clamp(pre_recoil.y - manual.y, -1.0f, 1.0f)};
    ads_handoff_track_id_ = selected_track_id != 0 ? selected_track_id : 1u;
    has_ads_handoff_assist_ = true;
}

bool NativeAimAssistDynamics::ads_handoff_assist(
    std::uint64_t selected_track_id,
    common_native::Vec2f* out_assist) const {
    const std::uint64_t target_key = selected_track_id != 0 ? selected_track_id : 1u;
    if (!has_ads_handoff_assist_ || ads_handoff_track_id_ != target_key) {
        return false;
    }
    if (out_assist != nullptr) {
        *out_assist = ads_handoff_assist_;
    }
    return true;
}

void NativeAimAssistDynamics::reset_envelope() {
    previous_assist_ = {};
    previous_delta_ = {};
    has_history_ = false;
    bodylock_history_active_ = false;
}

void NativeAimAssistDynamics::reset_ads_crossing() {
    ads_crossing_x_ = {};
    ads_crossing_y_ = {};
    ads_target_key_ = 0;
    ads_vision_sequence_ = 0;
}

NativeAimAssistDynamicsOutput NativeAimAssistDynamics::apply(
    const NativeAimAssistDynamicsInput& input) {
    if (!config_.enabled) {
        return {input.requested_assist, "disabled"};
    }

    const bool authority_absent =
        input.authority == pipeline_contract::AssistAuthorityState::Reject ||
        input.authority == pipeline_contract::AssistAuthorityState::TrackOnly;
    const bool release_bodylock = bodylock_history_active_ && has_history_ &&
        (input.lifecycle == pipeline_contract::BodylockLifecycleState::Yield ||
         input.lifecycle == pipeline_contract::BodylockLifecycleState::Inactive);
    if (release_bodylock) {
        reset_ads_crossing();
        const double dt = std::max(0.0005, std::min(0.004, input.dt_seconds));
        const float tick_scale = static_cast<float>(dt / 0.001);
        constexpr float kStepCapPerMs = 0.035f;
        constexpr float kJerkCapPerMs = 0.018f;
        const float step_cap = kStepCapPerMs * tick_scale;
        const float jerk_cap = kJerkCapPerMs * tick_scale;
        common_native::Vec2f release_previous_delta = previous_delta_;
        if (previous_assist_.x > 0.0f) {
            release_previous_delta.x = std::min(0.0f, release_previous_delta.x);
        } else if (previous_assist_.x < 0.0f) {
            release_previous_delta.x = std::max(0.0f, release_previous_delta.x);
        }
        if (previous_assist_.y > 0.0f) {
            release_previous_delta.y = std::min(0.0f, release_previous_delta.y);
        } else if (previous_assist_.y < 0.0f) {
            release_previous_delta.y = std::max(0.0f, release_previous_delta.y);
        }
        common_native::Vec2f delta;
        NativeAimAssistDynamicsOutput output;
        output.assist.x = shape_axis(
            0.0f,
            previous_assist_.x,
            release_previous_delta.x,
            step_cap,
            jerk_cap,
            &delta.x);
        output.assist.y = shape_axis(
            0.0f,
            previous_assist_.y,
            release_previous_delta.y,
            step_cap,
            jerk_cap,
            &delta.y);
        output.limit_reason = "bodylock_release";
        if (std::fabs(output.assist.x) <= 0.000001f &&
            std::fabs(output.assist.y) <= 0.000001f) {
            reset_envelope();
            output.assist = {};
            return output;
        }
        previous_assist_ = output.assist;
        previous_delta_ = delta;
        return output;
    }
    if (authority_absent ||
        input.lifecycle == pipeline_contract::BodylockLifecycleState::Yield) {
        reset();
        assist_authority_was_absent_ = authority_absent;
        return {{}, authority_absent ? "no_authority" : "yield"};
    }

    if (input.ads_snap_active) {
        observe_ads_snap_crossing(input);
        if (assist_authority_was_absent_) {
            reset_envelope();
            assist_authority_was_absent_ = false;
            ads_reacquire_envelope_active_ = true;
        }
        if (!strong_opposing_manual(input)) {
            if (ads_reacquire_envelope_active_) {
                return shape_ads_reacquire(input);
            }
            reset_envelope();
            return {input.requested_assist, "ads_passthrough"};
        }

        NativeAimAssistDynamicsOutput output;
        const bool x_brake = ads_axis_brake_active(
            ads_crossing_x_,
            input.manual.x,
            input.requested_assist.x,
            input.target_error_px.x,
            input.now_seconds);
        const bool y_brake = ads_axis_brake_active(
            ads_crossing_y_,
            input.manual.y,
            input.requested_assist.y,
            input.target_error_px.y,
            input.now_seconds);
        if (!x_brake && !y_brake) {
            return {{}, "manual_yield"};
        }
        output.assist = input.requested_assist;
        if (input.manual.x * input.requested_assist.x < 0.0f && !x_brake) {
            output.assist.x = 0.0f;
        }
        if (input.manual.y * input.requested_assist.y < 0.0f && !y_brake) {
            output.assist.y = 0.0f;
        }
        output.limit_reason = "ads_crossing_brake";
        return output;
    }

    assist_authority_was_absent_ = false;
    ads_reacquire_envelope_active_ = false;
    reset_ads_crossing();

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

    if (input.lifecycle == pipeline_contract::BodylockLifecycleState::Coast &&
        has_history_ && bodylock_history_active_ &&
        std::hypot(input.requested_assist.x, input.requested_assist.y) <= 0.000001f) {
        const double dt = std::max(0.0005, std::min(0.004, input.dt_seconds));
        constexpr double kCoastTimeConstantSeconds = 0.080;
        const float decay = static_cast<float>(std::exp(-dt / kCoastTimeConstantSeconds));
        NativeAimAssistDynamicsOutput output;
        output.assist = {
            previous_assist_.x * decay,
            previous_assist_.y * decay};
        previous_delta_ = {
            output.assist.x - previous_assist_.x,
            output.assist.y - previous_assist_.y};
        previous_assist_ = output.assist;
        output.limit_reason = "bodylock_coast";
        return output;
    }

    const std::uint64_t target_key = input.selected_track_id != 0
        ? input.selected_track_id
        : 1u;
    bool seeded_from_ads = false;
    if (!has_history_ && has_ads_handoff_assist_) {
        if (ads_handoff_track_id_ == target_key) {
            previous_assist_ = ads_handoff_assist_;
            previous_delta_ = {};
            has_history_ = true;
            seeded_from_ads = true;
        }
        has_ads_handoff_assist_ = false;
    }

    const double dt = std::max(0.0005, std::min(0.004, input.dt_seconds));
    const float tick_scale = seeded_from_ads
        ? 1.0f
        : static_cast<float>(dt / 0.001);
    constexpr float kStepCapPerMs = 0.035f;
    constexpr float kJerkCapPerMs = 0.018f;
    const float step_cap = kStepCapPerMs * tick_scale;
    const float jerk_cap = kJerkCapPerMs * tick_scale;

    const common_native::Vec2f previous = has_history_ ? previous_assist_ : common_native::Vec2f{};
    const common_native::Vec2f previous_delta =
        has_history_ ? previous_delta_ : common_native::Vec2f{};
    common_native::Vec2f delta;
    NativeAimAssistDynamicsOutput output;
    const common_native::Vec2f delivery_target = {
        (previous.x * bodylock_damping_) +
            (input.requested_assist.x * (1.0f - bodylock_damping_)),
        (previous.y * bodylock_damping_) +
            (input.requested_assist.y * (1.0f - bodylock_damping_))};
    output.assist.x = shape_axis(
        delivery_target.x,
        previous.x,
        previous_delta.x,
        step_cap,
        jerk_cap,
        &delta.x);
    output.assist.y = shape_axis(
        delivery_target.y,
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
    bodylock_history_active_ = true;
    return output;
}

NativeAimAssistDynamicsOutput NativeAimAssistDynamics::shape_ads_reacquire(
    const NativeAimAssistDynamicsInput& input) {
    const double dt = std::max(0.0005, std::min(0.004, input.dt_seconds));
    const float tick_scale = static_cast<float>(dt / 0.001);
    constexpr float kStepCapPerMs = 0.035f;
    constexpr float kJerkCapPerMs = 0.018f;
    const float step_cap = kStepCapPerMs * tick_scale;
    const float jerk_cap = kJerkCapPerMs * tick_scale;
    const common_native::Vec2f previous =
        has_history_ ? previous_assist_ : common_native::Vec2f{};
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
    output.limit_reason = "assist_reacquire_envelope";
    previous_assist_ = output.assist;
    previous_delta_ = delta;
    has_history_ = true;
    bodylock_history_active_ = false;
    if (std::fabs(output.assist.x - input.requested_assist.x) <= 0.000001f &&
        std::fabs(output.assist.y - input.requested_assist.y) <= 0.000001f) {
        ads_reacquire_envelope_active_ = false;
    }
    return output;
}

void NativeAimAssistDynamics::observe_ads_snap_crossing(
    const NativeAimAssistDynamicsInput& input) {
    const std::uint64_t target_key = input.selected_track_id != 0
        ? input.selected_track_id
        : 1u;
    if (ads_target_key_ != 0 && ads_target_key_ != target_key) {
        reset_ads_crossing();
    }
    ads_target_key_ = target_key;

    if (!input.fresh_observation ||
        (input.vision_sequence != 0 && input.vision_sequence == ads_vision_sequence_)) {
        return;
    }
    ads_vision_sequence_ = input.vision_sequence;

    constexpr float kErrorDeadzonePx = 0.25f;
    constexpr float kCrossingEvidenceLimitPx = 48.0f;
    constexpr float kManualEvidence = 0.18f;
    constexpr double kBrakeWindowSeconds = 0.050;
    const auto observe_axis = [&](AdsAxisCrossingState& state, float error, float manual) {
        const bool previous_valid = state.has_previous_error &&
            std::fabs(state.previous_error) > kErrorDeadzonePx;
        const bool current_valid = std::fabs(error) > kErrorDeadzonePx;
        const bool crossed = previous_valid && current_valid &&
            state.previous_error * error < 0.0f &&
            std::max(std::fabs(state.previous_error), std::fabs(error)) <=
                kCrossingEvidenceLimitPx;
        const bool manual_carried_through = crossed &&
            state.has_previous_manual &&
            std::fabs(state.previous_manual) >= kManualEvidence &&
            std::fabs(manual) >= kManualEvidence &&
            state.previous_manual * manual > 0.0f &&
            state.previous_manual * state.previous_error > 0.0f &&
            manual * state.previous_error > 0.0f &&
            manual * error < 0.0f;
        if (manual_carried_through) {
            state.brake_until_seconds = input.now_seconds + kBrakeWindowSeconds;
        }
        if (current_valid) {
            state.previous_error = error;
            state.has_previous_error = true;
        }
        state.previous_manual = manual;
        state.has_previous_manual = true;
    };

    observe_axis(ads_crossing_x_, input.target_error_px.x, input.manual.x);
    observe_axis(ads_crossing_y_, input.target_error_px.y, input.manual.y);
}

bool NativeAimAssistDynamics::ads_crossing_brake_pending(double now_seconds) const {
    return (ads_crossing_x_.brake_until_seconds > now_seconds) ||
        (ads_crossing_y_.brake_until_seconds > now_seconds);
}

bool NativeAimAssistDynamics::bodylock_envelope_active() const {
    return bodylock_history_active_ && has_history_;
}

bool NativeAimAssistDynamics::ads_axis_brake_active(
    const AdsAxisCrossingState& state,
    float manual,
    float requested_assist,
    float target_error,
    double now_seconds) const {
    if (state.brake_until_seconds <= 0.0 || now_seconds > state.brake_until_seconds) {
        return false;
    }
    return manual * requested_assist < 0.0f &&
        requested_assist * target_error > 0.0f;
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
