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

std::uint64_t seconds_to_ns(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    return static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
}

}  // namespace

TargetCoordinator::TargetCoordinator(TargetCoordinatorConfig config)
    : config_(config) {}

void TargetCoordinator::reset_target_owned_state_for_replacement() noexcept {
    // A selector-confirmed replacement is a new person, not a new physical
    // ADS epoch. Clear only state whose coordinate/identity belongs to the
    // previous target.
    stable_body_aim_tracker_.reset();
    position_ = {};
    velocity_ = {};
    acceleration_ = {};
    source_id_ = 0;
    previous_firing_velocity_innovation_ = {};
    firing_velocity_observer_active_ = false;
    last_observed_seconds_ = 0.0;
    last_observation_capture_seconds_ = 0.0;
    last_observed_reliability_ = 0.0f;
    last_observed_normalized_size_ = 0.0f;
    last_observed_target_size_px_ = {};
    settled_frames_ = 0;
    observed_frames_ = 0;
    has_observation_capture_time_ = false;
    fire_requested_ = false;
    observed_fire_eligible_ = false;
    cue_continuation_active_ = false;
}

const pipeline_contract::VisionCandidate* TargetCoordinator::choose_candidate(
    const pipeline_contract::VisionObservationBatch& observations,
    pipeline_contract::Vec2f association_anchor) const noexcept {
    if (!observations.selector_identity_protocol) {
        return nullptr;
    }
    // Under the selector-owned protocol, a zero preferred id is an explicit
    // "no selected target" result, not permission to acquire an arbitrary
    // detector candidate. Candidates still enter the batch for diagnostics
    // and memory, but only the selector may grant fresh control ownership.
    if (observations.preferred_source_id == 0) {
        // Cue continuation deliberately has no frame-local person id. It may
        // update only the already-owned target, in the same ADS epoch and the
        // exact same selector generation. It can neither acquire nor switch.
        if (!observations.selector_cue_continuation || !ads_epoch_active_ ||
            !has_target_ || observations.selector_target_changed ||
            observations.selector_target_generation == 0 ||
            selector_target_generation_ == 0 ||
            observations.selector_target_generation !=
                selector_target_generation_ ||
            observations.count != 1) {
            return nullptr;
        }
        const auto& cue = observations.candidates[0];
        if (cue.source_id != 0 || cue.reliability <= 0.0f ||
            cue.cue_confidence <= 0.0f ||
            length(subtract(cue.aim_px, association_anchor)) >
                config_.association_radius_px) {
            return nullptr;
        }
        return &cue;
    }
    const pipeline_contract::Vec2f screen_center{
        observations.frame_width_px > 0.0f
            ? observations.frame_width_px * 0.5f : 240.0f,
        observations.frame_height_px > 0.0f
            ? observations.frame_height_px * 0.5f : 208.0f};
    const bool needs_ads_admission = ads_epoch_active_ &&
        !ads_snap_consumed_ && !ads_target_admitted_;
    // The selector protocol carries one authoritative frame-local preferred
    // id. Coordinator scoring must never elect another detection in its place.
    const pipeline_contract::VisionCandidate* preferred = nullptr;
    const auto protocol_count = std::min<std::uint32_t>(
        observations.count,
        static_cast<std::uint32_t>(pipeline_contract::kMaxVisionCandidates));
    for (std::uint32_t index = 0; index < protocol_count; ++index) {
        const auto& candidate = observations.candidates[index];
        if (candidate.source_id == observations.preferred_source_id &&
            candidate.reliability > 0.0f) {
            preferred = &candidate;
            break;
        }
    }
    if (preferred == nullptr) return nullptr;

    if (needs_ads_admission) {
        const float distance = length(subtract(preferred->aim_px, screen_center));
        const float observed_size = std::clamp(
            preferred->normalized_size, 0.0f, 1.0f);
        const float ads_activation_radius =
            config_.ads_activation_radius_px *
            (1.0f + 0.75f * observed_size);
        if (distance > ads_activation_radius) return nullptr;
    }
    // The selected detection owns current-frame person geometry. Re-applying
    // an association radius against an older point would replace fresh
    // geometry with stale control state.
    return preferred;
}

bool TargetCoordinator::is_effective_selector_replacement(
    const pipeline_contract::VisionObservationBatch& observations,
    const pipeline_contract::VisionCandidate& candidate) const noexcept {
    // selector_target_changed is an edge notification and may be skipped by
    // the latest-only mailbox. The consumed generation is the durable
    // identity boundary; only the selector's preferred, reliable candidate
    // may use it to bypass the old target association radius.
    return has_target_ && observations.selector_identity_protocol &&
        observations.selector_target_generation != 0 &&
        selector_target_generation_ != 0 &&
        observations.selector_target_generation !=
            selector_target_generation_ &&
        observations.preferred_source_id != 0 &&
        candidate.reliability > 0.0f &&
        candidate.source_id == observations.preferred_source_id;
}

