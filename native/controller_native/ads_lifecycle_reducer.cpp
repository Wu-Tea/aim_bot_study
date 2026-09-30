#include "ads_lifecycle_reducer.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
bool AdsLifecycleReducer::select(const AdsSelectionEvidence& evidence, double now_seconds) noexcept {
    if (evidence.replacement && evidence.scope_active && state_.epoch_active &&
        !evidence.cue && state_.target_admitted && !state_.snap_consumed) {
        complete(pipeline_contract::AdsDecisionReason::TargetSwitch, now_seconds);
    }
    state_.evidence_wait = AdsEvidenceWait::None;
    if (evidence.scope_active && state_.snap_consumed) {
        mark_already_consumed();
        return false;
    }
    if (evidence.scope_active && state_.epoch_active && !state_.snap_consumed &&
        !state_.target_admitted && !evidence.cue) {
        if (evidence.within_pickup) {
            admit_target(now_seconds);
            return true;
        }
        reject_source(pipeline_contract::AdsDecisionReason::OutsidePickupEnvelope);
    } else {
        accept_continuation();
    }
    return false;
}

AdsTargetDisposition AdsLifecycleReducer::missing(const AdsMissingEvidence& evidence, double now_seconds) noexcept {
    if (!evidence.target_present) {
        if (evidence.scope_active && !state_.snap_consumed) wait_for_target();
        return AdsTargetDisposition::Absent;
    }
    const bool unfinished = evidence.scope_active && state_.target_admitted && !state_.snap_consumed;
    const bool waiting = state_.evidence_wait == AdsEvidenceWait::SameTarget;
    if (unfinished && !evidence.expired && (evidence.same_generation ||
        (waiting && (!evidence.fresh_miss || evidence.same_generation)))) {
        wait_for_target();
        state_.task_mode = pipeline_contract::ControlMode::Manual;
        return AdsTargetDisposition::Wait;
    }
    if (evidence.fresh_miss || evidence.expired || waiting) {
        state_.evidence_wait = AdsEvidenceWait::None;
        if (unfinished) {
            complete(pipeline_contract::AdsDecisionReason::TargetLost, now_seconds);
            state_.task_mode = pipeline_contract::ControlMode::Manual;
        } else if (evidence.scope_active && !state_.snap_consumed) wait_for_target();
        return AdsTargetDisposition::Release;
    }
    return AdsTargetDisposition::Retain;
}

namespace {

std::uint64_t seconds_to_ns(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    return static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
}

}  // namespace

void AdsLifecycleReducer::reset() noexcept {
    state_ = {};
    next_target_acquisition_id_ = 1;
}

void AdsLifecycleReducer::begin_tick(bool scope_active, double now_seconds) noexcept {
    state_.source_decision_available = false;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    state_.source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    if (!scope_active) {
        release_scope();
        state_.task_mode = pipeline_contract::ControlMode::Manual;
    }
    if (scope_active && state_.snap_consumed) {
        consume();
    }
    const bool wait_deadline_elapsed = scope_active && state_.epoch_active &&
        !state_.snap_consumed && !state_.target_admitted &&
        config_.target_wait_ms > 0.0f &&
        (now_seconds - state_.epoch_started_seconds) * 1000.0 >=
            static_cast<double>(config_.target_wait_ms);
    if (wait_deadline_elapsed) {
        // The pre-admission opportunity has its own deadline. Expiry consumes
        // the token before candidate selection on this tick, so a later target
        // may use BodyLock but cannot retroactively mint an ADS Snap.
        expire_wait(
            pipeline_contract::AdsDecisionReason::NoTarget,
            now_seconds);
        state_.task_mode = pipeline_contract::ControlMode::Manual;
    }

}

void AdsLifecycleReducer::begin_epoch(
    std::uint64_t epoch,
    double now_seconds) noexcept {
    state_.physical_ads_epoch = epoch;
    state_.epoch_started_seconds = now_seconds;
    state_.epoch_active = true;
    state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
    state_.evidence_wait = AdsEvidenceWait::InitialTarget;
    state_.snap_consumed = false;
    state_.target_admitted = false;
    state_.target_acquisition_id = 0;
    state_.acquisition_started_seconds = 0.0;
    state_.acquisition_completed_seconds = 0.0;
    state_.acquisition_begin_ns = 0;
    state_.acquisition_complete_ns = 0;
    state_.center_cross_seen = false;
    state_.target_switch_seen = false;
    state_.source_decision_available = false;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    state_.source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    state_.terminal_reason = pipeline_contract::AdsDecisionReason::None;
    state_.state =
        pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
    state_.decision_reason = pipeline_contract::AdsDecisionReason::None;
}

