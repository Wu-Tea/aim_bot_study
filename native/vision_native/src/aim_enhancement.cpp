#include "vision_native/aim_enhancement.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

namespace vision_native {
namespace {

constexpr float kLeadSeconds = 0.05f;
constexpr float kLeadGain = 0.85f;
constexpr float kMaxLeadPx = 10.0f;
constexpr float kMinMotionPx = 2.0f;
constexpr int kLeadConsistentFrames = 2;
constexpr int kCatchupTriggerFrames = 2;
constexpr float kCatchupGainPerFrame = 0.12f;
constexpr float kCatchupMaxBonus = 0.35f;
constexpr float kCatchupDecay = 0.12f;
constexpr float kCatchupConvergenceEpsilonPx = 0.25f;
constexpr float kDampingInnerRadius = 4.0f;
constexpr float kDampingOuterRadius = 28.0f;
constexpr float kDampingMinScale = 0.65f;
constexpr float kDampingConvergenceEpsilonPx = 0.25f;
constexpr float kVelocityFilterAlpha = 0.45f;

float sign_of(float value) {
    return value < 0.0f ? -1.0f : 1.0f;
}

} // namespace

void AimEnhancementPipeline::reset() {
    previous_target_.reset();
    previous_timestamp_.reset();
    velocity_x_ = 0.0f;
    velocity_y_ = 0.0f;
    lead_x_streak_ = 0;
    lead_y_streak_ = 0;
    lead_x_sign_ = 0.0f;
    lead_y_sign_ = 0.0f;
    catchup_x_growth_frames_ = 0;
    catchup_y_growth_frames_ = 0;
    catchup_x_bonus_ = 0.0f;
    catchup_y_bonus_ = 0.0f;
}

bool AimEnhancementPipeline::is_predicted(const VisionResult& target) {
    return std::string(target.target_source) == "predicted";
}

float AimEnhancementPipeline::clamp_axis_lead(float current_error, float lead, float max_lead) {
    if (current_error == 0.0f || lead == 0.0f) {
        return 0.0f;
    }

    const float bounded = std::max(-max_lead, std::min(max_lead, lead));
    if (sign_of(bounded) != sign_of(current_error)) {
        return 0.0f;
    }
    return bounded;
}

bool AimEnhancementPipeline::is_crosshair_in_slow_zone(const AimState& state) {
    if (!state.slow_zone.has_value()) {
        return true;
    }

    const AimSlowZone& zone = *state.slow_zone;
    return zone.left <= state.target.screen_center_x
        && state.target.screen_center_x <= zone.right
        && zone.top <= state.target.screen_center_y
        && state.target.screen_center_y <= zone.bottom;
}

float AimEnhancementPipeline::damping_scale_for_distance(float distance) {
    if (kDampingMinScale >= 1.0f || kDampingOuterRadius <= kDampingInnerRadius) {
        return 1.0f;
    }
    if (distance <= kDampingInnerRadius) {
        return kDampingMinScale;
    }
    if (distance >= kDampingOuterRadius) {
        return 1.0f;
    }

    const float progress = (distance - kDampingInnerRadius) / (kDampingOuterRadius - kDampingInnerRadius);
    return kDampingMinScale + ((1.0f - kDampingMinScale) * progress);
}

bool AimEnhancementPipeline::is_converging(const AimState& state) {
    if (!state.previous_dx.has_value() || !state.previous_dy.has_value()) {
        return false;
    }

    const float current_distance = std::hypot(state.target.dx, state.target.dy);
    const float previous_distance = std::hypot(*state.previous_dx, *state.previous_dy);
    return current_distance < (previous_distance - kDampingConvergenceEpsilonPx);
}

std::pair<int, float> AimEnhancementPipeline::update_axis_streak(
    float motion,
    int streak,
    float last_sign) const {
    if (std::fabs(motion) < kMinMotionPx) {
        return {0, 0.0f};
    }

    const float sign = sign_of(motion);
    if (sign == last_sign) {
        return {streak + 1, sign};
    }
    return {1, sign};
}

void AimEnhancementPipeline::apply_lead(AimState& state) {
    if (state.dt <= 0.0 || kLeadSeconds <= 0.0f || kLeadGain <= 0.0f) {
        return;
    }

    std::tie(lead_x_streak_, lead_x_sign_) =
        update_axis_streak(state.motion_x, lead_x_streak_, lead_x_sign_);
    std::tie(lead_y_streak_, lead_y_sign_) =
        update_axis_streak(state.motion_y, lead_y_streak_, lead_y_sign_);

    float lead_x = 0.0f;
    float lead_y = 0.0f;
    if (lead_x_streak_ >= kLeadConsistentFrames) {
        lead_x = state.velocity_x * kLeadSeconds * kLeadGain;
    }
    if (lead_y_streak_ >= kLeadConsistentFrames) {
        lead_y = state.velocity_y * kLeadSeconds * kLeadGain;
    }

    state.output_dx += clamp_axis_lead(state.target.dx, lead_x, kMaxLeadPx);
    state.output_dy += clamp_axis_lead(state.target.dy, lead_y, kMaxLeadPx);
}

std::pair<int, float> AimEnhancementPipeline::update_axis_bonus(
    float current_error,
    const std::optional<float>& previous_error,
    int growth_frames,
    float bonus) const {
    if (!previous_error.has_value()) {
        return {0, std::max(0.0f, bonus - kCatchupDecay)};
    }

    const float current_mag = std::fabs(current_error);
    const float previous_mag = std::fabs(*previous_error);
    if (current_mag <= kCatchupConvergenceEpsilonPx) {
        return {0, std::max(0.0f, bonus - kCatchupDecay)};
    }

    bool same_direction = false;
    if (current_error == 0.0f || *previous_error == 0.0f) {
        same_direction = current_error == *previous_error;
    } else {
        same_direction = sign_of(current_error) == sign_of(*previous_error);
    }
    if (!same_direction) {
        return {0, 0.0f};
    }

    if (current_mag > previous_mag + kCatchupConvergenceEpsilonPx) {
        growth_frames += 1;
        if (growth_frames >= kCatchupTriggerFrames) {
            bonus = std::min(kCatchupMaxBonus, bonus + kCatchupGainPerFrame);
        }
        return {growth_frames, bonus};
    }

    if (current_mag < previous_mag - kCatchupConvergenceEpsilonPx) {
        bonus = std::max(0.0f, bonus - kCatchupDecay);
    }
    return {0, bonus};
}

void AimEnhancementPipeline::apply_catchup(AimState& state) {
    std::tie(catchup_x_growth_frames_, catchup_x_bonus_) = update_axis_bonus(
        state.target.dx,
        state.previous_dx,
        catchup_x_growth_frames_,
        catchup_x_bonus_);
    std::tie(catchup_y_growth_frames_, catchup_y_bonus_) = update_axis_bonus(
        state.target.dy,
        state.previous_dy,
        catchup_y_growth_frames_,
        catchup_y_bonus_);

    state.output_dx *= 1.0f + catchup_x_bonus_;
    state.output_dy *= 1.0f + catchup_y_bonus_;
}

void AimEnhancementPipeline::apply_near_target_damping(AimState& state) {
    if (!is_crosshair_in_slow_zone(state)) {
        return;
    }
    if (!is_converging(state)) {
        return;
    }

    const float distance = std::hypot(state.target.dx, state.target.dy);
    const float scale = damping_scale_for_distance(distance);
    state.output_dx *= scale;
    state.output_dy *= scale;
}

VisionResult AimEnhancementPipeline::process(
    const VisionResult& target,
    double timestamp_seconds,
    const std::optional<AimSlowZone>& slow_zone) {
    if (!target.has_target) {
        reset();
        return target;
    }

    const bool predicted = is_predicted(target);
    double dt = 0.0;
    float motion_x = 0.0f;
    float motion_y = 0.0f;
    std::optional<float> previous_dx;
    std::optional<float> previous_dy;

    if (previous_target_.has_value()) {
        previous_dx = previous_target_->dx;
        previous_dy = previous_target_->dy;
    }

    if (!predicted && previous_target_.has_value() && previous_timestamp_.has_value()) {
        dt = std::max(0.0, timestamp_seconds - *previous_timestamp_);
        motion_x = target.target_x - previous_target_->target_x;
        motion_y = target.target_y - previous_target_->target_y;
        if (dt > 0.0) {
            const float raw_velocity_x = motion_x / static_cast<float>(dt);
            const float raw_velocity_y = motion_y / static_cast<float>(dt);
            velocity_x_ = (kVelocityFilterAlpha * raw_velocity_x) + ((1.0f - kVelocityFilterAlpha) * velocity_x_);
            velocity_y_ = (kVelocityFilterAlpha * raw_velocity_y) + ((1.0f - kVelocityFilterAlpha) * velocity_y_);
        }
    }

    AimState state{
        target,
        dt,
        motion_x,
        motion_y,
        velocity_x_,
        velocity_y_,
        previous_dx,
        previous_dy,
        target.dx,
        target.dy,
        slow_zone,
    };

    if (predicted) {
        apply_near_target_damping(state);
        VisionResult output = target;
        output.dx = state.output_dx;
        output.dy = state.output_dy;
        return output;
    }

    apply_lead(state);
    apply_catchup(state);
    apply_near_target_damping(state);

    previous_target_ = TargetSnapshot{
        target.target_x,
        target.target_y,
        target.dx,
        target.dy,
    };
    previous_timestamp_ = timestamp_seconds;

    VisionResult output = target;
    output.dx = state.output_dx;
    output.dy = state.output_dy;
    return output;
}

} // namespace vision_native