pipeline_contract::TargetPlan TargetCoordinator::no_target_plan(
    double now_seconds,
    std::uint64_t source_frame_id,
    std::uint32_t candidate_count,
    std::uint64_t preferred_source_id) noexcept {
    pipeline_contract::TargetPlan plan{};
    plan.generation = ++generation_;
    plan.source_frame_id = source_frame_id;
    plan.mode = pipeline_contract::ControlMode::Manual;
    plan.ads_acquisition_state = ads_acquisition_state_;
    plan.source_decision_available = source_decision_available_;
    plan.source_decision_outcome = source_decision_outcome_;
    plan.source_decision_reason = source_decision_reason_;
    plan.acquisition_terminal_reason = acquisition_terminal_reason_;
    plan.ads_decision_reason = ads_decision_reason_;
    plan.physical_ads_epoch = physical_ads_epoch_;
    plan.target_acquisition_id = target_acquisition_id_;
    plan.ads_acquisition_active = ads_target_admitted_ && !ads_snap_consumed_;
    plan.ads_acquisition_exists = target_acquisition_id_ != 0;
    plan.ads_acquisition_begin_ns = ads_acquisition_begin_ns_;
    plan.ads_acquisition_complete_ns = ads_acquisition_complete_ns_;
    plan.ads_activation_radius_px = config_.ads_activation_radius_px;
    plan.ads_candidate_count = candidate_count;
    plan.ads_preferred_source_id = preferred_source_id;
    plan.ads_epoch_elapsed_ms = ads_epoch_active_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - ads_epoch_started_seconds_) * 1000.0))
        : 0.0f;
    plan.acquisition_elapsed_ms = ads_target_admitted_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - acquisition_started_seconds_) * 1000.0))
        : 0.0f;
    latest_ = plan;
    return plan;
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
    source_decision_available_ = false;
    source_decision_outcome_ =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    source_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
    // NativeController normally calls begin_ads_epoch() on the physical
    // rising edge. Keep the coordinator deterministic for direct callers and
    // fixtures too, while never rearming a consumed held epoch.
    if (!intent.ads) {
        cue_continuation_active_ = false;
        ads_epoch_active_ = false;
        ads_snap_consumed_ = false;
        ads_target_admitted_ = false;
        target_acquisition_id_ = 0;
        acquisition_started_seconds_ = 0.0;
        acquisition_completed_seconds_ = 0.0;
        ads_acquisition_begin_ns_ = 0;
        ads_acquisition_complete_ns_ = 0;
        ads_center_cross_seen_ = false;
        ads_target_switch_seen_ = false;
        acquisition_terminal_reason_ = pipeline_contract::AdsDecisionReason::None;
        ads_acquisition_state_ =
            pipeline_contract::AdsAcquisitionState::Idle;
        ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
        control_mode_ = pipeline_contract::ControlMode::Manual;
    } else if (!ads_epoch_active_ && !ads_snap_consumed_) {
        ads_epoch_active_ = true;
        ads_epoch_started_seconds_ = now_seconds;
        physical_ads_epoch_ = physical_ads_epoch_ == 0
            ? 1 : physical_ads_epoch_ + 1;
        ads_target_admitted_ = false;
        target_acquisition_id_ = 0;
        acquisition_started_seconds_ = 0.0;
        acquisition_completed_seconds_ = 0.0;
        ads_acquisition_begin_ns_ = 0;
        ads_acquisition_complete_ns_ = 0;
        ads_center_cross_seen_ = false;
        ads_target_switch_seen_ = false;
        acquisition_terminal_reason_ = pipeline_contract::AdsDecisionReason::None;
        ads_acquisition_state_ =
            pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
        ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
        control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
    }
    if (intent.ads && ads_snap_consumed_) {
        ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Consumed;
    }
    // A controller tick without a source publication must not synthesize a
    // projected detector point. Association starts from the last source-owned
    // position and only a newly accepted capture may update geometry.
    const auto association_anchor = position_;
    double observation_capture_seconds = now_seconds;
    const bool source_time_available =
        std::isfinite(observations.source_time_seconds) &&
        (observations.source_time_seconds > 0.0 || now_seconds <= 0.001);
    if (source_time_available) {
        observation_capture_seconds = observations.source_time_seconds;
    }
    bool accepted_fresh_capture = observations.capture_fresh;
    bool stale_capture = false;
    bool duplicate_frame = false;
    if (accepted_fresh_capture && source_time_available) {
        const double source_age_ms =
            (now_seconds - observation_capture_seconds) * 1000.0;
        accepted_fresh_capture =
            source_age_ms >= -0.001 &&
            source_age_ms <=
                static_cast<double>(std::max(0.0f, config_.max_observation_age_ms));
        stale_capture = !accepted_fresh_capture;
    }
    if (accepted_fresh_capture && has_processed_capture_) {
        if (source_time_available && has_processed_capture_time_ &&
            observation_capture_seconds <=
                last_processed_capture_seconds_ + 1.0e-6) {
            accepted_fresh_capture = false;
            duplicate_frame = true;
        }
        if (observations.frame_id != 0 && last_processed_frame_id_ != 0 &&
            observations.frame_id <= last_processed_frame_id_) {
            accepted_fresh_capture = false;
            duplicate_frame = true;
        }
        if (!source_time_available && observations.frame_id == 0) {
            accepted_fresh_capture = false;
            stale_capture = true;
        }
    }
    if (observations.capture_fresh && !source_time_available &&
        observations.frame_id == 0) {
        stale_capture = true;
    }
    if (accepted_fresh_capture) {
        has_processed_capture_ = true;
        if (source_time_available) {
            last_processed_capture_seconds_ = observation_capture_seconds;
            has_processed_capture_time_ = true;
        }
        if (observations.frame_id != 0) {
            last_processed_frame_id_ = observations.frame_id;
        }
        if (observations.frame_width_px > 0.0f) {
            frame_width_px_ = observations.frame_width_px;
        }
        if (observations.frame_height_px > 0.0f) {
            frame_height_px_ = observations.frame_height_px;
        }
        if (observations.frame_id != 0) {
            // A fresh empty miss is still a source frame. It must be visible
            // in the trace even though it has no source observation id.
            source_frame_id_ = observations.frame_id;
        }
    }
    if (accepted_fresh_capture && !observations.selector_identity_protocol) {
        // Once a source frame explicitly lacks the selector identity
        // protocol, the last generation is no longer trustworthy. This is
        // the only non-reset path that clears it; manual/ADS lifecycle
        // transitions themselves must not erase identity continuity.
        selector_target_generation_ = 0;
    }
    const pipeline_contract::Vec2f center{
        frame_width_px_ * 0.5f,
        frame_height_px_ * 0.5f,
    };
    const auto* candidate = accepted_fresh_capture
        ? choose_candidate(observations, association_anchor)
        : nullptr;
    const bool cue_continuation_candidate = candidate != nullptr &&
        observations.selector_cue_continuation &&
        candidate->source_id == 0 && has_target_ && ads_epoch_active_ &&
        observations.selector_target_generation != 0 &&
        observations.selector_target_generation == selector_target_generation_;
    if (accepted_fresh_capture) {
        cue_continuation_active_ = cue_continuation_candidate;
    }
    bool current_plan_admitted = false;
    const bool selector_replacement = candidate != nullptr &&
        accepted_fresh_capture &&
        is_effective_selector_replacement(observations, *candidate);

    if (candidate == nullptr) {
        if (!intent.ads) {
            ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
        } else if (observations.capture_fresh) {
            source_decision_available_ = true;
            source_decision_outcome_ =
                pipeline_contract::SourceDecisionOutcome::Rejected;
            // Reject reasons describe a source-frame decision. A controller
            // replay has no new candidate set, so it must preserve the last
            // source reason instead of fabricating a radius/association
            // rejection from an intentionally empty replay batch.
            if (ads_snap_consumed_) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::AdsAlreadyConsumed;
            } else if (!accepted_fresh_capture) {
                ads_decision_reason_ = duplicate_frame
                    ? pipeline_contract::AdsDecisionReason::DuplicateFrame
                    : stale_capture
                        ? pipeline_contract::AdsDecisionReason::StaleCapture
                        : pipeline_contract::AdsDecisionReason::OldControlEpoch;
            } else if (!observations.selector_identity_protocol) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::InvalidSelectorProtocol;
            } else if (observations.preferred_source_id == 0) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::SelectorNoSelection;
            } else if (observations.rejected_friendly_count > 0) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::FriendlyOrCueReject;
            } else if (observations.rejected_low_reliability_count > 0 ||
                       (observations.count > 0 &&
                        std::all_of(
                            observations.candidates.begin(),
                            observations.candidates.begin() +
                                std::min<std::uint32_t>(
                                    observations.count,
                                    static_cast<std::uint32_t>(
                                        pipeline_contract::kMaxVisionCandidates)),
                            [](const auto& value) {
                                return value.reliability <= 0.0f;
                            }))) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::LowReliability;
            } else if (observations.count == 0) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::NoTarget;
            } else if (ads_epoch_active_ && !ads_snap_consumed_ &&
                       !ads_target_admitted_) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius;
            } else if (!has_target_) {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::OutsideAdsActivationRadius;
            } else {
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::OutsideAssociationRadius;
            }
            source_decision_reason_ = ads_decision_reason_;
        }
    }

    // source_id is frame-local. Only a selector-owned generation may signal a
    // replacement; when that signal is unavailable we deliberately do not
    // infer a switch from an observation id.
    if (accepted_fresh_capture && observations.selector_identity_protocol &&
        observations.selector_target_generation != 0 && candidate != nullptr &&
        observations.preferred_source_id != 0 &&
        candidate->source_id == observations.preferred_source_id &&
        candidate->reliability > 0.0f) {
        if (selector_replacement && ads_target_admitted_ && !ads_snap_consumed_) {
            ads_target_switch_seen_ = true;
        }
        selector_target_generation_ = observations.selector_target_generation;
    }
    if (candidate != nullptr && intent.ads && ads_snap_consumed_) {
        ads_decision_reason_ =
            pipeline_contract::AdsDecisionReason::AdsAlreadyConsumed;
        source_decision_available_ = true;
        source_decision_outcome_ =
            pipeline_contract::SourceDecisionOutcome::Rejected;
        source_decision_reason_ =
            pipeline_contract::AdsDecisionReason::AdsAlreadyConsumed;
    }
    pipeline_contract::TargetLifecycle lifecycle = pipeline_contract::TargetLifecycle::None;
    float reliability = latest_.reliability;
    float normalized_size = latest_.normalized_size;
    if (candidate != nullptr) {
        pipeline_contract::Vec2f observed_aim_px = candidate->aim_px;
        const bool new_observation_sample =
            accepted_fresh_capture && !cue_continuation_candidate;
        const bool new_target = !has_target_ || selector_replacement;
        const bool new_ads_acquisition = intent.ads && ads_epoch_active_ &&
            !ads_snap_consumed_ && !ads_target_admitted_ &&
            !cue_continuation_candidate;
        if (selector_replacement) {
            reset_target_owned_state_for_replacement();
        }
        if (new_ads_acquisition) {
            current_plan_admitted = true;
            source_decision_available_ = true;
            source_decision_outcome_ =
                pipeline_contract::SourceDecisionOutcome::Admitted;
            source_decision_reason_ = pipeline_contract::AdsDecisionReason::Admitted;
            ads_target_admitted_ = true;
            target_acquisition_id_ = next_target_acquisition_id_++;
            acquisition_started_seconds_ = now_seconds;
            acquisition_completed_seconds_ = 0.0;
            ads_acquisition_begin_ns_ = seconds_to_ns(now_seconds);
            ads_acquisition_complete_ns_ = 0;
            ads_acquisition_state_ =
                pipeline_contract::AdsAcquisitionState::AcquiringNominal;
            ads_decision_reason_ = pipeline_contract::AdsDecisionReason::Admitted;
        } else if (accepted_fresh_capture &&
                   !(intent.ads && ads_snap_consumed_)) {
            source_decision_available_ = true;
            source_decision_outcome_ =
                pipeline_contract::SourceDecisionOutcome::AcceptedContinuation;
            source_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
        }
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
        // candidate->aim_px is already the geometry-resolved result for this
        // fresh frame. A historic body/motion anchor must not replace that
        // absolute position; firing noise is bounded only while estimating
        // velocity below.
        if (cue_continuation_candidate) {
            // The selector already reconstructed this position from the live
            // cue plus the last observed person-to-cue offset. Consume the
            // absolute point, but do not learn person velocity from UI motion.
            position_ = observed_aim_px;
            velocity_ = {};
            acceleration_ = {};
            previous_firing_velocity_innovation_ = {};
            firing_velocity_observer_active_ = false;
        } else if (new_target) {
            has_target_ = true;
            target_id_ = next_target_id_++;
            if (!ads_target_admitted_) {
                acquisition_started_seconds_ = 0.0;
                target_acquisition_id_ = 0;
            }
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
            // Association has already accepted this fresh selected candidate.
            // Fresh Vision owns both the absolute point and the inter-capture
            // displacement; no controller-rate projection is folded back in.
            const auto position_innovation = subtract(observed_aim_px, position_);
            auto velocity_innovation = position_innovation;
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
                observed_frames_ >= 2 && !selector_replacement;
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
            // Fresh Vision owns the observed position. Firing disturbance
            // control is only allowed to limit admission into target velocity;
            // otherwise a legitimate center crossing would be held at the old
            // prediction and BodyLock would follow stale position authority.
            if (assisted_motion_model && new_observation_sample &&
                firing_context && observed_frames_ >= 2) {
                const float influence_limit = std::max(
                    0.0f, config_.fire_innovation_limit_px);
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
            const auto measured_position = observed_aim_px;
            auto measured_velocity = pipeline_contract::Vec2f{
                velocity_innovation.x / observation_dt,
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
            pipeline_contract::Vec2f velocity_delta{
                measured_velocity.x - velocity_.x,
                measured_velocity.y - velocity_.y,
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
            ++observed_frames_;
        }
        if (candidate->source_id != 0) {
            source_id_ = candidate->source_id;
        }
        source_frame_id_ = observations.frame_id;
        fire_requested_ = cue_continuation_candidate
            ? false : observations.fire_requested;
        observed_fire_eligible_ = cue_continuation_candidate
            ? false : observations.observed_fire_eligible;
        reliability = std::clamp(candidate->reliability, 0.0f, 1.0f);
        normalized_size = std::clamp(candidate->normalized_size, 0.0f, 1.0f);
        last_observed_reliability_ = reliability;
        last_observed_normalized_size_ = normalized_size;
        if (!cue_continuation_candidate &&
            pipeline_contract::finite(candidate->box_size_px) &&
            candidate->box_size_px.x > 0.0f &&
            candidate->box_size_px.y > 0.0f) {
            last_observed_target_size_px_ = candidate->box_size_px;
        }
        if (new_observation_sample || cue_continuation_candidate) {
            last_observed_seconds_ = observation_capture_seconds;
            last_observation_capture_seconds_ =
                observation_capture_seconds;
            has_observation_capture_time_ = true;
        }
        lifecycle = cue_continuation_candidate
            ? pipeline_contract::TargetLifecycle::CueContinuation
            : pipeline_contract::TargetLifecycle::Observed;
    } else if (has_target_) {
        const float source_age_ms = static_cast<float>(
            std::max(0.0, (now_seconds - last_observed_seconds_) * 1000.0));
        const bool fresh_no_target = accepted_fresh_capture;
        const bool source_expired = source_age_ms >
            std::max(0.0f, config_.max_observation_age_ms);
        if (fresh_no_target || source_expired) {
            has_target_ = false;
            cue_continuation_active_ = false;
            source_id_ = 0;
            settled_frames_ = 0;
            observed_frames_ = 0;
            if (intent.ads && ads_target_admitted_ && !ads_snap_consumed_) {
                ads_acquisition_state_ =
                    pipeline_contract::AdsAcquisitionState::Completed;
                ads_snap_consumed_ = true;
                acquisition_completed_seconds_ = now_seconds;
                ads_acquisition_complete_ns_ = seconds_to_ns(now_seconds);
                ads_decision_reason_ =
                    pipeline_contract::AdsDecisionReason::TargetLost;
                acquisition_terminal_reason_ =
                    pipeline_contract::AdsDecisionReason::TargetLost;
                control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
            } else if (intent.ads && !ads_snap_consumed_) {
                ads_acquisition_state_ =
                    pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
            }
            last_update_seconds_ = now_seconds;
            return no_target_plan(
                now_seconds,
                observations.frame_id,
                observations.count + observations.rejected_friendly_count +
                    observations.rejected_low_reliability_count,
                observations.preferred_source_id);
        }
        // No source publication occurred on this controller tick. Continue
        // the exact last source-owned point and authority until the next fresh
        // result or the hard source-age safety gate above. Do not project,
        // decay, or create a synthetic lifecycle transition.
        lifecycle = latest_.lifecycle;
        reliability = last_observed_reliability_;
        normalized_size = last_observed_normalized_size_;
    } else {
        if (intent.ads && !ads_snap_consumed_) {
            ads_acquisition_state_ =
                pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
        }
        last_update_seconds_ = now_seconds;
        return no_target_plan(
            now_seconds,
            observations.frame_id,
            observations.count + observations.rejected_friendly_count +
                observations.rejected_low_reliability_count,
            observations.preferred_source_id);
    }

    pipeline_contract::TargetPlan plan{};
    plan.generation = ++generation_;
    plan.source_frame_id = source_frame_id_;
    plan.source_observation_id = cue_continuation_active_
        ? 0 : candidate != nullptr ? source_id_ : 0;
    plan.target_id = target_id_;
    plan.lifecycle = lifecycle;
    plan.aim_px = position_;
    plan.error_px = subtract(position_, center);
    const pipeline_contract::Vec2f screen_velocity = velocity_;
    plan.velocity_px_per_sec = screen_velocity;
    plan.acceleration_px_per_sec2 = acceleration_;
    plan.observation_age_ms = static_cast<float>((now_seconds - last_observed_seconds_) * 1000.0);
    plan.confidence = reliability;
    plan.reliability = reliability;
    plan.normalized_size = normalized_size;
    const float error_length = length(plan.error_px);
    plan.acquisition_elapsed_ms = ads_target_admitted_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - acquisition_started_seconds_) * 1000.0))
        : 0.0f;
    plan.ads_epoch_elapsed_ms = ads_epoch_active_
        ? static_cast<float>(
            std::max(0.0, (now_seconds - ads_epoch_started_seconds_) * 1000.0))
        : 0.0f;
    plan.ads_acquisition_state = ads_acquisition_state_;
    plan.ads_decision_reason = ads_decision_reason_;
    plan.physical_ads_epoch = physical_ads_epoch_;
    plan.target_acquisition_id = target_acquisition_id_;
    plan.ads_plan_admitted = current_plan_admitted;
    plan.ads_acquisition_active = ads_target_admitted_ && !ads_snap_consumed_;
    plan.ads_acquisition_exists = target_acquisition_id_ != 0;
    plan.ads_acquisition_begin_ns = ads_acquisition_begin_ns_;
    plan.ads_acquisition_complete_ns = ads_acquisition_complete_ns_;
    plan.ads_activation_radius_px = config_.ads_activation_radius_px;
    plan.ads_raw_error_px = plan.error_px;
    plan.ads_candidate_count = std::min<std::uint32_t>(
        observations.count,
        static_cast<std::uint32_t>(pipeline_contract::kMaxVisionCandidates));
    plan.ads_preferred_source_id = observations.preferred_source_id;
    plan.ads_selected_source_id = candidate != nullptr
        ? candidate->source_id : 0;
    plan.selector_target_generation = observations.selector_target_generation;
    // Expose the consumed/effective identity boundary. The raw selector
    // changed pulse is intentionally not authoritative because latest-only
    // delivery may skip it or deliver a spurious pulse for the same gen.
    plan.selector_target_changed = selector_replacement;
    plan.cue_continuation = cue_continuation_active_;
    plan.ads_target_size_px = last_observed_target_size_px_;
    if (candidate != nullptr && !cue_continuation_candidate) {
        plan.ads_activation_radius_px = config_.ads_activation_radius_px *
            (1.0f + 0.75f * std::clamp(candidate->normalized_size, 0.0f, 1.0f));
    }
    plan.source_capture_age_ms = has_observation_capture_time_
        ? static_cast<float>(std::max(
            0.0, (now_seconds - last_observation_capture_seconds_) * 1000.0))
        : plan.observation_age_ms;
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
    if (candidate != nullptr && !cue_continuation_candidate) {
        if (inside_capture_set) {
            ++settled_frames_;
        } else {
            settled_frames_ = 0;
        }
    }
    const bool previous_target_same = latest_.target_id != 0 &&
        latest_.target_id == target_id_;
    const float previous_error_length = length(latest_.error_px);
    const float current_error_length = length(plan.error_px);
    const float radial_error_dot = latest_.error_px.x * plan.error_px.x +
        latest_.error_px.y * plan.error_px.y;
    // A sign flip on one noisy axis is not a center crossing. Require a
    // meaningful radial reversal with both samples away from the deadzone.
    const bool center_cross = previous_target_same && candidate != nullptr &&
        !cue_continuation_candidate &&
        accepted_fresh_capture && previous_error_length >= 6.0f &&
        current_error_length >= 2.0f && radial_error_dot < -std::max(
            4.0f, previous_error_length * current_error_length * 0.25f);
    if (center_cross) ads_center_cross_seen_ = true;

    const bool settled = settled_frames_ >= config_.settle_frames;
    const bool fresh_eligible = candidate != nullptr &&
        !cue_continuation_candidate && accepted_fresh_capture &&
        reliability > 0.0f &&
        lifecycle == pipeline_contract::TargetLifecycle::Observed;
    const bool acquisition_ceiling_elapsed = ads_target_admitted_ &&
        config_.ads_max_acquisition_ms > 0.0f &&
        plan.acquisition_elapsed_ms >= config_.ads_max_acquisition_ms;
    const bool nominal_elapsed = ads_target_admitted_ &&
        plan.acquisition_elapsed_ms >=
            std::max(0.0f, config_.ads_nominal_acquisition_ms);
    const bool continued_force_helpful = fresh_eligible &&
        !settled &&
        !ads_center_cross_seen_ &&
        !ads_target_switch_seen_;

    if (!intent.ads) {
        control_mode_ = pipeline_contract::ControlMode::Manual;
    } else if (ads_snap_consumed_) {
        ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Consumed;
        control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
    } else if (ads_target_admitted_) {
        if (settled) {
            ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Completed;
            ads_decision_reason_ = pipeline_contract::AdsDecisionReason::Settled;
            acquisition_completed_seconds_ = now_seconds;
            ads_acquisition_complete_ns_ = seconds_to_ns(now_seconds);
            ads_snap_consumed_ = true;
            control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
        } else if (acquisition_ceiling_elapsed) {
            ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Completed;
            ads_decision_reason_ = pipeline_contract::AdsDecisionReason::AcquisitionCeiling;
            acquisition_completed_seconds_ = now_seconds;
            ads_acquisition_complete_ns_ = seconds_to_ns(now_seconds);
            ads_snap_consumed_ = true;
            control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
        } else if (nominal_elapsed &&
                   ads_acquisition_state_ ==
                       pipeline_contract::AdsAcquisitionState::AcquiringNominal) {
            if (!accepted_fresh_capture) {
                // The controller commonly crosses 135ms while replaying the
                // latest Vision result. A replay is not evidence of any of
                // the fresh-source early-exit conditions. Keep the nominal
                // acquisition pending until a fresh authoritative frame
                // decides whether to extend or complete.
                ads_acquisition_state_ =
                    pipeline_contract::AdsAcquisitionState::AcquiringNominal;
                control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
            } else if (continued_force_helpful) {
                ads_acquisition_state_ =
                    pipeline_contract::AdsAcquisitionState::AcquiringExtended;
                ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
                control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
            } else {
                ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Completed;
                ads_decision_reason_ = center_cross || ads_center_cross_seen_
                    ? pipeline_contract::AdsDecisionReason::CenterCross
                    : ads_target_switch_seen_
                        ? pipeline_contract::AdsDecisionReason::TargetSwitch
                    : !fresh_eligible
                            ? pipeline_contract::AdsDecisionReason::TargetLost
                            : pipeline_contract::AdsDecisionReason::NonHelpfulOutput;
                acquisition_completed_seconds_ = now_seconds;
                ads_acquisition_complete_ns_ = seconds_to_ns(now_seconds);
                ads_snap_consumed_ = true;
                control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
            }
        } else if (ads_acquisition_state_ ==
                   pipeline_contract::AdsAcquisitionState::AcquiringExtended) {
            // A controller replay tick has no new source decision. Preserve
            // Extended until a fresh frame proves target loss, settle,
            // confirmed center crossing, or identity switch;
            // never oscillate through Nominal merely because Vision did not
            // publish this tick. The moving-away sample is diagnostic only.
            if (accepted_fresh_capture && !continued_force_helpful) {
                ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Completed;
                ads_decision_reason_ = center_cross || ads_center_cross_seen_
                    ? pipeline_contract::AdsDecisionReason::CenterCross
                    : ads_target_switch_seen_
                            ? pipeline_contract::AdsDecisionReason::TargetSwitch
                        : !fresh_eligible
                            ? pipeline_contract::AdsDecisionReason::TargetLost
                            : pipeline_contract::AdsDecisionReason::NonHelpfulOutput;
                acquisition_completed_seconds_ = now_seconds;
                ads_acquisition_complete_ns_ = seconds_to_ns(now_seconds);
                ads_snap_consumed_ = true;
                control_mode_ = pipeline_contract::ControlMode::BodyLockFollow;
            } else {
                ads_acquisition_state_ =
                    pipeline_contract::AdsAcquisitionState::AcquiringExtended;
                control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
            }
        } else {
            ads_acquisition_state_ =
                pipeline_contract::AdsAcquisitionState::AcquiringNominal;
            control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
        }
    } else if (intent.ads) {
        ads_acquisition_state_ =
            pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
        control_mode_ = pipeline_contract::ControlMode::Manual;
    } else {
        control_mode_ = pipeline_contract::ControlMode::Manual;
    }
    if (ads_acquisition_state_ ==
        pipeline_contract::AdsAcquisitionState::Completed) {
        acquisition_terminal_reason_ = ads_decision_reason_;
    }
    plan.mode = control_mode_;
    plan.ads_acquisition_state = ads_acquisition_state_;
    plan.source_decision_available = source_decision_available_;
    plan.source_decision_outcome = source_decision_outcome_;
    plan.source_decision_reason = source_decision_reason_;
    plan.acquisition_terminal_reason = acquisition_terminal_reason_;
    plan.ads_decision_reason = ads_decision_reason_;
    plan.ads_acquisition_complete_ns = ads_acquisition_complete_ns_;
    if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        // BodyLock starts from the current source-owned target point.
    }
    plan.ads_demand = std::clamp(
        error_length / std::max(1.0f, config_.ads_activation_radius_px),
        0.0f, 1.0f);
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
    if (bodylock_outside_activation_range &&
        plan.ads_decision_reason == pipeline_contract::AdsDecisionReason::None) {
        plan.ads_decision_reason =
            pipeline_contract::AdsDecisionReason::BodylockOutsideContinuation;
        ads_decision_reason_ = plan.ads_decision_reason;
    }
    plan.aim_authority =
        plan.mode == pipeline_contract::ControlMode::Manual ||
            bodylock_outside_activation_range
        ? 0.0f
        : std::min(
            config_.max_authority,
            std::max(0.0f, reliability));
    plan.response_scale = std::max(
        50.0f, feedback.aim_response_px_per_stick_second);
    plan.response_confidence = std::clamp(
        feedback.aim_response_confidence, 0.0f, 1.0f);
    plan.error_rate_px_per_sec = screen_velocity;
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
    plan.fire_authority = observed_fire_eligible_ && !plan.cue_continuation;
    plan.fire_requested = fire_requested_;
    plan.fire_suppression = plan.fire_authority
        ? pipeline_contract::FireSuppressionReason::None
        : lifecycle == pipeline_contract::TargetLifecycle::CueContinuation
            ? pipeline_contract::FireSuppressionReason::Stale
            : error_length > 6.0f
                ? pipeline_contract::FireSuppressionReason::LargeError
                : length(screen_velocity) > 120.0f
                    ? pipeline_contract::FireSuppressionReason::HighVelocity
                    : pipeline_contract::FireSuppressionReason::Ambiguous;
    latest_ = plan;
    last_update_seconds_ = now_seconds;
    return plan;
}