void AdsLifecycleReducer::release_scope() noexcept {
    state_.task_mode = pipeline_contract::ControlMode::Manual;
    state_.evidence_wait = AdsEvidenceWait::None;
    state_.epoch_active = false;
    state_.snap_consumed = false;
    state_.target_admitted = false;
    state_.target_acquisition_id = 0;
    state_.acquisition_started_seconds = 0.0;
    state_.acquisition_completed_seconds = 0.0;
    state_.acquisition_begin_ns = 0;
    state_.acquisition_complete_ns = 0;
    state_.center_cross_seen = false;
    state_.target_switch_seen = false;
    state_.terminal_reason = pipeline_contract::AdsDecisionReason::None;
    state_.state = pipeline_contract::AdsAcquisitionState::Idle;
    state_.decision_reason = pipeline_contract::AdsDecisionReason::None;
}

void AdsLifecycleReducer::admit_target(double now_seconds) noexcept {
    state_.source_decision_available = true;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::Admitted;
    state_.source_decision_reason =
        pipeline_contract::AdsDecisionReason::Admitted;
    state_.target_admitted = true;
    state_.target_acquisition_id = next_target_acquisition_id_++;
    state_.acquisition_started_seconds = now_seconds;
    state_.acquisition_completed_seconds = 0.0;
    state_.acquisition_begin_ns = seconds_to_ns(now_seconds);
    state_.acquisition_complete_ns = 0;
    state_.state =
        pipeline_contract::AdsAcquisitionState::AcquiringNominal;
    state_.decision_reason = pipeline_contract::AdsDecisionReason::Admitted;
}

void AdsLifecycleReducer::accept_continuation() noexcept {
    state_.source_decision_available = true;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::AcceptedContinuation;
    state_.source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
}

void AdsLifecycleReducer::reject_source(
    pipeline_contract::AdsDecisionReason reason) noexcept {
    state_.source_decision_available = true;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::Rejected;
    state_.source_decision_reason = reason;
    state_.decision_reason = reason;
}

void AdsLifecycleReducer::mark_already_consumed() noexcept {
    reject_source(pipeline_contract::AdsDecisionReason::AdsAlreadyConsumed);
}

void AdsLifecycleReducer::wait_for_target() noexcept {
    state_.evidence_wait = state_.target_admitted ? AdsEvidenceWait::SameTarget : AdsEvidenceWait::InitialTarget;
    state_.state =
        pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget;
}

void AdsLifecycleReducer::expire_wait(
    pipeline_contract::AdsDecisionReason reason,
    double now_seconds) noexcept {
    if (state_.target_admitted) return;
    state_.state = pipeline_contract::AdsAcquisitionState::Completed;
    state_.decision_reason = reason;
    state_.terminal_reason = reason;
    state_.acquisition_completed_seconds = now_seconds;
    state_.snap_consumed = true;
    state_.evidence_wait = AdsEvidenceWait::None;
}

void AdsLifecycleReducer::stay_nominal() noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::AcquiringNominal;
}

void AdsLifecycleReducer::extend() noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::AcquiringExtended;
    state_.decision_reason = pipeline_contract::AdsDecisionReason::None;
}

void AdsLifecycleReducer::enter_manual_safe() noexcept {
    state_.state =
        pipeline_contract::AdsAcquisitionState::AcquiringManualSafe;
    // This is a non-terminal phase reason. terminal_reason and completion
    // timestamps remain untouched because a timer is not proof of arrival.
    state_.decision_reason =
        pipeline_contract::AdsDecisionReason::ExtensionBudgetElapsed;
}

void AdsLifecycleReducer::complete(
    pipeline_contract::AdsDecisionReason reason,
    double now_seconds) noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::Completed;
    state_.decision_reason = reason;
    state_.terminal_reason = reason;
    state_.acquisition_completed_seconds = now_seconds;
    state_.acquisition_complete_ns = seconds_to_ns(now_seconds);
    state_.snap_consumed = true;
    state_.evidence_wait = AdsEvidenceWait::None;
}

void AdsLifecycleReducer::consume() noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::Consumed;
}

void AdsLifecycleReducer::clear_unadmitted_target_identity() noexcept {
    if (state_.target_admitted) return;
    state_.acquisition_started_seconds = 0.0;
    state_.target_acquisition_id = 0;
}

