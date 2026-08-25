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
    // The configured nominal + extension window elapsed without proof of
    // arrival. The same ADS solver may keep pursuing a valid target, but final
    // arbitration may no longer suppress material player input.
    AcquiringManualSafe,
};

// Stable, machine-readable reasons used by the plan/telemetry join.  Keep
// these values free of presentation text; runtime serializers provide names.
enum class AdsDecisionReason : unsigned char {
    None,
    Admitted,
    InvalidSelectorProtocol,
    SelectorNoSelection,
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
    // Append new wire-visible values so existing enum ordinals remain stable.
    ExtensionBudgetElapsed,
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

enum class DesiredPointSource : unsigned char {
    None,
    VisionDefault,
    UserCorrected,
    CueCarried,
};

struct TargetPlan {
    std::uint64_t generation = 0;
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t target_id = 0;
    TargetLifecycle lifecycle = TargetLifecycle::None;
    // True only when this exact controller update consumed a fresh,
    // selector-selected person observation. Replay and cue continuation never
    // inherit this bit; digital actions may therefore use it without creating
    // a second freshness/identity owner.
    bool direct_person_observation = false;
    TargetMotion motion = TargetMotion::Ambiguous;
    ControlMode mode = ControlMode::Manual;
    // I/R/D/T contract:
    // - source_aim_px is the source-selected anatomical point;
    // - aim_region_px is R, the currently valid/hittable region;
    // - aim_px is D, the sole desired impact point and must remain inside R.
    Vec2f source_aim_px{};
    common_native::Box2f aim_region_px{};
    AimRegionSource aim_region_source = AimRegionSource::None;
    bool has_aim_region = false;
    Vec2f aim_px{};
    Vec2f desired_point_normalized{};
    DesiredPointSource desired_point_source = DesiredPointSource::None;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_boundary_x = false;
    bool manual_boundary_y = false;
    bool manual_exit_requested = false;
    Vec2f error_px{};
    Vec2f error_rate_px_per_sec{};
    Vec2f velocity_px_per_sec{};
    Vec2f acceleration_px_per_sec2{};
    // Capture-aligned estimate of the selected target's world-relative screen
    // motion after removing camera work already delivered during the same
    // source interval. The aligned command is in normalized camera-response
    // coordinates (before inverse curve mapping), despite the compatibility
    // field name. BodyLock consumes this as total target-follow demand; ADS
    // never reads it.
    Vec2f bodylock_target_motion_px_per_sec{};
    Vec2f bodylock_aligned_delivered_stick{};
    float bodylock_target_motion_confidence = 0.0f;
    bool bodylock_target_motion_valid = false;
    float observation_age_ms = 0.0f;
    float confidence = 0.0f;
    float reliability = 0.0f;
    // Selector-owned enemy evidence and the resulting bounded control budget.
    // Geometry reliability answers "where can we aim?"; visual authority also
    // answers "is this selected person sufficiently enemy-confirmed?".
    bool enemy_cue_current = false;
    bool enemy_identity_confirmed = false;
    bool enemy_cue_checked = false;
    float visual_authority = 0.0f;
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
    const bool region_finite = std::isfinite(plan.aim_region_px.x) &&
        std::isfinite(plan.aim_region_px.y) &&
        std::isfinite(plan.aim_region_px.w) &&
        std::isfinite(plan.aim_region_px.h);
    const bool region_contract_valid = !plan.has_aim_region ||
        (region_finite && plan.aim_region_px.w > 0.0f &&
         plan.aim_region_px.h > 0.0f &&
         plan.aim_px.x >= plan.aim_region_px.x - 0.001f &&
         plan.aim_px.x <= plan.aim_region_px.x + plan.aim_region_px.w + 0.001f &&
         plan.aim_px.y >= plan.aim_region_px.y - 0.001f &&
         plan.aim_px.y <= plan.aim_region_px.y + plan.aim_region_px.h + 0.001f &&
         plan.desired_point_normalized.x >= -0.001f &&
         plan.desired_point_normalized.x <= 1.001f &&
         plan.desired_point_normalized.y >= -0.001f &&
         plan.desired_point_normalized.y <= 1.001f);
    return finite(plan.source_aim_px) && finite(plan.aim_px) &&
           finite(plan.desired_point_normalized) && region_contract_valid &&
           finite(plan.error_px) && finite(plan.error_rate_px_per_sec) &&
           finite(plan.velocity_px_per_sec) && finite(plan.acceleration_px_per_sec2) &&
           finite(plan.bodylock_target_motion_px_per_sec) &&
           finite(plan.bodylock_aligned_delivered_stick) &&
           unit_interval(plan.bodylock_target_motion_confidence) &&
           unit_interval(plan.confidence) && unit_interval(plan.reliability) &&
           unit_interval(plan.visual_authority) &&
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
