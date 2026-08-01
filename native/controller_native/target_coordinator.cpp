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

float smoothstep(float value) noexcept {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

float motion_velocity_alpha_for_interval(
    float reference_alpha,
    float reference_interval_seconds,
    float observation_interval_seconds) noexcept {
    const float alpha = std::clamp(reference_alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f || alpha >= 1.0f) return alpha;
    const float reference_dt = std::max(0.001f, reference_interval_seconds);
    const float observation_dt = std::clamp(
        observation_interval_seconds, 0.001f, 0.1f);
    const float interval_ratio = observation_dt / reference_dt;
    return std::clamp(
        1.0f - std::pow(1.0f - alpha, interval_ratio),
        0.0f, 1.0f);
}

TargetCoordinator::TargetCoordinator(TargetCoordinatorConfig config)
    : config_(config) {}

TargetCoordinator::PlayerMotionEstimate
TargetCoordinator::update_player_motion_estimate(
    const TargetControlFeedback& feedback,
    float manual_camera_ownership) noexcept {
    PlayerMotionEstimate estimate{};
    const bool jump_active =
        feedback.player_jump_action_age_ms >= 0.0f &&
        feedback.player_jump_action_age_ms <= 700.0f;
    const bool slide_active =
        feedback.player_slide_action_age_ms >= 0.0f &&
        feedback.player_slide_action_age_ms <= 620.0f;
    if (jump_active && (!slide_active ||
        feedback.player_jump_action_age_ms <=
            feedback.player_slide_action_age_ms)) {
        estimate.event = PlayerMotionEvent::Jump;
    } else if (slide_active) {
        estimate.event = PlayerMotionEvent::Slide;
    }

    auto unit_offset = [](PlayerMotionEvent event, float age_ms) noexcept {
        constexpr float kPi = 3.14159265358979323846f;
        if (event == PlayerMotionEvent::Jump) {
            constexpr float kJumpDurationMs = 560.0f;
            if (age_ms < 0.0f || age_ms >= kJumpDurationMs) return 0.0f;
            return std::sin(
                kPi * std::clamp(age_ms / kJumpDurationMs, 0.0f, 1.0f));
        }
        if (event != PlayerMotionEvent::Slide || age_ms < 0.0f) {
            return 0.0f;
        }
        constexpr float kDropMs = 110.0f;
        constexpr float kHoldEndMs = 320.0f;
        constexpr float kRecoveryEndMs = 490.0f;
        if (age_ms < kDropMs) {
            return -smoothstep(age_ms / kDropMs);
        }
        if (age_ms < kHoldEndMs) return -1.0f;
        if (age_ms >= kRecoveryEndMs) return 0.0f;
        return -1.0f +
            (age_ms - kHoldEndMs) /
                (kRecoveryEndMs - kHoldEndMs);
    };

    const float age_ms = estimate.event == PlayerMotionEvent::Jump
        ? feedback.player_jump_action_age_ms
        : estimate.event == PlayerMotionEvent::Slide
            ? feedback.player_slide_action_age_ms
            : -1.0f;
    const float amplitude = estimate.event == PlayerMotionEvent::Jump
        ? jump_effective_amplitude_px_
        : estimate.event == PlayerMotionEvent::Slide
            ? slide_effective_amplitude_px_
            : 0.0f;
    const std::uint32_t samples =
        estimate.event == PlayerMotionEvent::Jump
        ? jump_motion_learning_samples_
        : estimate.event == PlayerMotionEvent::Slide
            ? slide_motion_learning_samples_
            : 0;
    const float base_confidence =
        estimate.event == PlayerMotionEvent::Jump ? 0.45f :
        estimate.event == PlayerMotionEvent::Slide ? 0.35f : 0.0f;
    const float confidence_step =
        estimate.event == PlayerMotionEvent::Jump ? 0.05f : 0.06f;
    const float state_confidence = std::clamp(
        base_confidence + confidence_step *
            static_cast<float>(std::min<std::uint32_t>(samples, 8)),
        0.0f, 0.85f);
    estimate.unit_offset = unit_offset(estimate.event, age_ms);
    const float modeled_offset_y_px =
        amplitude * estimate.unit_offset * state_confidence;
    if (estimate.event != active_player_motion_event_) {
        active_player_motion_event_ = estimate.event;
        previous_player_motion_offset_y_px_ = modeled_offset_y_px;
        previous_observed_player_motion_unit_offset_ =
            estimate.unit_offset;
    } else {
        estimate.realized_delta_y_px =
            modeled_offset_y_px -
            previous_player_motion_offset_y_px_;
        previous_player_motion_offset_y_px_ = modeled_offset_y_px;
    }
    const float future_unit = unit_offset(estimate.event, age_ms + 32.0f);
    estimate.forecast_y_px =
        amplitude * (future_unit - estimate.unit_offset);
    estimate.confidence = state_confidence *
        (1.0f - 0.5f *
            std::clamp(manual_camera_ownership, 0.0f, 1.0f));
    return estimate;
}

void TargetCoordinator::learn_player_motion_amplitude(
    PlayerMotionEvent event,
    float unit_offset,
    float innovation_y_px,
    float reliability,
    bool reacquiring,
    float manual_camera_ownership) noexcept {
    const float unit_delta =
        unit_offset - previous_observed_player_motion_unit_offset_;
    previous_observed_player_motion_unit_offset_ = unit_offset;
    if (event == PlayerMotionEvent::None || reacquiring ||
        reliability < 0.65f || manual_camera_ownership > 0.25f ||
        std::fabs(unit_delta) < 0.025f ||
        std::fabs(innovation_y_px) > 12.0f) {
        return;
    }
    std::uint32_t& samples = event == PlayerMotionEvent::Jump
        ? jump_motion_learning_samples_
        : slide_motion_learning_samples_;
    float& amplitude = event == PlayerMotionEvent::Jump
        ? jump_effective_amplitude_px_
        : slide_effective_amplitude_px_;
    const float base_confidence =
        event == PlayerMotionEvent::Jump ? 0.45f : 0.35f;
    const float confidence_step =
        event == PlayerMotionEvent::Jump ? 0.05f : 0.06f;
    const float state_confidence = std::clamp(
        base_confidence + confidence_step *
            static_cast<float>(std::min<std::uint32_t>(samples, 8)),
        0.25f, 0.85f);
    const float amplitude_error = innovation_y_px /
        (unit_delta * state_confidence);
    amplitude += std::clamp(amplitude_error * 0.20f, -4.0f, 4.0f);
    amplitude = event == PlayerMotionEvent::Jump
        ? std::clamp(amplitude, 12.0f, 72.0f)
        : std::clamp(amplitude, 16.0f, 80.0f);
    ++samples;
}

void TargetCoordinator::set_motion_velocity_alpha_for_benchmark(
    float alpha) noexcept {
    config_.motion_velocity_alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void TargetCoordinator::set_causal_player_motion_enabled_for_benchmark(
    bool state_enabled,
    bool forecast_enabled) noexcept {
    causal_player_motion_state_enabled_ = state_enabled;
    causal_player_motion_forecast_enabled_ = forecast_enabled;
}

const pipeline_contract::VisionCandidate* TargetCoordinator::choose_candidate(
    const pipeline_contract::VisionObservationBatch& observations,
    pipeline_contract::Vec2f predicted) const noexcept {
    // Under the selector-owned protocol, a zero preferred id is an explicit
    // "no selected target" result, not permission to acquire an arbitrary
    // detector candidate. Candidates still enter the batch for diagnostics
    // and memory, but only the selector may grant fresh control ownership.
    if (observations.selector_identity_protocol &&
        observations.preferred_source_id == 0) {
        return nullptr;
    }
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
        const pipeline_contract::Vec2f screen_center{
            observations.frame_width_px > 0.0f
                ? observations.frame_width_px * 0.5f : 240.0f,
            observations.frame_height_px > 0.0f
                ? observations.frame_height_px * 0.5f : 208.0f};
        float distance = has_target_
            ? length(subtract(candidate.aim_px, predicted))
            : length(subtract(candidate.aim_px, screen_center));
        if (!has_target_ && ads_epoch_active_ && !ads_snap_consumed_) {
            const float observed_size = std::clamp(
                candidate.normalized_size, 0.0f, 1.0f);
            const float ads_activation_radius =
                config_.ads_activation_radius_px *
                (1.0f + 0.75f * observed_size);
            if (distance > ads_activation_radius) continue;
        }
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
    delivered_camera_work_since_capture_px_ = {};
    remaining_work_confidence_ = 0.0f;
    remaining_work_valid_ = false;
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

void TargetCoordinator::
set_firing_body_geometry_stabilizer_enabled_for_benchmark(
    bool enabled) noexcept {
    config_.firing_body_geometry_stabilizer_enabled = enabled;
    stable_body_aim_tracker_.reset();
}

void TargetCoordinator::
set_firing_disturbance_observer_enabled_for_benchmark(
    bool enabled) noexcept {
    config_.firing_disturbance_observer_enabled = enabled;
    previous_firing_velocity_innovation_ = {};
    firing_velocity_observer_active_ = false;
}

pipeline_contract::TargetPlan TargetCoordinator::update(
    const pipeline_contract::VisionObservationBatch& observations,
    const pipeline_contract::IntentState& intent,
    double now_seconds,
    const TargetControlFeedback& feedback) noexcept {
    const float dt = last_update_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now_seconds - last_update_seconds_, 0.001, 0.1))
        : 0.0f;
    const bool player_motion_oracle =
        feedback.has_player_motion_oracle &&
        std::isfinite(feedback.player_error_delta_px.x) &&
        std::isfinite(feedback.player_error_delta_px.y) &&
        std::isfinite(feedback.player_error_rate_px_per_sec.x) &&
        std::isfinite(feedback.player_error_rate_px_per_sec.y);
    const bool player_motion_rate_oracle =
        player_motion_oracle &&
        feedback.has_player_motion_rate_oracle;
    const bool jump_acceleration_model_active =
        !player_motion_oracle &&
        !causal_player_motion_state_enabled_ &&
        !causal_player_motion_forecast_enabled_ &&
        feedback.player_jump_action_age_ms >= 0.0f &&
        feedback.player_jump_action_age_ms <=
            config_.player_jump_acceleration_model_ms;
    const float manual_right_magnitude = std::hypot(
        intent.filtered_right.x, intent.filtered_right.y);
    const float manual_camera_ownership = std::max(
        std::clamp(
            std::max(intent.right_x.confidence,
                     intent.right_y.confidence),
            0.0f, 1.0f),
        std::clamp(manual_right_magnitude / 0.12f, 0.0f, 1.0f));
    const float jump_acceleration_authority =
        jump_acceleration_model_active
        ? 1.0f - manual_camera_ownership
        : 0.0f;
    const PlayerMotionEstimate player_motion =
        (causal_player_motion_state_enabled_ ||
         causal_player_motion_forecast_enabled_) &&
            !player_motion_oracle
        ? update_player_motion_estimate(
            feedback, manual_camera_ownership)
        : PlayerMotionEstimate{};
    if (feedback.reset_remaining_work) {
        delivered_camera_work_since_capture_px_ = {};
        remaining_work_confidence_ = 0.0f;
        remaining_work_valid_ = false;
    }
    auto predicted = dt > 0.0f
        ? pipeline_contract::Vec2f{
            position_.x + velocity_.x * dt,
            position_.y + velocity_.y * dt +
                0.5f * acceleration_.y * dt * dt *
                    jump_acceleration_authority}
        : position_;
    if (player_motion_oracle && dt > 0.0f) {
        predicted.x += feedback.player_error_delta_px.x;
        predicted.y += feedback.player_error_delta_px.y;
    } else if (causal_player_motion_state_enabled_ && dt > 0.0f) {
        predicted.y += player_motion.realized_delta_y_px;
    }
    if (feedback.apply_delivered_camera_work &&
        pipeline_contract::finite(
            feedback.delivered_camera_work_delta_px)) {
        predicted.x -= feedback.delivered_camera_work_delta_px.x;
        predicted.y -= feedback.delivered_camera_work_delta_px.y;
        delivered_camera_work_since_capture_px_.x +=
            feedback.delivered_camera_work_delta_px.x;
        delivered_camera_work_since_capture_px_.y +=
            feedback.delivered_camera_work_delta_px.y;
        remaining_work_confidence_ = std::clamp(
            feedback.remaining_work_confidence, 0.0f, 1.0f);
        remaining_work_valid_ = remaining_work_confidence_ > 0.0f;
    }
    const pipeline_contract::Vec2f center{
        observations.frame_width_px > 0.0f
            ? observations.frame_width_px * 0.5f : 240.0f,
        observations.frame_height_px > 0.0f
            ? observations.frame_height_px * 0.5f : 208.0f,
    };
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
        pipeline_contract::Vec2f observed_aim_px = candidate->aim_px;
        double observation_capture_seconds = now_seconds;
        const bool source_time_available =
            std::isfinite(observations.source_time_seconds) &&
            (observations.source_time_seconds > 0.0 ||
             now_seconds <= 0.001);
        if (source_time_available) {
            observation_capture_seconds = observations.source_time_seconds;
        }
        const bool new_observation_sample =
            !has_observation_capture_time_ ||
            observations.frame_id != source_frame_id_ ||
            observation_capture_seconds >
                last_observation_capture_seconds_ + 1.0e-6;
        const bool new_target = !has_target_;
        if (new_target) {
            stable_body_aim_tracker_.reset();
        }
        const bool assisted_motion_model =
            control_mode_ ==
                pipeline_contract::ControlMode::BodyLockFollow ||
            control_mode_ ==
                pipeline_contract::ControlMode::AdsAcquire;
        const bool firing_context =
            intent.fire || feedback.firing_recently;
        const bool stabilize_body_geometry =
            config_.firing_body_geometry_stabilizer_enabled &&
            assisted_motion_model && firing_context &&
            observed_frames_ >= 2 &&
            length(subtract(predicted, center)) <=
                std::max(18.0f, config_.settle_radius_px * 2.5f);
        if (new_observation_sample && candidate->has_body_box) {
            const auto stable = stable_body_aim_tracker_.update(
                {observed_aim_px.x, observed_aim_px.y},
                candidate->body_box_px,
                stabilize_body_geometry,
                candidate->has_motion_anchor,
                {candidate->motion_anchor_px.x,
                 candidate->motion_anchor_px.y});
            observed_aim_px = {stable.aim_px.x, stable.aim_px.y};
        } else if (new_observation_sample) {
            stable_body_aim_tracker_.reset();
        }
        if (feedback.has_delivered_camera_work_since_capture &&
            pipeline_contract::finite(
                feedback.delivered_camera_work_since_capture_px)) {
            observed_aim_px.x -=
                feedback.delivered_camera_work_since_capture_px.x;
            observed_aim_px.y -=
                feedback.delivered_camera_work_since_capture_px.y;
            if (feedback.capture_alignment_only) {
                // BodyLock needs capture-time coordinate alignment, but it
                // already owns the sustained position/velocity loop. Do not
                // convert alignment data back into Remaining authority.
                delivered_camera_work_since_capture_px_ = {};
                remaining_work_confidence_ = 0.0f;
                remaining_work_valid_ = false;
            } else {
                delivered_camera_work_since_capture_px_ =
                    feedback.delivered_camera_work_since_capture_px;
                remaining_work_confidence_ = std::clamp(
                    feedback.remaining_work_confidence, 0.0f, 1.0f);
                remaining_work_valid_ = remaining_work_confidence_ > 0.0f;
            }
        } else {
            delivered_camera_work_since_capture_px_ = {};
            remaining_work_confidence_ = 0.0f;
            remaining_work_valid_ = false;
        }
        if (new_observation_sample) {
            last_unique_observation_seconds_ = now_seconds;
            has_unique_observation_time_ = true;
        }
        const bool reacquiring = has_target_ && was_missing_;
        if (new_target) {
            has_target_ = true;
            target_id_ = next_target_id_++;
            acquisition_started_seconds_ = now_seconds;
            position_ = observed_aim_px;
            velocity_ = {};
            acceleration_ = {};
            previous_firing_velocity_innovation_ = {};
            firing_velocity_observer_active_ = false;
            settled_frames_ = 0;
            observed_frames_ = 1;
        } else if (dt > 0.0f && new_observation_sample) {
            const float observation_dt = has_observation_capture_time_ &&
                    observation_capture_seconds >
                        last_observation_capture_seconds_
                ? static_cast<float>(std::clamp(
                    observation_capture_seconds -
                        last_observation_capture_seconds_,
                    0.001, 0.1))
                : dt;
            auto innovation = subtract(observed_aim_px, predicted);
            float innovation_length = length(innovation);
            if (reacquiring && innovation_length > config_.max_reacquire_innovation_px) {
                const float scale = config_.max_reacquire_innovation_px / innovation_length;
                innovation.x *= scale;
                innovation.y *= scale;
                innovation_length = config_.max_reacquire_innovation_px;
            }
            auto velocity_innovation = innovation;
            const bool bodylock_motion_model =
                control_mode_ ==
                    pipeline_contract::ControlMode::BodyLockFollow;
            const bool low_anchor_bodylock_frame =
                bodylock_motion_model && firing_context &&
                (!candidate->has_motion_anchor ||
                 candidate->motion_anchor_score < 0.45f);
            const bool observe_firing_velocity =
                config_.firing_disturbance_observer_enabled &&
                firing_context &&
                (control_mode_ ==
                     pipeline_contract::ControlMode::AdsAcquire ||
                  (bodylock_motion_model &&
                  low_anchor_bodylock_frame &&
                  length(velocity_) <= 80.0f)) &&
                observed_frames_ >= 2 && !reacquiring;
            bool persistent_firing_innovation = true;
            if (observe_firing_velocity) {
                if (!firing_velocity_observer_active_) {
                    persistent_firing_innovation = false;
                } else {
                    const float previous_length =
                        length(previous_firing_velocity_innovation_);
                    const float current_length =
                        length(velocity_innovation);
                    const float agreement =
                        previous_firing_velocity_innovation_.x *
                            velocity_innovation.x +
                        previous_firing_velocity_innovation_.y *
                            velocity_innovation.y;
                    persistent_firing_innovation =
                        previous_length <= 0.25f ||
                        current_length <= 0.25f ||
                        agreement >=
                            0.35f * previous_length * current_length;
                }
            }
            // Robust alpha-beta observation update. Gun kick, recoil recovery,
            // and detector reconstruction all appear as innovation, just like
            // target motion. A hard confirmation gate adds phase delay and
            // eventually releases the whole residual, which turns jitter into
            // a low-frequency control oscillation. A bounded influence
            // function instead accepts a continuous, physically useful share
            // on every frame and discards the transient tail permanently.
            if (assisted_motion_model && new_observation_sample &&
                firing_context && observed_frames_ >= 2) {
                const float influence_limit = std::max(
                    0.0f, config_.fire_innovation_limit_px);
                if (influence_limit > 0.0f &&
                    innovation_length > influence_limit) {
                    const float scale =
                        influence_limit / innovation_length;
                    innovation.x *= scale;
                    innovation.y *= scale;
                    innovation_length = influence_limit;
                }
                const float velocity_innovation_length =
                    length(velocity_innovation);
                if (influence_limit > 0.0f &&
                    velocity_innovation_length > influence_limit) {
                    const float scale =
                        influence_limit / velocity_innovation_length;
                    velocity_innovation.x *= scale;
                    velocity_innovation.y *= scale;
                }
            }
            learn_player_motion_amplitude(
                player_motion.event,
                player_motion.unit_offset,
                innovation.y,
                candidate->reliability,
                reacquiring,
                manual_camera_ownership);
            const auto measured_position = pipeline_contract::Vec2f{
                predicted.x + innovation.x,
                predicted.y + innovation.y,
            };
            auto measured_velocity = pipeline_contract::Vec2f{
                velocity_.x +
                    velocity_innovation.x / observation_dt,
                velocity_.y +
                    velocity_innovation.y / observation_dt,
            };
            if (observe_firing_velocity) {
                if (!firing_velocity_observer_active_) {
                    firing_velocity_observer_active_ = true;
                    measured_velocity = velocity_;
                } else if (!persistent_firing_innovation) {
                    // An unconfirmed reversal is a firing transient until a
                    // second observation supports it. Neutralize the old
                    // velocity instead of holding it, so uncertainty cannot
                    // become an overshoot tail.
                    measured_velocity = {};
                }
                previous_firing_velocity_innovation_ =
                    velocity_innovation;
            } else {
                previous_firing_velocity_innovation_ = {};
                firing_velocity_observer_active_ = false;
            }
            const auto previous_velocity = velocity_;
            const float velocity_alpha =
                motion_velocity_alpha_for_interval(
                    config_.motion_velocity_alpha,
                    config_.motion_velocity_reference_interval_seconds,
                    observation_dt);
            pipeline_contract::Vec2f velocity_delta{
                velocity_alpha * (measured_velocity.x - velocity_.x),
                velocity_alpha * (measured_velocity.y - velocity_.y),
            };
            if (bodylock_motion_model ||
                (firing_context &&
                 control_mode_ ==
                     pipeline_contract::ControlMode::AdsAcquire)) {
                const float maximum_velocity_delta =
                    std::max(
                        0.0f,
                        config_.
                            bodylock_max_target_acceleration_px_per_second2) *
                    observation_dt;
                const float velocity_delta_length = length(velocity_delta);
                if (maximum_velocity_delta > 0.0f &&
                    velocity_delta_length > maximum_velocity_delta) {
                    const float scale =
                        maximum_velocity_delta / velocity_delta_length;
                    velocity_delta.x *= scale;
                    velocity_delta.y *= scale;
                }
            }
            velocity_.x += velocity_delta.x;
            velocity_.y += velocity_delta.y;
            velocity_.x = std::clamp(velocity_.x, -4000.0f, 4000.0f);
            velocity_.y = std::clamp(velocity_.y, -4000.0f, 4000.0f);
            acceleration_ = {
                std::clamp(
                    (velocity_.x - previous_velocity.x) /
                        observation_dt,
                    -20000.0f, 20000.0f),
                std::clamp(
                    (velocity_.y - previous_velocity.y) /
                        observation_dt,
                    -20000.0f, 20000.0f),
            };
            position_ = measured_position;
            observed_frames_ = reacquiring ? 1 : observed_frames_ + 1;
        } else if (dt > 0.0f) {
            // The controller runs much faster than Vision and replays the
            // latest candidate between publications. Propagate the target
            // state and delivered camera work, but never admit the same
            // measurement into position/velocity twice.
            position_ = predicted;
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
        if (new_observation_sample) {
            last_observed_seconds_ = now_seconds;
            last_observation_capture_seconds_ =
                observation_capture_seconds;
            has_observation_capture_time_ = true;
        }
        lifecycle = reacquiring
            ? pipeline_contract::TargetLifecycle::Reacquiring
            : pipeline_contract::TargetLifecycle::Observed;
        was_missing_ = false;
    } else if (has_target_) {
        const float missing_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
        if (missing_ms <= config_.hold_ms) {
            position_ = predicted;
            if (jump_acceleration_authority > 0.0f && dt > 0.0f) {
                velocity_.y = std::clamp(
                    velocity_.y + acceleration_.y * dt *
                        jump_acceleration_authority,
                    -4000.0f, 4000.0f);
            }
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

    pipeline_contract::TargetPlan plan{};
    plan.generation = ++generation_;
    plan.source_frame_id = source_frame_id_;
    plan.source_observation_id = source_id_;
    plan.target_id = target_id_;
    plan.lifecycle = lifecycle;
    plan.aim_px = position_;
    plan.error_px = subtract(position_, center);
    const pipeline_contract::Vec2f screen_velocity =
        player_motion_rate_oracle
        ? pipeline_contract::Vec2f{
            velocity_.x + feedback.player_error_rate_px_per_sec.x,
            velocity_.y + feedback.player_error_rate_px_per_sec.y}
        : velocity_;
    plan.predicted_aim_px = add_scaled(position_, screen_velocity, 0.032f);
    plan.velocity_px_per_sec = screen_velocity;
    plan.acceleration_px_per_sec2 = acceleration_;
    plan.observation_age_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
    plan.confidence = reliability;
    plan.reliability = reliability;
    plan.normalized_size = normalized_size;
    plan.occlusion_budget_ms = std::max(0.0f, config_.hold_ms - plan.observation_age_ms);
    const float error_length = length(plan.error_px);
    plan.acquisition_elapsed_ms = static_cast<float>(
        std::max(0.0, (now_seconds - acquisition_started_seconds_) * 1000.0));
    plan.ads_epoch_elapsed_ms = ads_epoch_active_
        ? static_cast<float>(
            std::max(0.0, (now_seconds - ads_epoch_started_seconds_) * 1000.0))
        : 0.0f;
    plan.source_capture_age_ms = has_observation_capture_time_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - last_observation_capture_seconds_) * 1000.0))
        : plan.observation_age_ms;
    plan.delivered_camera_motion_since_capture_px =
        delivered_camera_work_since_capture_px_;
    plan.remaining_work_px = plan.error_px;
    plan.remaining_work_confidence = remaining_work_confidence_;
    plan.remaining_work_valid = remaining_work_valid_;
    const float response_scale = std::max(
        0.0f, feedback.aim_response_px_per_stick_second);
    const pipeline_contract::Vec2f residual_error_rate = observed_frames_ >= 2
        ? screen_velocity
        : pipeline_contract::Vec2f{
            screen_velocity.x -
                feedback.previous_delivered_stick.x * response_scale,
            screen_velocity.y +
                feedback.previous_delivered_stick.y * response_scale,
        };
    plan.predicted_terminal_error_px = add_scaled(
        plan.error_px, residual_error_rate, config_.handoff_prediction_seconds);
    plan.player_motion_forecast_px = {
        0.0f,
        causal_player_motion_forecast_enabled_
            ? player_motion.forecast_y_px : 0.0f};
    plan.player_motion_confidence =
        causal_player_motion_forecast_enabled_
        ? player_motion.confidence : 0.0f;
    // The event model bridges time that Vision has not observed. A newly
    // captured point is authoritative, so do not add the same motion twice;
    // hand authority to the forecast continuously over one ~80 Hz frame.
    const float forecast_bridge_age_ms = has_unique_observation_time_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - last_unique_observation_seconds_) * 1000.0))
        : 0.0f;
    plan.player_motion_confidence *= smoothstep(
        std::clamp(forecast_bridge_age_ms / 12.5f, 0.0f, 1.0f));
    plan.predicted_terminal_error_px.y +=
        plan.player_motion_forecast_px.y *
        plan.player_motion_confidence;
    plan.radial_closing_velocity_px_per_sec = error_length > 0.001f
        ? -(plan.error_px.x * residual_error_rate.x +
            plan.error_px.y * residual_error_rate.y) / error_length
        : 0.0f;
    const float predicted_radial_error = error_length > 1.0f
        ? (plan.predicted_terminal_error_px.x * plan.error_px.x +
           plan.predicted_terminal_error_px.y * plan.error_px.y) / error_length
        : 0.0f;
    const float capture_radius = config_.settle_radius_px *
        (1.0f + 2.0f *
            std::clamp(length(screen_velocity) / 120.0f, 0.0f, 1.0f));
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
    const bool acquisition_ceiling_elapsed = ads_epoch_active_ &&
        plan.ads_epoch_elapsed_ms >= std::max(
            0.0f, config_.ads_max_acquisition_ms);
    if (!intent.ads) {
        control_mode_ = pipeline_contract::ControlMode::Manual;
        ads_epoch_active_ = false;
        ads_snap_consumed_ = false;
    } else if (control_mode_ == pipeline_contract::ControlMode::BodyLockFollow) {
        ads_snap_consumed_ = true;
    } else if (ads_snap_consumed_ || acquisition_ceiling_elapsed ||
               settled_frames_ >= config_.settle_frames) {
        control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
        ads_snap_consumed_ = true;
    } else {
        control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
    }
    plan.mode = control_mode_;
    if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        // ADS may carry a Remaining estimate into the handoff tick. BodyLock
        // must start from the tracker's current state instead of inheriting a
        // second, response-model-based position loop.
        delivered_camera_work_since_capture_px_ = {};
        remaining_work_confidence_ = 0.0f;
        remaining_work_valid_ = false;
        plan.delivered_camera_motion_since_capture_px = {};
        plan.remaining_work_px = plan.error_px;
        plan.remaining_work_confidence = 0.0f;
        plan.remaining_work_valid = false;
    }
    plan.ads_demand = std::clamp(error_length / 130.0f, 0.0f, 1.0f);
    plan.bodylock_demand = std::clamp(
        std::max(
            error_length / 40.0f,
            length(screen_velocity) / 600.0f),
        0.0f, 1.0f);
    // A fixed pixel radius is too small for close targets: the upper-body aim
    // point can move far from the reticle during a climb, slide, or jump while
    // the target still fills the capture. Let observed body geometry expand
    // the continuation range, while small/far targets retain the conservative
    // base radius.
    const float observed_body_size =
        lifecycle == pipeline_contract::TargetLifecycle::Observed
        ? normalized_size
        : 0.0f;
    const float bodylock_continuation_radius =
        config_.bodylock_activation_radius_px *
        (1.0f + 0.75f * observed_body_size);
    const bool bodylock_outside_activation_range =
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        error_length > bodylock_continuation_radius;
    plan.aim_authority =
        plan.mode == pipeline_contract::ControlMode::Manual ||
            bodylock_outside_activation_range
        ? 0.0f
        : std::min(config_.max_authority, reliability);
    const auto response = response_estimator_.estimate();
    plan.left_motion_response_scale = response.scale_px_per_stick_second;
    plan.left_motion_response_confidence = response.confidence;
    plan.response_scale = std::max(
        50.0f, feedback.aim_response_px_per_stick_second);
    plan.response_confidence = std::clamp(
        feedback.aim_response_confidence, 0.0f, 1.0f);
    plan.error_rate_px_per_sec = screen_velocity;
    if (!player_motion_rate_oracle) {
        plan.error_rate_px_per_sec.x +=
            response.scale_px_per_stick_second *
            response.confidence * intent.filtered_left.x *
            intent.left_confidence;
    }
    if (screen_velocity.y < -config_.jump_fall_velocity_px_per_second) {
        plan.motion = pipeline_contract::TargetMotion::Jump;
    } else if (screen_velocity.y > config_.jump_fall_velocity_px_per_second) {
        plan.motion = pipeline_contract::TargetMotion::Fall;
    } else if (
        std::fabs(screen_velocity.x) >
        config_.jump_fall_velocity_px_per_second) {
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
                : length(screen_velocity) > 120.0f
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
    delivered_camera_work_since_capture_px_ = {};
    remaining_work_confidence_ = 0.0f;
    remaining_work_valid_ = false;
}