void AdsLifecycleReducer::project(
    pipeline_contract::TargetPlan* plan,
    double now_seconds) const noexcept {
    if (plan == nullptr) return;
    plan->ads_acquisition_state = state_.state;
    plan->source_decision_available = state_.source_decision_available;
    plan->source_decision_outcome = state_.source_decision_outcome;
    plan->source_decision_reason = state_.source_decision_reason;
    plan->acquisition_terminal_reason = state_.terminal_reason;
    plan->ads_decision_reason = state_.decision_reason;
    plan->physical_ads_epoch = state_.physical_ads_epoch;
    plan->target_acquisition_id = state_.target_acquisition_id;
    plan->ads_acquisition_active =
        state_.target_admitted && !state_.snap_consumed;
    plan->ads_acquisition_exists = state_.target_acquisition_id != 0;
    plan->ads_acquisition_begin_ns = state_.acquisition_begin_ns;
    plan->ads_acquisition_complete_ns = state_.acquisition_complete_ns;
    plan->ads_epoch_elapsed_ms = state_.epoch_active
        ? static_cast<float>(std::max(
            0.0, (now_seconds - state_.epoch_started_seconds) * 1000.0))
        : 0.0f;
    plan->acquisition_elapsed_ms = state_.target_admitted
        ? static_cast<float>(std::max(
            0.0, (now_seconds - state_.acquisition_started_seconds) * 1000.0))
        : 0.0f;
}

}  // namespace controller_native

namespace controller_native {
void AdsLifecycleReducer::advance(bool scope_active, bool settled, bool center_cross, double now_seconds) noexcept {
    const float elapsed_ms = state_.target_admitted ? static_cast<float>(std::max(
        0.0, (now_seconds - state_.acquisition_started_seconds) * 1000.0)) : 0.0f;
    if (center_cross) note_center_cross();

    const bool extension_budget_elapsed = state_.target_admitted &&
        config_.extension_ms > 0.0f &&
        elapsed_ms >=
            std::max(0.0f, config_.nominal_ms) +
                config_.extension_ms;
    const bool nominal_elapsed = state_.target_admitted &&
        elapsed_ms >=
            std::max(0.0f, config_.nominal_ms);
    if (!scope_active) {
        state_.task_mode = pipeline_contract::ControlMode::Manual;
    } else if (state_.snap_consumed) {
        consume();
        state_.task_mode = pipeline_contract::ControlMode::BodyLockFollow;
    } else if (state_.target_admitted) {
        if (settled) {
            complete(
                pipeline_contract::AdsDecisionReason::Settled,
                now_seconds);
            state_.task_mode = pipeline_contract::ControlMode::BodyLockFollow;
        } else if (center_cross) {
            // The strict radial predicate above proves that this one-per-LT
            // positioning job reached and passed the reticle center. Keeping
            // full ADS authority after that event makes the next correction
            // reverse through center again. Consume this snap and let the
            // continuous BodyLock owner track the remaining target motion.
            complete(
                pipeline_contract::AdsDecisionReason::CenterCross,
                now_seconds);
            state_.task_mode = pipeline_contract::ControlMode::BodyLockFollow;
        } else if (extension_budget_elapsed) {
            // 220 ms is extra time after the nominal phase, not the total ADS
            // lifetime. Exhaustion cannot manufacture success. Keep the same
            // full ADS solver alive for neutral input, while publishing a
            // phase that final arbitration treats as manual-safe.
            enter_manual_safe();
            state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
        } else if (nominal_elapsed &&
                   state_.state ==
                       pipeline_contract::AdsAcquisitionState::AcquiringNominal) {
            // Ending exclusive input ownership is a clock invariant, not a
            // detector decision. A replay tick therefore enters the extension
            // phase on time; fresh evidence is still required for every real
            // completion path above.
            extend();
            state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
        } else if (state_.state ==
                   pipeline_contract::AdsAcquisitionState::AcquiringExtended) {
            extend();
            state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
        } else if (state_.state ==
                   pipeline_contract::AdsAcquisitionState::AcquiringManualSafe) {
            enter_manual_safe();
            state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
        } else {
            stay_nominal();
            state_.task_mode = pipeline_contract::ControlMode::AdsAcquire;
        }
    } else if (scope_active) {
        wait_for_target();
        state_.task_mode = pipeline_contract::ControlMode::Manual;
    } else {
        state_.task_mode = pipeline_contract::ControlMode::Manual;
    }
}
}  // namespace controller_native
