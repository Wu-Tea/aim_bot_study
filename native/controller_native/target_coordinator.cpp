#include "target_coordinator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace controller_native {
namespace {

float length(pipeline_contract::Vec2f value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

pipeline_contract::Vec2f subtract(
    pipeline_contract::Vec2f lhs,
    pipeline_contract::Vec2f rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

pipeline_contract::Vec2f add_scaled(
    pipeline_contract::Vec2f value,
    pipeline_contract::Vec2f velocity,
    float seconds) noexcept {
    return {value.x + velocity.x * seconds, value.y + velocity.y * seconds};
}

}  // namespace

TargetCoordinator::TargetCoordinator(TargetCoordinatorConfig config)
    : config_(config) {}

void TargetCoordinator::set_motion_velocity_alpha_for_benchmark(
    float alpha) noexcept {
    config_.motion_velocity_alpha = std::clamp(alpha, 0.0f, 1.0f);
}

const pipeline_contract::VisionCandidate* TargetCoordinator::choose_candidate(
    const pipeline_contract::VisionObservationBatch& observations,
    pipeline_contract::Vec2f predicted) const noexcept {
    const pipeline_contract::VisionCandidate* best = nullptr;
    float best_score = std::numeric_limits<float>::max();
    const auto count = std::min<std::uint32_t>(
        observations.count,
        static_cast<std::uint32_t>(pipeline_contract::kMaxVisionCandidates));
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& candidate = observations.candidates[index];
        if (candidate.reliability <= 0.0f) continue;
        if (has_target_ && observations.preferred_source_id == 0 &&
            candidate.source_id != 0 && candidate.source_id != source_id_) {
            continue;
        }
        float distance = has_target_ ? length(subtract(candidate.aim_px, predicted)) : 0.0f;
        const bool same_source = has_target_ && candidate.source_id != 0 &&
            candidate.source_id == source_id_;
        if (same_source) distance *= 0.25f;
        const float preferred_bonus = observations.preferred_source_id != 0 &&
            candidate.source_id == observations.preferred_source_id ? 25.0f : 0.0f;
        const float score = distance - candidate.reliability * 10.0f - preferred_bonus;
        if ((!has_target_ || same_source || distance <= config_.association_radius_px) &&
            score < best_score) {
            best = &candidate;
            best_score = score;
        }
    }
    return best;
}

pipeline_contract::TargetPlan TargetCoordinator::no_target_plan() noexcept {
    pipeline_contract::TargetPlan plan{};
    plan.generation = ++generation_;
    latest_ = plan;
    return plan;
}

void TargetCoordinator::fill_horizon(pipeline_contract::TargetPlan& plan) const noexcept {
    plan.horizon_count = static_cast<std::uint32_t>(pipeline_contract::kMaxPlanHorizonSamples);
    for (std::size_t index = 0; index < pipeline_contract::kMaxPlanHorizonSamples; ++index) {
        const float time = 0.016f * static_cast<float>(index + 1);
        plan.horizon[index].time_seconds = time;
        plan.horizon[index].error_px = {
            plan.error_px.x + plan.error_rate_px_per_sec.x * time + 0.5f * acceleration_.x * time * time,
            plan.error_px.y + plan.error_rate_px_per_sec.y * time + 0.5f * acceleration_.y * time * time,
        };
    }
}

pipeline_contract::TargetPlan TargetCoordinator::update(
    const pipeline_contract::VisionObservationBatch& observations,
    const pipeline_contract::IntentState& intent,
    double now_seconds,
    const TargetControlFeedback& feedback) noexcept {
    const float dt = last_update_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now_seconds - last_update_seconds_, 0.001, 0.1))
        : 0.0f;
    const auto predicted = dt > 0.0f ? add_scaled(position_, velocity_, dt) : position_;
    const auto* candidate = choose_candidate(observations, predicted);

    if (observations.has_control_response_hint) {
        response_estimator_.update({
            intent.filtered_left.x,
            observations.control_response_x_px_per_second,
            candidate != nullptr && intent.left_confidence > 0.5f,
            candidate == nullptr,
        });
    }

    pipeline_contract::TargetLifecycle lifecycle = pipeline_contract::TargetLifecycle::None;
    float reliability = latest_.reliability;
    float normalized_size = latest_.normalized_size;
    if (candidate != nullptr) {
        const bool new_target = !has_target_;
        const bool reacquiring = has_target_ && was_missing_;
        if (new_target) {
            has_target_ = true;
            target_id_ = next_target_id_++;
            acquisition_started_seconds_ = now_seconds;
            position_ = candidate->aim_px;
            velocity_ = {};
            acceleration_ = {};
            settled_frames_ = 0;
            observed_frames_ = 1;
        } else if (dt > 0.0f) {
            auto innovation = subtract(candidate->aim_px, predicted);
            const float innovation_length = length(innovation);
            if (reacquiring && innovation_length > config_.max_reacquire_innovation_px) {
                const float scale = config_.max_reacquire_innovation_px / innovation_length;
                innovation.x *= scale;
                innovation.y *= scale;
            }
            const auto measured_position = pipeline_contract::Vec2f{
                predicted.x + innovation.x,
                predicted.y + innovation.y,
            };
            const float observation_dt = last_observed_seconds_ > 0.0
                ? static_cast<float>(std::clamp(
                    now_seconds - last_observed_seconds_, 0.005, 0.1))
                : dt;
            const auto measured_velocity = pipeline_contract::Vec2f{
                velocity_.x + innovation.x / observation_dt,
                velocity_.y + innovation.y / observation_dt,
            };
            const auto previous_velocity = velocity_;
            velocity_.x += config_.motion_velocity_alpha * (measured_velocity.x - velocity_.x);
            velocity_.y += config_.motion_velocity_alpha * (measured_velocity.y - velocity_.y);
            velocity_.x = std::clamp(velocity_.x, -4000.0f, 4000.0f);
            velocity_.y = std::clamp(velocity_.y, -4000.0f, 4000.0f);
            acceleration_ = {
                std::clamp((velocity_.x - previous_velocity.x) / observation_dt,
                           -20000.0f, 20000.0f),
                std::clamp((velocity_.y - previous_velocity.y) / observation_dt,
                           -20000.0f, 20000.0f),
            };
            position_ = measured_position;
            observed_frames_ = reacquiring ? 1 : observed_frames_ + 1;
        }
        if (candidate->source_id != 0) {
            source_id_ = candidate->source_id;
        }
        source_frame_id_ = observations.frame_id;
        fire_requested_ = observations.fire_requested;
        observed_fire_eligible_ = observations.observed_fire_eligible;
        reliability = std::clamp(candidate->reliability, 0.0f, 1.0f);
        normalized_size = std::clamp(candidate->normalized_size, 0.0f, 1.0f);
        last_observed_reliability_ = reliability;
        last_observed_normalized_size_ = normalized_size;
        last_observed_seconds_ = now_seconds;
        lifecycle = reacquiring
            ? pipeline_contract::TargetLifecycle::Reacquiring
            : pipeline_contract::TargetLifecycle::Observed;
        was_missing_ = false;
    } else if (has_target_) {
        const float missing_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
        if (missing_ms <= config_.hold_ms) {
            position_ = predicted;
            if (observations.capture_fresh) {
                fire_requested_ = false;
                observed_fire_eligible_ = false;
                was_missing_ = true;
            }
            lifecycle = pipeline_contract::TargetLifecycle::Coasting;
            reliability = last_observed_reliability_ *
                std::clamp(1.0f - missing_ms / config_.hold_ms, 0.0f, 1.0f);
            normalized_size = last_observed_normalized_size_;
        } else {
            has_target_ = false;
            source_id_ = 0;
            settled_frames_ = 0;
            observed_frames_ = 0;
            last_update_seconds_ = now_seconds;
            return no_target_plan();
        }
    } else {
        last_update_seconds_ = now_seconds;
        return no_target_plan();
    }

    const pipeline_contract::Vec2f center{
        observations.frame_width_px > 0.0f ? observations.frame_width_px * 0.5f : 240.0f,
        observations.frame_height_px > 0.0f ? observations.frame_height_px * 0.5f : 208.0f,
    };
    pipeline_contract::TargetPlan plan{};
    plan.generation = ++generation_;
    plan.source_frame_id = source_frame_id_;
    plan.source_observation_id = source_id_;
    plan.target_id = target_id_;
    plan.lifecycle = lifecycle;
    plan.aim_px = position_;
    plan.predicted_aim_px = add_scaled(position_, velocity_, 0.032f);
    plan.error_px = subtract(position_, center);
    plan.velocity_px_per_sec = velocity_;
    plan.acceleration_px_per_sec2 = acceleration_;
    plan.observation_age_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
    plan.confidence = reliability;
    plan.reliability = reliability;
    plan.normalized_size = normalized_size;
    plan.occlusion_budget_ms = std::max(0.0f, config_.hold_ms - plan.observation_age_ms);
    const float error_length = length(plan.error_px);
    plan.acquisition_elapsed_ms = static_cast<float>(
        std::max(0.0, (now_seconds - acquisition_started_seconds_) * 1000.0));
    const float response_scale = std::max(
        0.0f, feedback.aim_response_px_per_stick_second);
    const pipeline_contract::Vec2f residual_error_rate = observed_frames_ >= 2
        ? velocity_
        : pipeline_contract::Vec2f{
            velocity_.x - feedback.previous_delivered_stick.x * response_scale,
            velocity_.y + feedback.previous_delivered_stick.y * response_scale,
        };
    plan.predicted_terminal_error_px = add_scaled(
        plan.error_px, residual_error_rate, config_.handoff_prediction_seconds);
    plan.radial_closing_velocity_px_per_sec = error_length > 0.001f
        ? -(plan.error_px.x * residual_error_rate.x +
            plan.error_px.y * residual_error_rate.y) / error_length
        : 0.0f;
    const float predicted_radial_error = error_length > 1.0f
        ? (plan.predicted_terminal_error_px.x * plan.error_px.x +
           plan.predicted_terminal_error_px.y * plan.error_px.y) / error_length
        : 0.0f;
    const float capture_radius = config_.settle_radius_px *
        (1.0f + 2.0f * std::clamp(length(velocity_) / 120.0f, 0.0f, 1.0f));
    const bool inside_capture_set =
        error_length <= capture_radius &&
        std::fabs(predicted_radial_error) <= capture_radius &&
        plan.radial_closing_velocity_px_per_sec <=
            config_.handoff_max_closing_velocity_px_per_sec;
    if (candidate != nullptr) {
        if (inside_capture_set) {
            ++settled_frames_;
        } else {
            settled_frames_ = 0;
        }
    }
    const bool snap_window_elapsed = ads_epoch_active_ &&
        (now_seconds - ads_epoch_started_seconds_) * 1000.0 >=
            static_cast<double>(std::max(0.0f, config_.ads_snap_window_ms));
    if (!intent.ads) {
        control_mode_ = pipeline_contract::ControlMode::Manual;
        ads_epoch_active_ = false;
        ads_snap_consumed_ = false;
    } else if (control_mode_ == pipeline_contract::ControlMode::BodyLockFollow) {
        ads_snap_consumed_ = true;
    } else if (ads_snap_consumed_ || snap_window_elapsed ||
               settled_frames_ >= config_.settle_frames) {
        control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
        ads_snap_consumed_ = true;
    } else {
        control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
    }
    plan.mode = control_mode_;
    plan.ads_demand = std::clamp(error_length / 130.0f, 0.0f, 1.0f);
    plan.bodylock_demand = std::clamp(
        std::max(error_length / 40.0f, length(velocity_) / 600.0f), 0.0f, 1.0f);
    plan.aim_authority = plan.mode == pipeline_contract::ControlMode::Manual
        ? 0.0f
        : std::min(config_.max_authority, reliability);
    const auto response = response_estimator_.estimate();
    plan.left_motion_response_scale = response.scale_px_per_stick_second;
    plan.left_motion_response_confidence = response.confidence;
    plan.response_scale = std::max(
        50.0f, feedback.aim_response_px_per_stick_second);
    plan.response_confidence = std::clamp(
        feedback.aim_response_confidence, 0.0f, 1.0f);
    plan.error_rate_px_per_sec = velocity_;
    plan.error_rate_px_per_sec.x += response.scale_px_per_stick_second *
        response.confidence * intent.filtered_left.x * intent.left_confidence;
    if (velocity_.y < -config_.jump_fall_velocity_px_per_second) {
        plan.motion = pipeline_contract::TargetMotion::Jump;
    } else if (velocity_.y > config_.jump_fall_velocity_px_per_second) {
        plan.motion = pipeline_contract::TargetMotion::Fall;
    } else if (std::fabs(velocity_.x) > config_.jump_fall_velocity_px_per_second) {
        plan.motion = pipeline_contract::TargetMotion::Strafe;
    } else {
        plan.motion = pipeline_contract::TargetMotion::Steady;
    }
    plan.fire_authority = observed_fire_eligible_ && !was_missing_;
    plan.fire_requested = fire_requested_;
    plan.fire_suppression = plan.fire_authority
        ? pipeline_contract::FireSuppressionReason::None
        : lifecycle == pipeline_contract::TargetLifecycle::Coasting
            ? pipeline_contract::FireSuppressionReason::Stale
            : error_length > 6.0f
                ? pipeline_contract::FireSuppressionReason::LargeError
                : length(velocity_) > 120.0f
                    ? pipeline_contract::FireSuppressionReason::HighVelocity
                    : pipeline_contract::FireSuppressionReason::Ambiguous;
    fill_horizon(plan);
    latest_ = plan;
    last_update_seconds_ = now_seconds;
    return plan;
}

