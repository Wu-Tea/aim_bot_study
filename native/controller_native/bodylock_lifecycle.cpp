#include "bodylock_lifecycle.h"

namespace controller_native {

void BodylockLifecycle::reset() {
    state_ = pipeline_contract::BodylockLifecycleState::Inactive;
    track_id_ = 0;
}

BodylockLifecycleDecision BodylockLifecycle::update(const BodylockLifecycleInput& input) {
    using pipeline_contract::AssistAuthorityReason;
    using pipeline_contract::AssistAuthorityState;
    using pipeline_contract::BodylockLifecycleState;

    const bool was_active =
        state_ == BodylockLifecycleState::Warm ||
        state_ == BodylockLifecycleState::Tracking ||
        state_ == BodylockLifecycleState::Coast;

    if (!input.aiming) {
        return transition(
            was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
            BodylockTransitionReason::AimReleased,
            0,
            was_active);
    }

    if (state_ == BodylockLifecycleState::Yield) {
        return transition(
            BodylockLifecycleState::Inactive,
            BodylockTransitionReason::AuthorityLost,
            0,
            false);
    }

    if (!input.bodylock_available) {
        return transition(
            was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
            BodylockTransitionReason::BodylockUnavailable,
            0,
            was_active);
    }

    if (input.authority == AssistAuthorityState::ObservedStrong &&
        input.selected_track_id != 0) {
        if (was_active && track_id_ != 0 && input.selected_track_id != track_id_) {
            return transition(
                BodylockLifecycleState::Warm,
                BodylockTransitionReason::TargetSwitched,
                input.selected_track_id,
                true);
        }
        if (state_ == BodylockLifecycleState::Inactive) {
            return transition(
                BodylockLifecycleState::Warm,
                BodylockTransitionReason::Observed,
                input.selected_track_id,
                false);
        }
        return transition(
            BodylockLifecycleState::Tracking,
            BodylockTransitionReason::Observed,
            input.selected_track_id,
            false);
    }

    if (was_active && input.selected_track_id != 0 && track_id_ != 0 &&
        input.selected_track_id != track_id_) {
        return transition(
            BodylockLifecycleState::Yield,
            BodylockTransitionReason::TargetSwitched,
            0,
            true);
    }

    if (input.authority == AssistAuthorityState::Continuity &&
        input.selected_track_id != 0 && input.selected_track_id == track_id_ &&
        was_active) {
        return transition(
            BodylockLifecycleState::Coast,
            BodylockTransitionReason::Continuity,
            track_id_,
            false);
    }

    BodylockTransitionReason reason = BodylockTransitionReason::AuthorityLost;
    if (input.authority_reason == AssistAuthorityReason::TargetSwitched) {
        reason = BodylockTransitionReason::TargetSwitched;
    } else if (input.authority_reason == AssistAuthorityReason::UserYield) {
        reason = BodylockTransitionReason::UserYield;
    }
    return transition(
        was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
        reason,
        0,
        was_active);
}

pipeline_contract::BodylockLifecycleState BodylockLifecycle::state() const {
    return state_;
}

std::uint64_t BodylockLifecycle::track_id() const {
    return track_id_;
}

BodylockLifecycleDecision BodylockLifecycle::transition(
    pipeline_contract::BodylockLifecycleState next,
    BodylockTransitionReason reason,
    std::uint64_t track_id,
    bool reset_history) {
    state_ = next;
    track_id_ = track_id;
    return {state_, reason, track_id_, reset_history};
}

}  // namespace controller_native
