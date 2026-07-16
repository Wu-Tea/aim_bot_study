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
        float distance = has_target_ ? length(subtract(candidate.aim_px, predicted)) : 0.0f;
        if (has_target_ && candidate.source_id == source_id_) distance *= 0.25f;
        const float score = distance - candidate.reliability * 10.0f;
        if ((!has_target_ || distance <= config_.association_radius_px) && score < best_score) {
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
    double now_seconds) noexcept {
    const float dt = last_update_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now_seconds - last_update_seconds_, 0.001, 0.1))
        : 0.0f;
    const auto predicted = dt > 0.0f ? add_scaled(position_, velocity_, dt) : position_;
    const auto* candidate = choose_candidate(observations, predicted);

    pipeline_contract::TargetLifecycle lifecycle = pipeline_contract::TargetLifecycle::None;
    float reliability = latest_.reliability;
    float normalized_size = latest_.normalized_size;
    if (candidate != nullptr) {
        const bool new_target = !has_target_;
        const bool reacquiring = has_target_ && (was_missing_ || candidate->source_id != source_id_);
        if (new_target) {
            has_target_ = true;
            target_id_ = next_target_id_++;
            acquisition_started_seconds_ = now_seconds;
            position_ = candidate->aim_px;
            velocity_ = {};
            acceleration_ = {};
            settled_frames_ = 0;
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
            const auto measured_velocity = pipeline_contract::Vec2f{
                (measured_position.x - position_.x) / dt,
                (measured_position.y - position_.y) / dt,
            };
            const auto previous_velocity = velocity_;
            velocity_.x += config_.motion_velocity_alpha * (measured_velocity.x - velocity_.x);
            velocity_.y += config_.motion_velocity_alpha * (measured_velocity.y - velocity_.y);
            acceleration_ = {
                (velocity_.x - previous_velocity.x) / dt,
                (velocity_.y - previous_velocity.y) / dt,
            };
            position_ = measured_position;
        }
        source_id_ = candidate->source_id;
        reliability = std::clamp(candidate->reliability, 0.0f, 1.0f);
        normalized_size = std::clamp(candidate->normalized_size, 0.0f, 1.0f);
        last_observed_seconds_ = now_seconds;
        lifecycle = reacquiring
            ? pipeline_contract::TargetLifecycle::Reacquiring
            : pipeline_contract::TargetLifecycle::Observed;
        was_missing_ = false;
    } else if (has_target_) {
        const float missing_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
        if (missing_ms <= config_.hold_ms) {
            position_ = predicted;
            lifecycle = pipeline_contract::TargetLifecycle::Coasting;
            reliability *= std::clamp(1.0f - missing_ms / config_.hold_ms, 0.0f, 1.0f);
            was_missing_ = true;
        } else {
            has_target_ = false;
            source_id_ = 0;
            settled_frames_ = 0;
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
    if (error_length <= config_.settle_radius_px && lifecycle != pipeline_contract::TargetLifecycle::Coasting) {
        ++settled_frames_;
    } else {
        settled_frames_ = 0;
    }
    plan.mode = !intent.ads
        ? pipeline_contract::ControlMode::Manual
        : settled_frames_ >= config_.settle_frames
            ? pipeline_contract::ControlMode::BodyLockFollow
            : pipeline_contract::ControlMode::AdsAcquire;
    plan.ads_demand = std::clamp(error_length / 130.0f, 0.0f, 1.0f);
    plan.bodylock_demand = std::clamp(
        std::max(error_length / 40.0f, length(velocity_) / 600.0f), 0.0f, 1.0f);
    plan.aim_authority = plan.mode == pipeline_contract::ControlMode::Manual
        ? 0.0f
        : std::min(config_.max_authority, reliability);
    const auto response = response_estimator_.estimate();
    plan.response_scale = response.scale_px_per_stick_second;
    plan.response_confidence = response.confidence;
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
    plan.fire_authority = lifecycle == pipeline_contract::TargetLifecycle::Observed &&
        reliability >= 0.8f && error_length <= 6.0f && length(velocity_) <= 120.0f;
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

void TargetCoordinator::begin_ads_epoch(std::uint64_t epoch) noexcept {
    response_estimator_.begin_ads_epoch(epoch);
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
    last_observed_seconds_ = 0.0;
    last_update_seconds_ = 0.0;
    acquisition_started_seconds_ = 0.0;
    settled_frames_ = 0;
    has_target_ = false;
    was_missing_ = false;
}

}  // namespace controller_native
