#include "aim_assist_dynamics.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

int sign(float value) {
    if (value > 0.0f) {
        return 1;
    }
    if (value < 0.0f) {
        return -1;
    }
    return 0;
}

}  // namespace

NativeAimAssistDynamics::NativeAimAssistDynamics(GamepadAimAssistDynamicsConfig config)
    : config_(std::move(config)) {}

void NativeAimAssistDynamics::reset() {
    has_last_raw_assist_ = false;
    last_raw_assist_x_ = 0.0f;
    last_raw_assist_y_ = 0.0f;
    last_timestamp_seconds_ = 0.0;
}

NativeAimAssistDynamicsOutput NativeAimAssistDynamics::apply(
    const NativeAimAssistDynamicsInput& input) {
    NativeAimAssistDynamicsOutput output{input.assisted_right_x, input.assisted_right_y};
    if (!config_.enabled) {
        return output;
    }

    const bool recoil_guard_active =
        input.recoil_active || input.manual_fire_active || input.auto_fire_active;
    if (!recoil_guard_active) {
        reset();
        return output;
    }

    const float raw_assist_x = input.assisted_right_x - input.manual_right_x;
    const float raw_assist_y = input.assisted_right_y - input.manual_right_y;
    const float guarded_assist_x = has_last_raw_assist_
        ? guard_recoil_axis_jitter(raw_assist_x, last_raw_assist_x_, input.now_seconds)
        : raw_assist_x;
    const float guarded_assist_y = has_last_raw_assist_
        ? guard_recoil_axis_jitter(raw_assist_y, last_raw_assist_y_, input.now_seconds)
        : raw_assist_y;

    last_raw_assist_x_ = raw_assist_x;
    last_raw_assist_y_ = raw_assist_y;
    last_timestamp_seconds_ = input.now_seconds;
    has_last_raw_assist_ = true;

    output.right_x = clamp_unit(input.manual_right_x + guarded_assist_x);
    output.right_y = clamp_unit(input.manual_right_y + guarded_assist_y);
    return output;
}

float NativeAimAssistDynamics::guard_recoil_axis_jitter(
    float raw_assist,
    float previous_assist,
    double now_seconds) const {
    if (!config_.recoil_jitter_guard_enabled || !within_memory_window(now_seconds)) {
        return raw_assist;
    }
    const float threshold = std::max(0.0f, config_.recoil_jitter_assist_threshold) / 32767.0f;
    if (std::fabs(raw_assist) > threshold || std::fabs(previous_assist) > threshold) {
        return raw_assist;
    }
    if (sign(raw_assist) == 0 || sign(previous_assist) == 0) {
        return raw_assist;
    }
    if (sign(raw_assist) == sign(previous_assist)) {
        return raw_assist;
    }
    const float scale = std::max(0.0f, std::min(1.0f, config_.recoil_jitter_flip_scale));
    return raw_assist * scale;
}

bool NativeAimAssistDynamics::within_memory_window(double now_seconds) const {
    if (!has_last_raw_assist_ || last_timestamp_seconds_ <= 0.0 || now_seconds <= 0.0) {
        return false;
    }
    const double elapsed = now_seconds - last_timestamp_seconds_;
    return elapsed >= 0.0 &&
        elapsed <= static_cast<double>(std::max(0.0f, config_.recoil_jitter_memory_seconds));
}

}  // namespace controller_native
