#include "ads_lifecycle_reducer.h"

#include <algorithm>
#include <cmath>

namespace controller_native {
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

void AdsLifecycleReducer::begin_tick() noexcept {
    state_.source_decision_available = false;
    state_.source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    state_.source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
}

void AdsLifecycleReducer::begin_epoch(
    std::uint64_t epoch,
    double now_seconds) noexcept {
    state_.physical_ads_epoch = epoch;
    state_.epoch_started_seconds = now_seconds;
    state_.epoch_active = true;
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
}

void AdsLifecycleReducer::stay_nominal() noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::AcquiringNominal;
}

void AdsLifecycleReducer::extend() noexcept {
    state_.state = pipeline_contract::AdsAcquisitionState::AcquiringExtended;
    state_.decision_reason = pipeline_contract::AdsDecisionReason::None;
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