void TargetCoordinator::begin_ads_epoch(
    std::uint64_t epoch, double now_seconds) noexcept {
    cue_continuation_active_ = false;
    physical_ads_epoch_ = epoch;
    ads_epoch_started_seconds_ = now_seconds;
    ads_epoch_active_ = true;
    ads_snap_consumed_ = false;
    ads_target_admitted_ = false;
    target_acquisition_id_ = 0;
    acquisition_started_seconds_ = 0.0;
    acquisition_completed_seconds_ = 0.0;
    ads_acquisition_begin_ns_ = 0;
    ads_acquisition_complete_ns_ = 0;
    ads_center_cross_seen_ = false;
    ads_target_switch_seen_ = false;
    source_decision_available_ = false;
    source_decision_outcome_ =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    source_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
    acquisition_terminal_reason_ = pipeline_contract::AdsDecisionReason::None;
    ads_acquisition_state_ =
        pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
    ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
    control_mode_ = pipeline_contract::ControlMode::AdsAcquire;
}

void TargetCoordinator::reset() noexcept {
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
    last_processed_frame_id_ = 0;
    last_observed_seconds_ = 0.0;
    last_observation_capture_seconds_ = 0.0;
    last_processed_capture_seconds_ = 0.0;
    last_update_seconds_ = 0.0;
    acquisition_started_seconds_ = 0.0;
    acquisition_completed_seconds_ = 0.0;
    ads_epoch_started_seconds_ = 0.0;
    last_observed_reliability_ = 0.0f;
    last_observed_normalized_size_ = 0.0f;
    last_observed_target_size_px_ = {};
    settled_frames_ = 0;
    observed_frames_ = 0;
    has_target_ = false;
    has_observation_capture_time_ = false;
    has_processed_capture_ = false;
    has_processed_capture_time_ = false;
    fire_requested_ = false;
    observed_fire_eligible_ = false;
    cue_continuation_active_ = false;
    ads_epoch_active_ = false;
    ads_snap_consumed_ = false;
    ads_target_admitted_ = false;
    physical_ads_epoch_ = 0;
    target_acquisition_id_ = 0;
    next_target_acquisition_id_ = 1;
    ads_acquisition_state_ = pipeline_contract::AdsAcquisitionState::Idle;
    ads_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
    ads_acquisition_begin_ns_ = 0;
    ads_acquisition_complete_ns_ = 0;
    ads_center_cross_seen_ = false;
    ads_target_switch_seen_ = false;
    selector_target_generation_ = 0;
    source_decision_available_ = false;
    source_decision_outcome_ =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    source_decision_reason_ = pipeline_contract::AdsDecisionReason::None;
    acquisition_terminal_reason_ = pipeline_contract::AdsDecisionReason::None;
    control_mode_ = pipeline_contract::ControlMode::Manual;
    frame_width_px_ = 480.0f;
    frame_height_px_ = 416.0f;
}

}  // namespace controller_native
