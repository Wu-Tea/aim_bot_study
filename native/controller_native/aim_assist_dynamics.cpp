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

    const float raw_assist_x = input.assisted_right_x - input.manual_right_x;
    const float raw_assist_y = input.assisted_right_y - input.manual_right_y;
    const auto [manual_x, manual_y] = straighten_manual_curve(
        input.manual_right_x,
        input.manual_right_y,
        raw_assist_x,
        raw_assist_y);

    const bool recoil_guard_active =
        input.recoil_active || input.manual_fire_active || input.auto_fire_active;
    float guarded_assist_x = raw_assist_x;
    float guarded_assist_y = raw_assist_y;
    if (recoil_guard_active) {
        guarded_assist_x = has_last_raw_assist_
            ? guard_recoil_axis_jitter(raw_assist_x, last_raw_assist_x_, input.now_seconds)
            : raw_assist_x;
        guarded_assist_y = has_last_raw_assist_
            ? guard_recoil_axis_jitter(raw_assist_y, last_raw_assist_y_, input.now_seconds)
            : raw_assist_y;

        last_raw_assist_x_ = raw_assist_x;
        last_raw_assist_y_ = raw_assist_y;
        last_timestamp_seconds_ = input.now_seconds;
        has_last_raw_assist_ = true;
    } else {
        reset();
    }

    output.right_x = clamp_unit(manual_x + guarded_assist_x);
    output.right_y = clamp_unit(manual_y + guarded_assist_y);
    return output;
}

std::pair<float, float> NativeAimAssistDynamics::straighten_manual_curve(
    float manual_x,
    float manual_y,
    float assist_x,
    float assist_y) const {
    if (!config_.manual_curve_straighten_enabled) {
        return {manual_x, manual_y};
    }

    const float assist_mag = std::sqrt((assist_x * assist_x) + (assist_y * assist_y));
    const float manual_mag = std::sqrt((manual_x * manual_x) + (manual_y * manual_y));
    const float min_assist = std::max(0.0f, config_.manual_curve_straighten_min_assist) / 32767.0f;
    const float min_manual = std::max(0.0f, config_.manual_curve_straighten_min_manual) / 32767.0f;
    if (assist_mag < min_assist || manual_mag < min_manual || assist_mag <= 0.000001f) {
        return {manual_x, manual_y};
    }

    const float strength = std::max(0.0f, std::min(0.85f, config_.manual_curve_straighten_strength));
    if (strength <= 0.0f) {
        return {manual_x, manual_y};
    }

    const float ux = assist_x / assist_mag;
    const float uy = assist_y / assist_mag;
    const float parallel = (manual_x * ux) + (manual_y * uy);
    const float parallel_x = ux * parallel;
    const float parallel_y = uy * parallel;
    const float orthogonal_x = manual_x - parallel_x;
    const float orthogonal_y = manual_y - parallel_y;
    const float keep_orthogonal = 1.0f - strength;
    return {
        parallel_x + (orthogonal_x * keep_orthogonal),
        parallel_y + (orthogonal_y * keep_orthogonal)};
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
