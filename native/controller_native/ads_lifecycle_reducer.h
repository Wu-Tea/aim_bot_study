#pragma once

#include "pipeline_contract/target_plan.h"

#include <cstdint>

namespace controller_native {

struct AdsLifecycleSnapshot {
    bool epoch_active = false;
    bool snap_consumed = false;
    bool target_admitted = false;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t target_acquisition_id = 0;
    pipeline_contract::AdsAcquisitionState state =
        pipeline_contract::AdsAcquisitionState::Idle;
    pipeline_contract::AdsDecisionReason decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    bool source_decision_available = false;
    pipeline_contract::SourceDecisionOutcome source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    pipeline_contract::AdsDecisionReason source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    pipeline_contract::AdsDecisionReason terminal_reason =
        pipeline_contract::AdsDecisionReason::None;
    std::uint64_t acquisition_begin_ns = 0;
    std::uint64_t acquisition_complete_ns = 0;
    bool center_cross_seen = false;
    bool target_switch_seen = false;
    double epoch_started_seconds = 0.0;
    double acquisition_started_seconds = 0.0;
    double acquisition_completed_seconds = 0.0;
};

// Sole owner of ADS epoch and acquisition lifecycle state. Decisions remain
// pure conditions in the coordinator façade; every state mutation enters here
// through a named transition.
class AdsLifecycleReducer {
public:
    void reset() noexcept;
    void begin_tick() noexcept;
    void begin_epoch(std::uint64_t epoch, double now_seconds) noexcept;
    void release_scope() noexcept;
    void admit_target(double now_seconds) noexcept;
    void accept_continuation() noexcept;
    void reject_source(pipeline_contract::AdsDecisionReason reason) noexcept;
    void mark_already_consumed() noexcept;
    void wait_for_target() noexcept;
    void expire_wait(
        pipeline_contract::AdsDecisionReason reason,
        double now_seconds) noexcept;
    void stay_nominal() noexcept;
    void extend() noexcept;
    void enter_manual_safe() noexcept;
    void complete(
        pipeline_contract::AdsDecisionReason reason,
        double now_seconds) noexcept;
    void consume() noexcept;
    void note_center_cross() noexcept { state_.center_cross_seen = true; }
    void note_target_switch() noexcept { state_.target_switch_seen = true; }
    void clear_unadmitted_target_identity() noexcept;
    void set_decision_reason(
        pipeline_contract::AdsDecisionReason reason) noexcept {
        state_.decision_reason = reason;
    }
    void set_source_decision_reason_from_current() noexcept {
        state_.source_decision_reason = state_.decision_reason;
    }
    void project(
        pipeline_contract::TargetPlan* plan,
        double now_seconds) const noexcept;

    const AdsLifecycleSnapshot& snapshot() const noexcept { return state_; }

private:
    AdsLifecycleSnapshot state_{};
    std::uint64_t next_target_acquisition_id_ = 1;
};

}  // namespace controller_native
