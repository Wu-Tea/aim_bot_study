#pragma once

#include "pipeline_contract/vision_observation.h"

#include <cmath>
#include <cstdint>

namespace pipeline_contract {

enum class TargetLifecycle : unsigned char {
    None,
    Observed,
    CueContinuation,
};

enum class TargetMotion : unsigned char {
    Ambiguous,
    Steady,
    Strafe,
    Jump,
    Fall,
};

enum class ControlMode : unsigned char {
    Manual,
    AdsAcquire,
    BodyLockFollow,
};

// ADS ownership is a target-acquisition lifecycle, not a timer measured from
// the physical LT edge.  The coordinator is the single owner of this state.
enum class AdsAcquisitionState : unsigned char {
    Idle,
    ArmedWaitingForTarget,
    AcquiringNominal,
    AcquiringExtended,
    Completed,
    Consumed,
};

// Stable, machine-readable reasons used by the plan/telemetry join.  Keep
// these values free of presentation text; runtime serializers provide names.
enum class AdsDecisionReason : unsigned char {
    None,
    Admitted,
    InvalidSelectorProtocol,
    SelectorNoSelection,
    OutsideAdsActivationRadius,
    OutsideAssociationRadius,
    StaleCapture,
    DuplicateFrame,
    OldControlEpoch,
    LowReliability,
    FriendlyOrCueReject,
    BodylockOutsideContinuation,
    AdsAlreadyConsumed,
    AcquisitionCeiling,
    Settled,
    CenterCross,
    NonHelpfulOutput,
    TargetLost,
    TargetSwitch,
    NoTarget,
};

enum class SourceDecisionOutcome : unsigned char {
    NoDecision,
    Admitted,
    AcceptedContinuation,
    Rejected,
};

enum class FireSuppressionReason : unsigned char {
    None,
    NoTarget,
    Stale,
    Ambiguous,
    LargeError,
    HighVelocity,
    ManualReject,
    Cadence,
    AimOnly,
};

struct TargetPlan {
    std::uint64_t generation = 0;
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t target_id = 0;
    TargetLifecycle lifecycle = TargetLifecycle::None;
    TargetMotion motion = TargetMotion::Ambiguous;
    ControlMode mode = ControlMode::Manual;
    Vec2f aim_px{};
    Vec2f error_px{};
    Vec2f error_rate_px_per_sec{};
    Vec2f velocity_px_per_sec{};
    Vec2f acceleration_px_per_sec2{};
    float observation_age_ms = 0.0f;
    float confidence = 0.0f;
    float reliability = 0.0f;
    float normalized_size = 0.0f;
    float ads_demand = 0.0f;
    float bodylock_demand = 0.0f;
    float aim_authority = 0.0f;
    float response_scale = 0.0f;
    float response_confidence = 0.0f;
    float acquisition_elapsed_ms = 0.0f;
    float ads_epoch_elapsed_ms = 0.0f;
    float source_capture_age_ms = 0.0f;
    AdsAcquisitionState ads_acquisition_state = AdsAcquisitionState::Idle;
    // ads_decision_reason is the epoch/lifecycle reason. These fields are a
    // separate per-fresh-source-frame decision contract; replay ticks leave
    // them at NoDecision/unavailable.
    bool source_decision_available = false;
    SourceDecisionOutcome source_decision_outcome = SourceDecisionOutcome::NoDecision;
    AdsDecisionReason source_decision_reason = AdsDecisionReason::None;
    AdsDecisionReason acquisition_terminal_reason = AdsDecisionReason::None;
    AdsDecisionReason ads_decision_reason = AdsDecisionReason::None;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t target_acquisition_id = 0;
    // Per-frame admission is distinct from the existence and continuation of
    // the epoch-owned acquisition. A non-zero acquisition id alone must not
    // be serialized as admission of the current frame.
    bool ads_plan_admitted = false;
    bool ads_acquisition_active = false;
    bool ads_acquisition_exists = false;
    std::uint64_t ads_acquisition_begin_ns = 0;
    std::uint64_t ads_acquisition_complete_ns = 0;
    float ads_activation_radius_px = 0.0f;
    Vec2f ads_raw_error_px{};
    Vec2f ads_target_size_px{};
    std::uint32_t ads_candidate_count = 0;
    std::uint64_t ads_preferred_source_id = 0;
    std::uint64_t ads_selected_source_id = 0;
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    // Current geometry comes from a same-generation selector cue rather than
    // a person detection. This is aim-only, bounded continuity authority.
    bool cue_continuation = false;
    Vec2f predicted_terminal_error_px{};
    float radial_closing_velocity_px_per_sec = 0.0f;
    bool fire_authority = false;
    bool fire_requested = false;
    FireSuppressionReason fire_suppression = FireSuppressionReason::NoTarget;
};

inline bool finite(Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline bool unit_interval(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

inline bool valid(const TargetPlan& plan) noexcept {
    return finite(plan.aim_px) &&
           finite(plan.error_px) && finite(plan.error_rate_px_per_sec) &&
           finite(plan.velocity_px_per_sec) && finite(plan.acceleration_px_per_sec2) &&
           unit_interval(plan.confidence) && unit_interval(plan.reliability) &&
           unit_interval(plan.normalized_size) && unit_interval(plan.ads_demand) &&
           unit_interval(plan.bodylock_demand) && unit_interval(plan.aim_authority) &&
           unit_interval(plan.response_confidence) &&
           std::isfinite(plan.response_scale) &&
           std::isfinite(plan.acquisition_elapsed_ms) &&
           std::isfinite(plan.ads_epoch_elapsed_ms) &&
           std::isfinite(plan.source_capture_age_ms) &&
           std::isfinite(plan.ads_activation_radius_px) &&
           plan.ads_activation_radius_px >= 0.0f &&
           finite(plan.ads_raw_error_px) &&
           finite(plan.ads_target_size_px) &&
           finite(plan.predicted_terminal_error_px) &&
           std::isfinite(plan.radial_closing_velocity_px_per_sec);
}

}  // namespace pipeline_contract
