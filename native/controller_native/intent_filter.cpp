#include "intent_filter.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

IntentFilter::IntentFilter(IntentFilterConfig config)
    : config_(config) {}

pipeline_contract::AxisIntentState IntentFilter::update_axis(
    AxisState& state,
    float raw) noexcept {
    if (std::fabs(raw) <= config_.neutral_learning_limit) {
        // Enter calibration continuously. A hard on/off learning boundary
        // changed bias/noise abruptly during a slow physical release, turning
        // the otherwise continuous axis activity into an output step.
        const float learning_position = std::clamp(std::fabs(raw) /
            std::max(config_.neutral_learning_limit, 1.0e-6f), 0.0f, 1.0f);
        const float learning_weight = 1.0f - learning_position;
        state.bias += config_.bias_alpha * learning_weight * (raw - state.bias);
        const float residual = std::fabs(raw - state.bias);
        state.noise += config_.noise_alpha * learning_weight * (residual - state.noise);
    }

    const float centered = raw - state.bias;
    const float threshold = std::max(
        config_.base_deadzone,
        state.noise * config_.noise_multiplier + config_.noise_margin);
    const float magnitude = std::fabs(centered);
    const float filtered = magnitude <= threshold ? 0.0f : centered;
    const float confidence = filtered == 0.0f
        ? 0.0f
        : std::clamp((magnitude - threshold) / 0.5f, 0.0f, 1.0f);
    const float activity_fraction = std::clamp(
        (magnitude - threshold) / std::max(threshold, 1.0e-6f), 0.0f, 1.0f);
    const float activity = activity_fraction * activity_fraction *
        (3.0f - 2.0f * activity_fraction);
    return pipeline_contract::AxisIntentState{
        raw,
        filtered,
        state.bias,
        state.noise,
        confidence,
        activity,
    };
}

pipeline_contract::StickPhase IntentFilter::phase_for(
    StickState& state,
    pipeline_contract::Vec2f filtered) noexcept {
    const bool active = std::hypot(filtered.x, filtered.y) > 0.0f;
    pipeline_contract::StickPhase phase = pipeline_contract::StickPhase::Neutral;
    if (active && !state.active) {
        phase = pipeline_contract::StickPhase::Onset;
    } else if (
        active && state.active &&
        filtered.x * state.filtered.x + filtered.y * state.filtered.y < 0.0f) {
        phase = pipeline_contract::StickPhase::Reversal;
    } else if (active) {
        phase = pipeline_contract::StickPhase::Sustained;
    } else if (state.active) {
        phase = pipeline_contract::StickPhase::Release;
    }
    state.active = active;
    if (active) state.filtered = filtered;
    return phase;
}

pipeline_contract::IntentState IntentFilter::update(
    pipeline_contract::Vec2f raw_left,
    pipeline_contract::Vec2f raw_right,
    bool ads,
    bool fire,
    double sample_time_seconds,
    bool target_owned,
    bool handover_requested) noexcept {
    pipeline_contract::IntentState result{};
    result.raw_left = raw_left;
    result.raw_right = raw_right;
    result.left_x = update_axis(left_x_, raw_left.x);
    result.left_y = update_axis(left_y_, raw_left.y);
    result.right_x = update_axis(right_x_, raw_right.x);
    result.right_y = update_axis(right_y_, raw_right.y);
    result.filtered_left = {result.left_x.filtered, result.left_y.filtered};
    result.filtered_right = {result.right_x.filtered, result.right_y.filtered};
    result.left_phase = phase_for(left_stick_, result.filtered_left);
    result.right_phase = phase_for(right_stick_, result.filtered_right);
    if (!ads) {
        right_purpose_ = pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    } else if (handover_requested) {
        right_purpose_ = pipeline_contract::UserAimIntentPurpose::HandoverTarget;
    } else if (!target_owned) {
        // Purpose is scoped to the owned target. A held correction cannot be
        // carried across target loss and silently rewrite the next target's D.
        right_purpose_ = pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    } else if (
        result.right_phase == pipeline_contract::StickPhase::Onset ||
        result.right_phase == pipeline_contract::StickPhase::Reversal) {
        right_purpose_ = target_owned
            ? pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget
            : pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    }
    result.right_purpose = right_purpose_;
    result.left_confidence = std::max(result.left_x.confidence, result.left_y.confidence);
    result.right_confidence = std::max(result.right_x.confidence, result.right_y.confidence);
    result.sample_time_seconds = sample_time_seconds;
    result.ads = ads;
    result.fire = fire;
    return result;
}

void IntentFilter::reset() noexcept {
    left_x_ = {};
    left_y_ = {};
    right_x_ = {};
    right_y_ = {};
    left_stick_ = {};
    right_stick_ = {};
    right_purpose_ = pipeline_contract::UserAimIntentPurpose::AcquireTarget;
}

}  // namespace controller_native