bool TargetCoordinator::observe_control_response(const ControlResponseSample& sample) noexcept {
    return response_estimator_.update(sample);
}

void TargetCoordinator::begin_ads_epoch(
    std::uint64_t epoch, double now_seconds) noexcept {
    response_estimator_.begin_ads_epoch(epoch);
    ads_epoch_started_seconds_ = now_seconds;
    ads_epoch_active_ = true;
    ads_snap_consumed_ = false;
    control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
}

void TargetCoordinator::reset() noexcept {
    response_estimator_.reset();
    latest_ = {};
    position_ = {};
    velocity_ = {};
    acceleration_ = {};
    source_id_ = 0;
    target_id_ = 0;
    generation_ = 0;
    source_frame_id_ = 0;
    last_observed_seconds_ = 0.0;
    last_update_seconds_ = 0.0;
    acquisition_started_seconds_ = 0.0;
    ads_epoch_started_seconds_ = 0.0;
    last_observed_reliability_ = 0.0f;
    last_observed_normalized_size_ = 0.0f;
    settled_frames_ = 0;
    observed_frames_ = 0;
    has_target_ = false;
    fire_requested_ = false;
    observed_fire_eligible_ = false;
    was_missing_ = false;
    ads_epoch_active_ = false;
    ads_snap_consumed_ = false;
    control_mode_ = pipeline_contract::ControlMode::Manual;
}

}  // namespace controller_native
