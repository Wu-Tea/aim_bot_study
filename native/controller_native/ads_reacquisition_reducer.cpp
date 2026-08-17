#include "ads_reacquisition_reducer.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

AdsReacquisitionDecision AdsReacquisitionReducer::on_input(
    const AimScopeSnapshot& scope,
    const pipeline_contract::TargetPlan& current_plan,
    pipeline_contract::EventSequence cause_event) noexcept {
    if (scope.physical_ads_released || scope.scope_released) {
        if (!pending_.active) return {};
        const auto cause = pending_.cause_event;
        pending_ = {};
        return {
            true,
            false,
            false,
            pipeline_contract::AdsReacquireDecisionCode::CancelledByAdsRelease,
            cause};
    }

    const bool target_present = current_plan.target_id != 0;
    if (scope.scope_acquired) {
        // A newly acquired assist scope is the first ADS job, not a BodyLock
        // re-press. Vision may already have published a person while LT/fire
        // was idle; deferring that edge leaves the coordinator with a manual
        // target and no epoch that can ever admit it.
        pending_ = {};
        return {
            true,
            true,
            true,
            pipeline_contract::AdsReacquireDecisionCode::InitialScopeAcquired,
            cause_event};
    }
    const bool explicit_physical_ads_request = scope.physical_ads_pressed;
    // Fire may establish its own aim scope, but it must never rearm the ADS
    // lifecycle while the same physical LT press is already active.
    const bool fire_search_request = scope.manual_fire_pressed &&
        !scope.physical_ads_active && !target_present;
    if (!explicit_physical_ads_request &&
        !fire_search_request) {
        return {};
    }
    if (current_plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        target_present) {
        return {
            true,
            true,
            false,
            pipeline_contract::AdsReacquireDecisionCode::CoveredByActiveAdsSnap,
            cause_event};
    }
    if (!target_present) {
        pending_ = {};
        return {
            true,
            true,
            true,
            pipeline_contract::AdsReacquireDecisionCode::RearmedWaitingForTarget,
            cause_event};
    }

    pending_.active = true;
    pending_.cause_event = cause_event;
    pending_.after_frame = pipeline_contract::VisionFrameId::from(
        current_plan.source_frame_id);
    pending_.bound_target_generation =
        pipeline_contract::TargetGeneration::from(
            current_plan.selector_target_generation);
    pending_.fresh_frames_waited = 0;
    return {
        true,
        true,
        false,
        pipeline_contract::AdsReacquireDecisionCode::DeferredWaitingForFreshVision,
        cause_event,
        pending_.after_frame,
        pending_.bound_target_generation};
}

AdsReacquisitionDecision AdsReacquisitionReducer::on_fresh_observation(
    const pipeline_contract::VisionObservationBatch& observations,
    const pipeline_contract::TargetPlan& current_plan,
    bool physical_ads_active) noexcept {
    if (!pending_.active) return {};
    if (!physical_ads_active) {
        const auto cause = pending_.cause_event;
        pending_ = {};
        return {
            true, false, false,
            pipeline_contract::AdsReacquireDecisionCode::CancelledByAdsRelease,
            cause};
    }
    if (!observations.capture_fresh || observations.frame_id == 0 ||
        (pending_.after_frame.valid &&
         observations.frame_id <= pending_.after_frame.value)) {
        return {};
    }
    ++pending_.fresh_frames_waited;
    if (current_plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        const auto cause = pending_.cause_event;
        pending_ = {};
        return {
            true, false, false,
            pipeline_contract::AdsReacquireDecisionCode::CoveredByActiveAdsSnap,
            cause};
    }
    if (pending_.bound_target_generation.valid &&
        observations.selector_target_generation != 0 &&
        observations.selector_target_generation !=
            pending_.bound_target_generation.value) {
        const auto cause = pending_.cause_event;
        pending_ = {};
        return {
            true, false, false,
            pipeline_contract::AdsReacquireDecisionCode::
                CancelledByTargetGenerationChange,
            cause};
    }

    const pipeline_contract::VisionCandidate* selected = nullptr;
    for (std::uint32_t index = 0;
         index < std::min<std::uint32_t>(
             observations.count,
             static_cast<std::uint32_t>(
                 pipeline_contract::kMaxVisionCandidates));
         ++index) {
        const auto& candidate = observations.candidates[index];
        if (candidate.source_id == observations.preferred_source_id &&
            candidate.has_aim_point && candidate.reliability > 0.0f) {
            selected = &candidate;
            break;
        }
    }
    if (selected == nullptr) {
        if (pending_.fresh_frames_waited <
            std::max(1u, config_.max_fresh_frames_waiting)) {
            return {};
        }
        const auto cause = pending_.cause_event;
        pending_ = {};
        return {
            true, false, false,
            pipeline_contract::AdsReacquireDecisionCode::
                ExpiredWithoutEligibleObservation,
            cause};
    }

    const pipeline_contract::Vec2f center{
        observations.frame_width_px * 0.5f,
        observations.frame_height_px * 0.5f};
    const float error_px = std::hypot(
        selected->aim_px.x - center.x,
        selected->aim_px.y - center.y);
    const float radius_px = target_scaled_radius(
        config_.bodylock_base_radius_px,
        selected->normalized_size);
    const bool rearm = error_px > radius_px;
    const auto cause = pending_.cause_event;
    const auto after_frame = pending_.after_frame;
    const auto bound_generation = pending_.bound_target_generation;
    pending_ = {};
    return {
        true,
        false,
        rearm,
        rearm
            ? pipeline_contract::AdsReacquireDecisionCode::
                RearmedOutsideBodylockEnvelope
            : pipeline_contract::AdsReacquireDecisionCode::
                KeptBodylockWithinEnvelope,
        cause,
        after_frame,
        bound_generation,
        error_px,
        radius_px};
}

}  // namespace controller_native