void TargetCoordinator::reset() noexcept {
    response_estimator_.reset();
    latest_ = {};
    position_ = {};
    velocity_ = {};
    acceleration_ = {};
    stable_body_aim_tracker_.reset();
    previous_firing_velocity_innovation_ = {};
    firing_velocity_observer_active_ = false;
    source_id_ = 0;
    target_id_ = 0;
    generation_ = 0;
    source_frame_id_ = 0;
    last_observed_seconds_ = 0.0;
    last_observation_capture_seconds_ = 0.0;
    last_unique_observation_seconds_ = 0.0;
    last_update_seconds_ = 0.0;
    acquisition_started_seconds_ = 0.0;
    ads_epoch_started_seconds_ = 0.0;
    last_observed_reliability_ = 0.0f;
    last_observed_normalized_size_ = 0.0f;
    settled_frames_ = 0;
    observed_frames_ = 0;
    has_target_ = false;
    has_observation_capture_time_ = false;
    has_unique_observation_time_ = false;
    fire_requested_ = false;
    observed_fire_eligible_ = false;
    was_missing_ = false;
    ads_epoch_active_ = false;
    ads_snap_consumed_ = false;
    control_mode_ = pipeline_contract::ControlMode::Manual;
    active_player_motion_event_ = PlayerMotionEvent::None;
    previous_player_motion_offset_y_px_ = 0.0f;
    previous_observed_player_motion_unit_offset_ = 0.0f;
    jump_effective_amplitude_px_ = 28.0f;
    slide_effective_amplitude_px_ = 36.0f;
    jump_motion_learning_samples_ = 0;
    slide_motion_learning_samples_ = 0;
    delivered_camera_work_since_capture_px_ = {};
    remaining_work_confidence_ = 0.0f;
    remaining_work_valid_ = false;
}

}  // namespace controller_native
